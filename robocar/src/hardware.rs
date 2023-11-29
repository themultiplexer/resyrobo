use crate::logging::*;
use linux_embedded_hal::spidev::SpidevOptions;
use linux_embedded_hal::sysfs_gpio::Direction;
use linux_embedded_hal::{Pin, Spidev};
use mfrc522::Mfrc522;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::Read;
use std::io::Write;
use std::mem::transmute;
use std::sync::atomic::{AtomicBool, AtomicI32, Ordering};
use std::sync::Arc;
use std::{thread, time};

pub struct MotorProperties {
    target_mm_per_s: AtomicI32,
    current_percentage: AtomicI32,
}

impl MotorProperties {
    fn get_target(&self) -> i32 {
        self.target_mm_per_s.load(Ordering::SeqCst)
    }

    fn set_target(&self, new_target: i32) {
        self.target_mm_per_s.store(new_target, Ordering::SeqCst);
    }

    fn get_current(&self) -> i32 {
        self.current_percentage.load(Ordering::SeqCst)
    }

    fn set_current(&self, new_percentage: i32) {
        self.current_percentage
            .store(new_percentage, Ordering::SeqCst);
    }
}

pub struct Motor {
    device: File,
    description: String,
    prev_speed: Arc<AtomicI32>,
    speed_control_activated: Arc<AtomicBool>,
    properties: Arc<MotorProperties>,
}

impl Motor {
    pub fn set_target_and_estimate(&self, new_speed: i32) {
        self.set_speed_control(true);
        if new_speed != self.prev_speed.load(Ordering::SeqCst) {
            self.properties.set_target(new_speed);
            let new_power = if new_speed == 0 {
                0
            } else {
                (new_speed / 10) + 50 * new_speed.signum()
            };
            self.properties.set_current(new_power);
            Motor::set_speed(&self.device, new_power);
            self.prev_speed.store(new_speed, Ordering::SeqCst);
        }
    }

    pub fn new(dev: &str, lightbarrier: Device, description: &str) -> Self {
        let mut motor = Motor {
            device: OpenOptions::new()
                .write(true)
                .open(dev)
                .expect(&format!("Could not open {}", dev)),
            description: String::from(description),
            speed_control_activated: Arc::new(AtomicBool::new(false)),
            prev_speed: Arc::new(AtomicI32::new(0)),
            properties: Arc::new(MotorProperties {
                current_percentage: AtomicI32::new(0),
                target_mm_per_s: AtomicI32::new(0),
            }),
        };
        motor.start_speed_control(lightbarrier);
        motor
    }

    pub fn set_speed_control(&self, activated: bool) {
        self.speed_control_activated
            .store(activated, Ordering::SeqCst);
    }

    pub fn start_speed_control(&mut self, mut lightbarrier: Device) -> thread::JoinHandle<()> {
        let props = self.properties.clone();
        let sc_activated = self.speed_control_activated.clone();
        let file = self.device.try_clone().unwrap();
        let description = self.description.clone();

        thread::spawn(move || {
            thread::sleep(time::Duration::from_millis(100));
            let mut last_lightbarrier_ticks = 0;
            loop {
                let activated = sc_activated.load(Ordering::SeqCst);
                if activated {
                    log_with_time(&format!("1|{}", description));
                    // 1 light barrier break means 1.125 cm.
                    let speed_target = ((props.get_target() as f32 / 9.0) / 10.0) as i32;

                    let lightbarrier_current_ticks = lightbarrier.read();
                    let lightbarrier_relative_ticks =
                        lightbarrier_current_ticks - last_lightbarrier_ticks;
                    last_lightbarrier_ticks = lightbarrier_current_ticks;

                    let lightbarrier_speed = lightbarrier_relative_ticks * speed_target.signum();

                    let new_speed = if lightbarrier_speed > speed_target
                        && props.get_current() - 2 > -100
                    {
                        props.get_current() - 2
                    } else if lightbarrier_speed < speed_target && props.get_current() + 2 < 100 {
                        props.get_current() + 2
                    } else {
                        props.get_current()
                    };

                    props.set_current(new_speed);
                    Motor::set_speed(&file, new_speed);
                    log_with_time(&format!("2|{}", description));
                    thread::sleep(time::Duration::from_millis(100));
                    continue;
                }
                thread::sleep(time::Duration::from_millis(10));
            }
        })
    }

    pub fn set_direct_speed(&self, speed: i32) {
        self.set_speed_control(false);
        self.properties.set_target(0);
        self.properties.set_current(0);
        self.prev_speed.store(0, Ordering::SeqCst);
        Motor::set_speed(&self.device, speed);
    }

    fn set_speed(mut device: &File, speed: i32) {
        let bytes: [u8; 4] = unsafe { transmute(speed) };
        device
            .write_all(&bytes)
            .expect(&format!("Could not write to {:?} motor", device));
    }
}

pub struct Rfid {}

impl Rfid {
    pub fn new(pin_number: u64) -> Mfrc522<Spidev, Pin> {
        let mut spi = Spidev::open("/dev/spidev0.0").unwrap();
        let options = SpidevOptions::new()
            .max_speed_hz(1_000_000)
            .mode(linux_embedded_hal::spidev::SPI_MODE_0)
            .build();
        spi.configure(&options).unwrap();
        let pin = Pin::new(pin_number);
        pin.export().unwrap();
        while !pin.is_exported() {}
        pin.set_direction(Direction::Out).unwrap();
        pin.set_value(1).unwrap();
        Mfrc522::new(spi, pin).unwrap()
    }
}

pub struct Device {
    device: File,
}

impl Device {
    pub fn new(device: &str) -> Self {
        Device {
            device: OpenOptions::new()
                .read(true)
                .write(true)
                .open(device)
                .expect(&format!("Could not open {}", device)),
        }
    }

    pub fn read(&mut self) -> i32 {
        let mut buf = [0; 4];
        self.device.read(&mut buf).unwrap();
        return unsafe { std::mem::transmute::<[u8; 4], i32>(buf) }.to_le();
    }
}

impl Clone for Device {
    fn clone(&self) -> Self {
        Device {
            device: self.device.try_clone().unwrap(),
        }
    }
}
