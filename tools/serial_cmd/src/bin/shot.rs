//! 屏幕截图接收：发送 SHOT 命令，收集设备分帧上传的 RLE 十六进制数据，重建为 PNG。
//!
//! 传输契约见设备端 app/src/app_screenshot.c：
//!   头帧 (0x84)：SHOT BEGIN W=<宽> H=<高> FMT=RLE16
//!   数据帧 (0x84)：纯大端十六进制 token 流，每 token 8 字符 = count(u16) + color(u16,RGB565)
//!   尾帧 (0x84)：SHOT END PIX=<像素总数>
//!
//! 用法：shot [PORT] [OUT.png] [TIMEOUT_SECS]

use std::fs::File;
use std::io::{BufWriter, Read, Write};
use std::time::{Duration, Instant};

use serial_cmd::protocol::{build_frame, try_parse_frame, FrameType};

fn main() {
    let port_path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "/dev/cu.usbserial-110".to_string());
    let out_path = std::env::args()
        .nth(2)
        .unwrap_or_else(|| "screenshot.png".to_string());
    let timeout_secs: u64 = std::env::args()
        .nth(3)
        .and_then(|s| s.parse().ok())
        .unwrap_or(30);

    let mut port = serialport::new(&port_path, 115200)
        .timeout(Duration::from_millis(50))
        .open()
        .expect("无法打开串口");

    std::thread::sleep(Duration::from_millis(500));
    let _ = port.clear(serialport::ClearBuffer::Input);

    let t0 = Instant::now();

    let frame = build_frame("SHOT", FrameType::Command, 0);
    port.write_all(&frame).unwrap();
    port.flush().unwrap();
    println!("[+{:>5}ms] >> SHOT", t0.elapsed().as_millis());

    // 收帧：只认 TYPE=0x84 帧，EVT/RSP 等其他类型一律忽略，避免污染十六进制流。
    let deadline = Instant::now() + Duration::from_secs(timeout_secs);
    let mut buf: Vec<u8> = Vec::new();
    let mut hex_data = String::new();
    let mut width: u32 = 0;
    let mut height: u32 = 0;
    let mut declared_pix: Option<u32> = None;
    let mut began = false;
    let mut done = false;
    let mut data_frames = 0usize;
    let mut bad_crc = 0usize;

    while !done && Instant::now() < deadline {
        let mut chunk = vec![0u8; 4096];
        match port.read(&mut chunk) {
            Ok(n) if n > 0 => {
                chunk.truncate(n);
                buf.extend_from_slice(&chunk);
                loop {
                    match try_parse_frame(&buf) {
                        Some((frame, consumed)) => {
                            buf.drain(..consumed);
                            if !frame.crc_ok {
                                bad_crc += 1;
                                continue;
                            }
                            if frame.frame_type != FrameType::Shot {
                                continue; // 忽略自动上报/响应等非截图帧
                            }
                            let p = frame.payload.as_str();
                            if p.starts_with("SHOT BEGIN") {
                                width = parse_kv(p, "W=").unwrap_or(0);
                                height = parse_kv(p, "H=").unwrap_or(0);
                                hex_data.clear();
                                began = true;
                                println!("[+{:>5}ms] << {}", t0.elapsed().as_millis(), p);
                            } else if p.starts_with("SHOT END") {
                                declared_pix = parse_kv(p, "PIX=");
                                println!("[+{:>5}ms] << {}", t0.elapsed().as_millis(), p);
                                done = true;
                                break;
                            } else if began {
                                hex_data.push_str(p);
                                data_frames += 1;
                            }
                        }
                        None => break,
                    }
                }
            }
            _ => std::thread::sleep(Duration::from_millis(5)),
        }
    }

    if !began {
        eprintln!("错误：未收到 SHOT BEGIN（设备未响应或固件无 SHOT 命令）");
        std::process::exit(1);
    }
    if !done {
        eprintln!("警告：{}s 内未收到 SHOT END，按已收数据尽力重建", timeout_secs);
    }
    if width == 0 || height == 0 {
        eprintln!("错误：BEGIN 帧未解析出 W/H");
        std::process::exit(1);
    }

    // 十六进制 → 字节 → RLE token → 像素
    let bytes = match hex_to_bytes(&hex_data) {
        Ok(b) => b,
        Err(e) => {
            eprintln!("错误：十六进制解码失败：{e}");
            std::process::exit(1);
        }
    };

    let mut pixels: Vec<u16> = Vec::with_capacity((width * height) as usize);
    let mut i = 0usize;
    while i + 4 <= bytes.len() {
        let count = ((bytes[i] as u16) << 8) | bytes[i + 1] as u16;
        let color = ((bytes[i + 2] as u16) << 8) | bytes[i + 3] as u16;
        for _ in 0..count {
            pixels.push(color);
        }
        i += 4;
    }
    if i != bytes.len() {
        eprintln!("警告：数据尾部有 {} 个无法组成完整 token 的字节", bytes.len() - i);
    }

    let expanded = pixels.len();
    let expected = (width as usize) * (height as usize);
    if let Some(pix) = declared_pix {
        if pix as usize != expanded {
            eprintln!("警告：设备声明 PIX={pix} 但实际展开 {expanded}");
        }
    }
    if expanded != expected {
        eprintln!("警告：展开像素 {expanded} != W*H={expected}，将截断/补黑");
        pixels.resize(expected, 0);
    }

    // RGB565 → RGB888
    let mut rgb = Vec::with_capacity(expected * 3);
    for &px in &pixels {
        let [r, g, b] = rgb565_to_rgb888(px);
        rgb.push(r);
        rgb.push(g);
        rgb.push(b);
    }

    // 写 PNG
    let file = File::create(&out_path).expect("无法创建输出文件");
    let mut encoder = png::Encoder::new(BufWriter::new(file), width, height);
    encoder.set_color(png::ColorType::Rgb);
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = encoder.write_header().expect("写 PNG 头失败");
    writer.write_image_data(&rgb).expect("写 PNG 数据失败");

    println!(
        "已保存 {out_path}（{width}×{height}，数据帧 {data_frames}，坏 CRC {bad_crc}，耗时 {}ms）",
        t0.elapsed().as_millis()
    );
}

