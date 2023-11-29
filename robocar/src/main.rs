#![feature(integer_atomics)]
mod hardware;
mod logging;

use hardware::{Device, Motor, Rfid};
use logging::*;
use nix::sys::signal::*;
use rust_gpiozero::*;
use std::fmt::*;
use std::process::exit;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::{thread, time};

#[derive(PartialEq, Debug)]
enum Mode {
    Idle,
    WallFollowing,
    LineFollowing,
    BetweenLines,
    Straight,
    EndOfRamp,
    TopOfRamp,
}

impl From<usize> for Mode {
    fn from(val: usize) -> Self {
        match val {
            0 => Mode::Idle,
            1 => Mode::WallFollowing,
            2 => Mode::LineFollowing,
            3 => Mode::BetweenLines,
            4 => Mode::Straight,
            5 => Mode::EndOfRamp,
            6 => Mode::TopOfRamp,
            _ => unreachable!(),
        }
    }
}

impl Display for Mode {
    fn fmt(&self, f: &mut Formatter) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}

static MODE: AtomicUsize = AtomicUsize::new(Mode::Idle as usize);

extern "C" fn handle_siguser(_: i32) {
    log_with_time("Emergency Button pressed");
    MODE.store(Mode::Idle as usize, Ordering::SeqCst);
}

extern "C" fn handle_sigint(_: i32) {
    halt("Ctrl-C");
}

fn halt(_msg: &str) {
    exit(0);
}

pub fn setup_sched(cpu: usize) {
    unsafe {
        let sched = libc::SCHED_FIFO;
        let prio = libc::sched_get_priority_max(sched);
        let params = libc::sched_param {
            sched_priority: prio,
        };
        let _ = libc::sched_setscheduler(0, sched, &params);
        let mut set: libc::cpu_set_t = std::mem::zeroed();
        libc::CPU_SET(cpu, &mut set);
        libc::sched_setaffinity(0, std::mem::size_of::<libc::cpu_set_t>(), &set);
    }
}

