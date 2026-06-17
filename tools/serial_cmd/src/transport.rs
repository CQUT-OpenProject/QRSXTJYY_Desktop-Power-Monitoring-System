/// 串口传输层：打开端口、发送帧、接收并解析响应。

use std::io;
use std::time::{Duration, Instant};

use serialport::SerialPort;

use serial_cmd::protocol::{self, Frame, FrameType};

const BAUDRATE: u32 = 115200;
const DEFAULT_TIMEOUT: Duration = Duration::from_secs(3);
const READ_CHUNK: usize = 1024;

/// 打开串口并返回端口对象。
pub fn open_port(path: &str) -> Result<Box<dyn SerialPort>, String> {
    serialport::new(path, BAUDRATE)
        .timeout(Duration::from_millis(50))
        .open()
        .map_err(|e| format!("无法打开串口 {path}: {e}"))
}

/// 发送一个命令帧并读取所有响应帧。
pub fn send_command(port: &mut dyn SerialPort, cmd: &str) -> io::Result<Vec<Frame>> {
    let frame = protocol::build_frame(cmd, FrameType::Command, 0);
    port.write_all(&frame)?;
    port.flush()?;
    println!(">> {cmd}");
    read_responses(port, DEFAULT_TIMEOUT)
}

/// 读取串口数据并解析出所有帧，直到超时。
fn read_responses(port: &mut dyn SerialPort, timeout: Duration) -> io::Result<Vec<Frame>> {
    let mut buf = Vec::new();
    let mut responses = Vec::new();
    let mut deadline = Instant::now() + timeout;

    while Instant::now() < deadline {
        let mut chunk = vec![0u8; READ_CHUNK];
        match port.read(&mut chunk) {
            Ok(n) if n > 0 => {
                chunk.truncate(n);
                buf.extend_from_slice(&chunk);
                // 收到数据后延长等待窗口
                deadline = Instant::now() + Duration::from_millis(200);

                // 尝试从缓冲区中连续解析帧
                loop {
                    match protocol::try_parse_frame(&buf) {
                        Some((frame, consumed)) => {
                            print_frame(&frame);
                            responses.push(frame);
                            buf.drain(..consumed);
                        }
                        None => break,
                    }
                }
            }
            Ok(_) => {
                // 没有新数据，短暂休眠
                std::thread::sleep(Duration::from_millis(10));
            }
            Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {
                std::thread::sleep(Duration::from_millis(10));
            }
            Err(e) => return Err(e),
        }
    }

    Ok(responses)
}

/// 持续监听模式：无限循环读取自动上报事件帧。
pub fn monitor_mode(port: &mut dyn SerialPort) -> io::Result<()> {
    println!("监听模式已启动 (Ctrl+C 退出)...");
    let mut buf = Vec::new();

    loop {
        let mut chunk = vec![0u8; READ_CHUNK];
        match port.read(&mut chunk) {
            Ok(n) if n > 0 => {
                chunk.truncate(n);
                buf.extend_from_slice(&chunk);

                loop {
                    match protocol::try_parse_frame(&buf) {
                        Some((frame, consumed)) => {
                            let ts = chrono_now();
                            let tag = frame.frame_type.tag();
                            let crc_mark = if frame.crc_ok { "" } else { " [CRC FAIL]" };
                            println!("[{ts}] << {} {}{}", tag, frame.payload, crc_mark);
                            buf.drain(..consumed);
                        }
                        None => break,
                    }
                }
            }
            Ok(_) => {
                std::thread::sleep(Duration::from_millis(10));
            }
            Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {
                std::thread::sleep(Duration::from_millis(10));
            }
            Err(e) => return Err(e),
        }
    }
}

/// 发送命令后额外监听一段时间（用于 REPORT ON 等会触发后续上报的命令）。
pub fn send_and_listen(port: &mut dyn SerialPort, cmd: &str, listen: Duration) -> io::Result<()> {
    send_command(port, cmd)?;
    println!("\n--- 继续监听 {} 秒自动上报 ---", listen.as_secs());
    read_responses(port, listen)?;
    Ok(())
}

fn print_frame(frame: &Frame) {
    let tag = frame.frame_type.tag();
    let crc_mark = if frame.crc_ok { "" } else { " [CRC FAIL]" };
    println!("<< {tag} {}{crc_mark}", frame.payload);
}

/// 获取当前本地时间的 HH:MM:SS 字符串。
fn chrono_now() -> String {
    use std::time::SystemTime;
    let now = SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .unwrap_or_default();
    let secs = now.as_secs() % 86400;
    // 转换为 UTC+8
    let local_secs = (secs + 8 * 3600) % 86400;
    let h = local_secs / 3600;
    let m = (local_secs % 3600) / 60;
    let s = local_secs % 60;
    format!("{h:02}:{m:02}:{s:02}")
}
