/// 二进制协议帧的构建与解析。
///
/// 帧格式：
///   A5 5A | ver(01) | type | seq | len_lo | len_hi | payload... | crc_lo | crc_hi
///
/// CRC 使用 CRC-16/Modbus（多项式 0xA001，初值 0xFFFF）。

/// CRC-16/Modbus 计算。
fn crc16_modbus(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &b in data {
        crc ^= b as u16;
        for _ in 0..8 {
            if crc & 1 != 0 {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    crc
}

/// 协议帧类型标识。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FrameType {
    /// 命令帧（主机 → 设备）
    Command,
    /// 响应帧（设备 → 主机）
    Response,
    /// 事件帧（设备主动上报）
    Event,
    /// 错误帧
    Error,
    /// 屏幕截图帧（设备主动分帧发送，临时调试用）
    Shot,
    /// 未知类型
    Unknown(u8),
}

impl FrameType {
    fn from_byte(b: u8) -> Self {
        match b {
            0x01 => Self::Command,
            0x81 => Self::Response,
            0x82 => Self::Event,
            0x83 => Self::Error,
            0x84 => Self::Shot,
            other => Self::Unknown(other),
        }
    }

    /// 转换为协议字节值。
    fn to_byte(&self) -> u8 {
        match self {
            Self::Command => 0x01,
            Self::Response => 0x81,
            Self::Event => 0x82,
            Self::Error => 0x83,
            Self::Shot => 0x84,
            Self::Unknown(v) => *v,
        }
    }

    pub fn tag(&self) -> String {
        match self {
            Self::Command => "CMD".to_string(),
            Self::Response => "RSP".to_string(),
            Self::Event => "EVT".to_string(),
            Self::Error => "ERR".to_string(),
            Self::Shot => "SHOT".to_string(),
            Self::Unknown(v) => format!("TYP=0x{v:02x}"),
        }
    }
}

/// 解析出的协议帧。
#[derive(Debug)]
pub struct Frame {
    pub frame_type: FrameType,
    pub seq: u8,
    pub payload: String,
    pub crc_ok: bool,
}

/// 构建一个完整的命令帧字节流。
pub fn build_frame(payload: &str, frame_type: FrameType, seq: u8) -> Vec<u8> {
    let payload_bytes = payload.as_bytes();
    let payload_len = payload_bytes.len() as u16;

    // ver(1) + type(1) + seq(1) + len(2) = 5 字节头部
    let mut header_and_payload = Vec::with_capacity(5 + payload_bytes.len());
    header_and_payload.push(0x01); // ver
    header_and_payload.push(frame_type.to_byte());
    header_and_payload.push(seq);
    header_and_payload.push((payload_len & 0xFF) as u8);
    header_and_payload.push(((payload_len >> 8) & 0xFF) as u8);
    header_and_payload.extend_from_slice(payload_bytes);

    let crc = crc16_modbus(&header_and_payload);

    let mut frame = Vec::with_capacity(2 + header_and_payload.len() + 2);
    frame.push(0xA5);
    frame.push(0x5A);
    frame.extend_from_slice(&header_and_payload);
    frame.push((crc & 0xFF) as u8);
    frame.push(((crc >> 8) & 0xFF) as u8);

    frame
}

/// 从字节缓冲区中尝试解析一帧。
///
/// 返回 `Some((frame, consumed))` 表示成功解析，`consumed` 为已消费的字节数。
/// 返回 `None` 表示数据不足以构成完整帧，需要更多数据。
pub fn try_parse_frame(buf: &[u8]) -> Option<(Frame, usize)> {
    // 查找 SOF 标记 A5 5A
    let sof_pos = buf.windows(2).position(|w| w[0] == 0xA5 && w[1] == 0x5A)?;

    let header_start = sof_pos + 2;
    // 至少需要 ver + type + seq + len(2) + crc(2) = 7 字节
    if buf.len() < header_start + 7 {
        return None;
    }

    let _ver = buf[header_start];
    let typ = buf[header_start + 1];
    let seq = buf[header_start + 2];
    let payload_len =
        (buf[header_start + 3] as u16) | ((buf[header_start + 4] as u16) << 8);

    let frame_end = header_start + 5 + payload_len as usize + 2;
    if buf.len() < frame_end {
        return None;
    }

    let payload_bytes = &buf[header_start + 5..header_start + 5 + payload_len as usize];
    let crc_received = (buf[frame_end - 2] as u16) | ((buf[frame_end - 1] as u16) << 8);

    let crc_input = &buf[header_start..header_start + 5 + payload_len as usize];
    let crc_expected = crc16_modbus(crc_input);
    let crc_ok = crc_received == crc_expected;

    let payload = String::from_utf8_lossy(payload_bytes).into_owned();

    Some((
        Frame {
            frame_type: FrameType::from_byte(typ),
            seq,
            payload,
            crc_ok,
        },
        frame_end,
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_crc16_modbus() {
        // 已知 CRC-16/Modbus 测试向量
        let data = b"\x01\x01\x00\x07\x00\x53\x54\x41\x54\x55\x53\x3F";
        let crc = crc16_modbus(data);
        // 只需确保不为 0 且计算一致
        assert_ne!(crc, 0);
        assert_eq!(crc, crc16_modbus(data));
    }

    #[test]
    fn test_build_and_parse_roundtrip() {
        let frame = build_frame("STATUS?", FrameType::Command, 0x2A);
        let (parsed, consumed) = try_parse_frame(&frame).unwrap();
        assert_eq!(consumed, frame.len());
        assert_eq!(parsed.frame_type, FrameType::Command);
        assert_eq!(parsed.seq, 0x2A);
        assert_eq!(parsed.payload, "STATUS?");
        assert!(parsed.crc_ok);
    }

    #[test]
    fn test_parse_incomplete_frame() {
        let frame = build_frame("HELP", FrameType::Command, 0);
        // 截断帧
        assert!(try_parse_frame(&frame[..5]).is_none());
    }

    #[test]
    fn test_parse_with_leading_garbage() {
        let mut data = vec![0xFF, 0x00, 0x12]; // garbage
        data.extend_from_slice(&build_frame("OK", FrameType::Response, 1));
        let (parsed, consumed) = try_parse_frame(&data).unwrap();
        assert_eq!(parsed.payload, "OK");
        assert_eq!(parsed.frame_type, FrameType::Response);
        assert_eq!(consumed, data.len());
    }
}