/// 从 "KEY=VALUE" 形式的空白分隔文本中取出整数值。
fn parse_kv(line: &str, key: &str) -> Option<u32> {
    line.split_whitespace()
        .find_map(|tok| tok.strip_prefix(key))
        .and_then(|v| v.parse().ok())
}

/// RGB565 → RGB888（位复制扩展，避免高位偏暗）。
fn rgb565_to_rgb888(px: u16) -> [u8; 3] {
    let r5 = ((px >> 11) & 0x1F) as u8;
    let g6 = ((px >> 5) & 0x3F) as u8;
    let b5 = (px & 0x1F) as u8;
    [(r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2)]
}

/// 大写/小写十六进制串 → 字节数组。
fn hex_to_bytes(s: &str) -> Result<Vec<u8>, String> {
    if s.len() % 2 != 0 {
        return Err(format!("十六进制长度为奇数：{}", s.len()));
    }
    let raw = s.as_bytes();
    let mut out = Vec::with_capacity(s.len() / 2);
    let mut i = 0usize;
    while i < raw.len() {
        let hi = hex_val(raw[i])?;
        let lo = hex_val(raw[i + 1])?;
        out.push((hi << 4) | lo);
        i += 2;
    }
    Ok(out)
}

fn hex_val(c: u8) -> Result<u8, String> {
    match c {
        b'0'..=b'9' => Ok(c - b'0'),
        b'A'..=b'F' => Ok(c - b'A' + 10),
        b'a'..=b'f' => Ok(c - b'a' + 10),
        other => Err(format!("非法十六进制字符：0x{other:02x}")),
    }
}
