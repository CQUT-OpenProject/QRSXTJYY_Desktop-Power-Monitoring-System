//! CRC 有效性测试：发送正确 CRC 帧 vs 故意破坏 CRC 的帧，对比设备响应。

use std::io::{Read, Write};
use std::time::{Duration, Instant};

use serialport::SerialPort;

use serial_cmd::protocol::{build_frame, try_parse_frame, FrameType};

fn main() {
    let port_path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "/dev/cu.usbserial-10".to_string());

    let mut port = serialport::new(&port_path, 115200)
        .timeout(Duration::from_millis(50))
        .open()
        .expect("无法打开串口");

    std::thread::sleep(Duration::from_millis(500));
    let _ = port.clear(serialport::ClearBuffer::Input);

    // ---- 测试 1：正确 CRC 帧 ----
    println!("=== 测试 1：正确 CRC 帧 (STATUS?) ===");
    let good_frame = build_frame("STATUS?", FrameType::Command, 0x01);
    println!(
        "发送帧 ({} 字节): {:02X?}",
        good_frame.len(),
        &good_frame
    );
    port.write_all(&good_frame).unwrap();
    port.flush().unwrap();

    let good_responses = collect_responses(&mut port, Duration::from_secs(2));
    println!("收到 {} 条响应", good_responses.len());
    for r in &good_responses {
        println!("  << [{}] {} (CRC OK: {})", r.frame_type.tag(), r.payload, r.crc_ok);
    }

    std::thread::sleep(Duration::from_millis(200));
    let _ = port.clear(serialport::ClearBuffer::Input);

    // ---- 测试 2：故意破坏 CRC 的帧 ----
    println!("\n=== 测试 2：破坏 CRC 帧 (STATUS?, CRC 翻转末字节) ===");
    let mut bad_frame = build_frame("STATUS?", FrameType::Command, 0x02);
    let last = bad_frame.len() - 1;
    bad_frame[last] ^= 0xFF; // 翻转 CRC 末字节
    println!(
        "发送帧 ({} 字节): {:02X?}",
        bad_frame.len(),
        &bad_frame
    );
    port.write_all(&bad_frame).unwrap();
    port.flush().unwrap();

    let bad_responses = collect_responses(&mut port, Duration::from_secs(2));
    println!("收到 {} 条响应", bad_responses.len());
    for r in &bad_responses {
        println!("  << [{}] {} (CRC OK: {})", r.frame_type.tag(), r.payload, r.crc_ok);
    }

    std::thread::sleep(Duration::from_millis(200));
    let _ = port.clear(serialport::ClearBuffer::Input);

    // ---- 测试 3：篡改 payload 但保留原 CRC ----
    println!("\n=== 测试 3：篡改 payload 但保留原 CRC (HELP→HALP) ===");
    let mut tampered = build_frame("HELP", FrameType::Command, 0x03);
    // 把 payload 中的 'E' 改成 'A'（HELP → HALP），CRC 不变
    // SOF(2) + ver(1) + type(1) + seq(1) + len(2) = 7, payload 从 offset 7 开始
    // "HELP" → 改 offset 8 的 'E' 为 'A'
    tampered[8] = b'A';
    println!(
        "发送帧 ({} 字节): {:02X?}",
        tampered.len(),
        &tampered
    );
    port.write_all(&tampered).unwrap();
    port.flush().unwrap();

    let tampered_responses = collect_responses(&mut port, Duration::from_secs(2));
    println!("收到 {} 条响应", tampered_responses.len());
    for r in &tampered_responses {
        println!("  << [{}] {} (CRC OK: {})", r.frame_type.tag(), r.payload, r.crc_ok);
    }

    // ---- 汇总 ----
    println!("\n=== 结论 ===");
    println!(
        "正确 CRC: {} 条响应 → {}",
        good_responses.len(),
        if !good_responses.is_empty() { "✓ 正常响应" } else { "✗ 无响应" }
    );
    println!(
        "破坏 CRC: {} 条响应 → {}",
        bad_responses.len(),
        if bad_responses.is_empty() { "✓ 被正确拒绝" } else { "✗ CRC 校验未生效" }
    );
    println!(
        "篡改 payload: {} 条响应 → {}",
        tampered_responses.len(),
        if tampered_responses.is_empty() { "✓ 被正确拒绝" } else { "✗ CRC 校验未生效" }
    );
}

fn collect_responses(port: &mut Box<dyn SerialPort>, timeout: Duration) -> Vec<serial_cmd::protocol::Frame> {
    let mut buf = Vec::new();
    let mut frames = Vec::new();
    let deadline = Instant::now() + timeout;

    while Instant::now() < deadline {
        let mut chunk = vec![0u8; 1024];
        match port.read(&mut chunk) {
            Ok(n) if n > 0 => {
                chunk.truncate(n);
                buf.extend_from_slice(&chunk);
                loop {
                    match try_parse_frame(&buf) {
                        Some((frame, consumed)) => {
                            frames.push(frame);
                            buf.drain(..consumed);
                        }
                        None => break,
                    }
                }
            }
            _ => std::thread::sleep(Duration::from_millis(10)),
        }
    }
    frames
}
