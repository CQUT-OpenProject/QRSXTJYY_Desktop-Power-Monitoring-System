//! 自动上报测试：发送 REPORT ON 后在单进程内连续监听，验证 TYPE=0x82 事件帧周期上报。

use std::io::{Read, Write};
use std::time::{Duration, Instant};

use serialport::SerialPort;

use serial_cmd::protocol::{build_frame, try_parse_frame, FrameType};

fn main() {
    let port_path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "/dev/cu.usbserial-110".to_string());
    let listen_secs: u64 = std::env::args()
        .nth(2)
        .and_then(|s| s.parse().ok())
        .unwrap_or(6);

    let mut port = serialport::new(&port_path, 115200)
        .timeout(Duration::from_millis(50))
        .open()
        .expect("无法打开串口");

    std::thread::sleep(Duration::from_millis(500));
    let _ = port.clear(serialport::ClearBuffer::Input);

    let t0 = Instant::now();

    // 发送 REPORT ON
    let frame = build_frame("REPORT ON", FrameType::Command, 0);
    port.write_all(&frame).unwrap();
    port.flush().unwrap();
    println!("[+{:>5}ms] >> REPORT ON", t0.elapsed().as_millis());

    // 单进程内连续监听
    let deadline = Instant::now() + Duration::from_secs(listen_secs);
    let mut buf = Vec::new();
    let mut evt_count = 0usize;
    let mut rsp_count = 0usize;

    while Instant::now() < deadline {
        let mut chunk = vec![0u8; 1024];
        match port.read(&mut chunk) {
            Ok(n) if n > 0 => {
                chunk.truncate(n);
                buf.extend_from_slice(&chunk);
                loop {
                    match try_parse_frame(&buf) {
                        Some((frame, consumed)) => {
                            let tag = frame.frame_type.tag();
                            if tag == "EVT" {
                                evt_count += 1;
                            } else if tag == "RSP" {
                                rsp_count += 1;
                            }
                            println!(
                                "[+{:>5}ms] << {} {} (crc_ok={})",
                                t0.elapsed().as_millis(),
                                tag,
                                frame.payload,
                                frame.crc_ok
                            );
                            buf.drain(..consumed);
                        }
                        None => break,
                    }
                }
            }
            _ => std::thread::sleep(Duration::from_millis(5)),
        }
    }

    println!(
        "\n=== 汇总：{}s 内收到 {} 条 EVT(0x82) 事件帧, {} 条 RSP 响应帧 ===",
        listen_secs, evt_count, rsp_count
    );
}