fn main() {
    setup_sched(0);
    const WALL_FOLLOWING: [u8; 4] = [174, 11, 30, 43];
    const BETWEEN_LINES: [u8; 4] = [186, 23, 207, 41];
    const STRAIGHT: [u8; 4] = [183, 25, 34, 43];
    const LINE_FOLLOWING: [u8; 4] = [125, 239, 33, 43];
    const TOP_OF_RAMP: [u8; 4] = [195, 136, 144, 26];
    const END_OF_RAMP: [u8; 4] = [193, 76, 3, 32];

    let int_action = SigAction::new(
        SigHandler::Handler(handle_sigint),
        SaFlags::empty(),
        SigSet::empty(),
    );
    unsafe {
        let _ = sigaction(SIGINT, &int_action);
    }

    let sig_action = SigAction::new(
        SigHandler::Handler(handle_siguser),
        SaFlags::empty(),
        SigSet::empty(),
    );
    unsafe {
        let _ = sigaction(SIGUSR1, &sig_action);
    }

    let mut _button = Device::new("/dev/emergency");

    let lightbarrier_left = Device::new("/dev/lightbarrier-left");
    let lightbarrier_right = Device::new("/dev/lightbarrier-right");

    let mut ultrasonic_left = Device::new("/dev/ultrasonic-left");
    let mut ultrasonic_right = Device::new("/dev/ultrasonic-right");

    let motor_left = Motor::new("/dev/motor-left", lightbarrier_left.clone(), "left");
    let motor_right = Motor::new("/dev/motor-right", lightbarrier_right.clone(), "right");

    start_logging();

    let mut mfrc522 = Rfid::new(25);
    thread::spawn(move || loop {
        log_with_time("0|RFID");
        if let Ok(atqa) = mfrc522.reqa() {
            if let Ok(uid) = mfrc522.select(&atqa) {
                if uid.bytes() == &WALL_FOLLOWING {
                    MODE.store(Mode::WallFollowing as usize, Ordering::SeqCst);
                } else if uid.bytes() == &LINE_FOLLOWING {
                    MODE.store(Mode::LineFollowing as usize, Ordering::SeqCst);
                } else if uid.bytes() == &BETWEEN_LINES {
                    MODE.store(Mode::BetweenLines as usize, Ordering::SeqCst);
                } else if uid.bytes() == &STRAIGHT {
                    MODE.store(Mode::Straight as usize, Ordering::SeqCst);
                } else if uid.bytes() == &END_OF_RAMP {
                    log_with_time("0|SLEEP");
                    thread::sleep(time::Duration::from_millis(400));
                    log_with_time("1|SLEEP");
                    MODE.store(Mode::EndOfRamp as usize, Ordering::SeqCst);
                } else if uid.bytes() == &TOP_OF_RAMP {
                    MODE.store(Mode::TopOfRamp as usize, Ordering::SeqCst);
                }
            }
        }
        log_with_time("1|RFID");
        thread::sleep(time::Duration::from_millis(30));
    });

    let left = InputDevice::new(14);
    let middle_left = InputDevice::new(15);
    let middle_right = InputDevice::new(12);
    let right = InputDevice::new(16);

    // Staying and driving in between to line
    let between_lines = |fwd, rev, reverse| {
        if left.value() == true || middle_left.value() == true {
            if reverse {
                motor_right.set_direct_speed(-100);
                thread::sleep(time::Duration::from_millis(5));
                motor_left.set_direct_speed(100);
            } else {
                motor_right.set_direct_speed(rev);
                motor_left.set_direct_speed(fwd);
            }
        } else if right.value() == true || middle_right.value() == true {
            if reverse {
                motor_left.set_direct_speed(-100);
                thread::sleep(time::Duration::from_millis(5));
                motor_right.set_direct_speed(100);
            } else {
                motor_left.set_direct_speed(rev);
                motor_right.set_direct_speed(fwd);
            }
        } else {
            motor_left.set_direct_speed(fwd);
            motor_right.set_direct_speed(fwd);
        }
    };

    // Main Loop
    loop {
        log_with_time(&format!("1|MAIN"));
        let mode = MODE.load(Ordering::SeqCst).into();
        
        let left_distance = ultrasonic_left.read() as f32 / 58.2;
        let right_distance = ultrasonic_right.read() as f32 / 58.2;

        match mode {
            Mode::WallFollowing => {
                if left_distance < 5.0 {
                    motor_left.set_direct_speed(60);
                    motor_right.set_direct_speed(-60);
                } else if right_distance < 5.0 {
                    motor_left.set_direct_speed(-60);
                    motor_right.set_direct_speed(60);
                } else if left_distance - right_distance > 30.0 {
                    motor_left.set_direct_speed(0);
                    motor_right.set_direct_speed(60);
                } else if left_distance - right_distance < -30.0 {
                    motor_left.set_direct_speed(60);
                    motor_right.set_direct_speed(0);
                } else {
                    motor_left.set_direct_speed(60);
                    motor_right.set_direct_speed(60);
                }
            }
            Mode::LineFollowing => {
                if left.value() == true {
                    motor_left.set_direct_speed(-60);
                    motor_right.set_direct_speed(60);
                } else if right.value() == true {
                    motor_left.set_direct_speed(60);
                    motor_right.set_direct_speed(-60);
                } else {
                    motor_left.set_direct_speed(100);
                    motor_right.set_direct_speed(100);
                }
            }
            Mode::Straight => {
                motor_left.set_direct_speed(-100);
                thread::sleep(time::Duration::from_millis(50));
                motor_left.set_target_and_estimate(0);
                motor_right.set_target_and_estimate(100);
                thread::sleep(time::Duration::from_millis(3000));

                let mut same_counter = 0;
                while MODE.load(Ordering::SeqCst) == 4 {
                    log_with_time(&format!("1|ULT"));
                    let left_distance = ultrasonic_left.read() as f32 / 58.2;
                    let right_distance = ultrasonic_right.read() as f32 / 58.2;

                    if left_distance < 150.0
                        && left_distance > 125.0
                        && right_distance < 150.0
                        && right_distance > 125.0
                    {
                        same_counter += 1;
                    } else {
                        same_counter = 0;
                    }

                    if same_counter == 12 {
                        break;
                    }
                    log_with_time(&format!("2|ULT"));
                    thread::sleep(time::Duration::from_millis(15));
                }

                motor_left.set_target_and_estimate(0);
                motor_right.set_target_and_estimate(0);
                thread::sleep(time::Duration::from_millis(1000));

                let mut count = 0;
                let mut last = false;
                while MODE.load(Ordering::SeqCst) == 4 {
                    log_with_time(&format!("1|LINES"));
                    let left_distance = ultrasonic_left.read() as f32 / 58.2;
                    let right_distance = ultrasonic_right.read() as f32 / 58.2;
                    if left_distance < 25.0 || right_distance < 25.0 {
                        motor_left.set_direct_speed(0);
                        motor_right.set_direct_speed(0);
                    } else {
                        motor_left.set_target_and_estimate(300);
                        motor_right.set_target_and_estimate(300);
                    }
                    let new = middle_left.value();
                    if last != new {
                        if !new {
                            count += 1;
                        }
                        if count == 8 {
                            MODE.store(Mode::Idle as usize, Ordering::SeqCst);
                            break;
                        }

                        thread::sleep(time::Duration::from_millis(200));
                        last = new;
                    }
                    log_with_time(&format!("2|LINES"));
                    thread::sleep(time::Duration::from_millis(5));
                }
            }
            Mode::BetweenLines => between_lines(100, 0, false),
            Mode::Idle => {
                motor_left.set_direct_speed(0);
                motor_right.set_direct_speed(0);
            }
            Mode::EndOfRamp => between_lines(-15, -100, true),
            Mode::TopOfRamp => between_lines(60, 0, false),
        }

        log_with_time(&format!("3|{}", mode));

        if mode != Mode::Idle && mode != Mode::WallFollowing && mode != Mode::EndOfRamp {
            if left_distance < 25.0 || right_distance < 25.0 {
                motor_left.set_direct_speed(0);
                motor_right.set_direct_speed(0);
            }
        }

        log_with_time(&format!("4|END"));

        thread::sleep(time::Duration::from_millis(20));
    }
}
