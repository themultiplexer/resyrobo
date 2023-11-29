use log::*;
use simplelog::*;
use std::fs::File;

pub fn start_logging() {
    let mut c = Config::default();
    c.time = None;
    c.level = None;
    CombinedLogger::init(vec![
        WriteLogger::new(LevelFilter::Error, c, File::create("error.log").unwrap()),
        WriteLogger::new(
            LevelFilter::Info,
            c,
            File::create("/tmp/timing.log").unwrap(),
        ),
    ])
    .unwrap();
}

pub fn log_with_time(message: &str) {
    let mut timeval = libc::timeval {
        tv_sec: 0,
        tv_usec: 0,
    };
    unsafe {
        libc::gettimeofday(&mut timeval, std::ptr::null_mut());
    }
    info!("{},{}|{}", timeval.tv_sec, timeval.tv_usec, message);
}
