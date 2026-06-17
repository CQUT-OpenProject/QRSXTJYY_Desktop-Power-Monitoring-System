//! 串口二进制帧命令工具 — 向 STM32 发送命令并接收响应。
//!
//! 用法：
//!   serial_cmd <PORT> <COMMAND>
//!   serial_cmd <PORT> monitor          # 持续监听自动上报和响应
//!
//! 示例：
//!   serial_cmd /dev/tty.usbserial-110 HELP
//!   serial_cmd /dev/tty.usbserial-110 "STATUS?"
//!   serial_cmd /dev/tty.usbserial-110 "PWM SET 1000"
//!   serial_cmd /dev/tty.usbserial-110 "REPORT ON"
//!   serial_cmd /dev/tty.usbserial-110 "DAC SET MODE DUAL"
//!   serial_cmd /dev/tty.usbserial-110 "CAL ZERO"
//!   serial_cmd /dev/tty.usbserial-110 monitor

use serial_cmd::protocol;
mod transport;

use clap::Parser;
use std::thread;
use std::time::Duration;

/// 串口二进制帧命令工具
#[derive(Parser, Debug)]
#[command(name = "serial_cmd", version, about = "向 STM32 发送二进制帧命令并接收响应")]
struct Cli {
    /// 串口路径，例如 /dev/tty.usbserial-110 或 COM3
    port: String,

    /// 要发送的命令，或 "monitor" 进入监听模式
    #[arg(trailing_var_arg = true, allow_hyphen_values = true)]
    command: Vec<String>,
}

fn main() {
    let cli = Cli::parse();

    if cli.command.is_empty() {
        eprintln!("错误：请提供命令参数");
        eprintln!("用法：serial_cmd <PORT> <COMMAND>");
        eprintln!("      serial_cmd <PORT> monitor");
        std::process::exit(1);
    }

    let command = cli.command.join(" ");

    let mut port = match transport::open_port(&cli.port) {
        Ok(p) => p,
        Err(e) => {
            eprintln!("{e}");
            std::process::exit(1);
        }
    };

    // 等待设备就绪
    thread::sleep(Duration::from_millis(500));
    // 清空可能存在的启动事件帧
    let _ = port.clear(serialport::ClearBuffer::Input);

    if command == "monitor" {
        if let Err(e) = transport::monitor_mode(&mut *port) {
            eprintln!("监听中断: {e}");
        }
    } else if command == "REPORT ON" {
        if let Err(e) = transport::send_and_listen(&mut *port, &command, Duration::from_secs(5)) {
            eprintln!("错误: {e}");
        }
    } else {
        if let Err(e) = transport::send_command(&mut *port, &command) {
            eprintln!("错误: {e}");
        }
    }
}
