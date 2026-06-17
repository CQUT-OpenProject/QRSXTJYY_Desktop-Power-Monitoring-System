#![cfg_attr(not(test), no_std)]

#[cfg(not(test))]
use core::panic::PanicInfo;

#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {
        core::hint::spin_loop();
    }
}

/// 定点数电参数计算结果。
///
/// 所有物理量使用定点整数表示，避免浮点运算。
/// 布局与 C 侧 app_electrical_params_t 一致。
#[repr(C)]
pub struct PmElectricalResult {
    /// 电压真有效值 × 100（单位 0.01 V）
    pub vrms_x100: u32,
    /// 电流真有效值 × 1000（单位 0.001 A）
    pub irms_x1000: u32,
    /// 漏电流真有效值 × 1000（单位 0.001 A）
    pub ilk_rms_x1000: u32,
    /// 有功功率 × 10（单位 0.1 W），有符号：正=消耗，负=回馈
    pub active_power_x10: i32,
    /// 视在功率 × 10（单位 0.1 VA）
    pub apparent_power_x10: u32,
    /// 功率因数 × 1000（范围 0..1000，例如 810 表示 0.810）
    pub power_factor_x1000: u16,
    /// 保留对齐
    pub reserved: u16,
}

/// 计算功率的定点数结果。
///
/// voltage_x100 表示电压，单位为 0.01 V。
/// current_x1000 表示电流，单位为 0.001 A。
/// 返回值表示功率，单位为 0.01 W。
#[no_mangle]
pub extern "C" fn pm_calc_power_x100(voltage_x100: u32, current_x1000: u32) -> u32 {
    voltage_x100.saturating_mul(current_x1000) / 1000
}

/// 整数平方根（Newton's method，返回 floor(sqrt(n))）。
///
/// 用于 RMS 计算中把均方值开方得到有效值。
fn isqrt(n: u64) -> u32 {
    if n == 0 {
        return 0;
    }
    let mut x = n;
    let mut y = (x + 1) >> 1;
    while y < x {
        x = y;
        y = (x + n / x) >> 1;
    }
    x as u32
}

/// 计算单通道 DC 偏移。
///
/// 如果 provided_zero 非 0 则直接使用；否则从样点数据估算算术平均。
fn compute_zero(samples: &[u16], provided_zero: u16) -> u32 {
    if provided_zero != 0 {
        return provided_zero as u32;
    }

    let sum: u64 = samples.iter().map(|&s| s as u64).sum();
    let n = samples.len() as u64;
    if n == 0 {
        return 0;
    }
    (sum / n) as u32
}

/// 计算单通道去直流后的 RMS 原始码值（不含增益缩放）。
///
/// RMS = sqrt(Σ(sample - zero)² / N)
/// 使用 i64 中间类型，避免 12-bit ADC × 12-bit ADC 溢出。
fn compute_rms_code(samples: &[u16], zero: u32) -> u32 {
    let n = samples.len() as u64;
    if n == 0 {
        return 0;
    }

    let sum_sq: u64 = samples
        .iter()
        .map(|&s| {
            let centered = s as i32 - zero as i32;
            let sq = (centered as i64) * (centered as i64);
            sq as u64
        })
        .sum();

    isqrt(sum_sq / n)
}

/// 从 ADC 原始样点计算单相电参数。
///
/// # 参数
///
/// * `v_samples` / `i_samples` / `ilk_samples` — 三通道原始 ADC 码值数组，各 count 个元素。
/// * `count` — 样点数（通常为 128）。
/// * `v_gain_x1000` 等 — 校准增益 × 1000，将 ADC RMS 码值换算为物理量。
///   增益含义：physical_value = rms_code × gain / 1000。
/// * `v_zero` 等 — DC 偏移（0 表示从本批数据自动估算）。
/// * `result` — 输出电参数结构体。
///
/// # 算法
///
/// 1. 对每通道求 DC 偏移（手动指定或自动估算均值）。
/// 2. 去直流后计算 RMS 码值。
/// 3. RMS 码值 × 增益 → 物理量 RMS。
/// 4. 有功功率 = mean( (v[n]-Vdc) × (i[n]-Idc) )，再乘增益。
/// 5. 视在功率 = Vrms × Irms。
/// 6. 功率因数 = P / S，钳位到 [0, 1] 区间。
#[no_mangle]
pub extern "C" fn pm_calc_electrical(
    v_samples: *const u16,
    i_samples: *const u16,
    ilk_samples: *const u16,
    count: u16,
    v_gain_x1000: u32,
    i_gain_x1000: u32,
    ilk_gain_x1000: u32,
    v_zero: u16,
    i_zero: u16,
    ilk_zero: u16,
    result: *mut PmElectricalResult,
) {
    // 入口参数校验
    if v_samples.is_null()
        || i_samples.is_null()
        || ilk_samples.is_null()
        || result.is_null()
        || count == 0
    {
        return;
    }

    let n = count as usize;

    // SAFETY: 调用者保证指针指向至少 count 个 u16 的有效内存。
    // DMA 缓冲区固定为 128 半字，与 APP_ADC_SAMPLES 一致。
    let v_slice = unsafe { core::slice::from_raw_parts(v_samples, n) };
    let i_slice = unsafe { core::slice::from_raw_parts(i_samples, n) };
    let ilk_slice = unsafe { core::slice::from_raw_parts(ilk_samples, n) };

    let r = unsafe { &mut *result };

    // 1. 确定各通道 DC 偏移
    let zv = compute_zero(v_slice, v_zero);
    let zi = compute_zero(i_slice, i_zero);
    let zilk = compute_zero(ilk_slice, ilk_zero);

    // 2. 计算各通道 RMS 码值
    let vrms_code = compute_rms_code(v_slice, zv);
    let irms_code = compute_rms_code(i_slice, zi);
    let ilkrms_code = compute_rms_code(ilk_slice, zilk);

    // 3. RMS 码值 → 物理量 (gain/1000 缩放)
    r.vrms_x100 = vrms_code.saturating_mul(v_gain_x1000) / 1000;
    r.irms_x1000 = irms_code.saturating_mul(i_gain_x1000) / 1000;
    r.ilk_rms_x1000 = ilkrms_code.saturating_mul(ilk_gain_x1000) / 1000;

    // 4. 有功功率：mean(v_centered × i_centered)，再乘组合增益
    //    先算 Σ(v_ac[n] × i_ac[n]) / N（原始码值域），再乘增益转物理量。
    //    保留符号：正=消耗功率，负=回馈功率。
    let active_sum: i64 = v_slice
        .iter()
        .zip(i_slice.iter())
        .map(|(&v, &i)| {
            let v_c = v as i32 - zv as i32;
            let i_c = i as i32 - zi as i32;
            (v_c as i64) * (i_c as i64)
        })
        .sum();

    // 平均瞬时功率（码值域，有符号）
    let active_code: i32 = (active_sum / n as i64) as i32;

    // 有功功率 → 物理量（W × 10）
    // active_code = mean(v_ac × i_ac)，单位 code²。
    // P(W) × 10 = active_code × v_gain_x1000 × i_gain_x1000 / 10^10
    // 验证：1125721 × 1000 × 1000 / 10^10 ≈ 112.6 → 11.26W ✓
    r.active_power_x10 = ((active_code as i64)
        .wrapping_mul(v_gain_x1000 as i64)
        .wrapping_mul(i_gain_x1000 as i64)
        / 10_000_000_000i64) as i32;

    // 5. 视在功率 = Vrms × Irms
    //    vrms_x100 / 100 × irms_x1000 / 1000 = vrms × irms (VA)
    //    apparent_x10 = vrms_x100 × irms_x1000 / 10000
    r.apparent_power_x10 = ((r.vrms_x100 as u64) * (r.irms_x1000 as u64) / 10000) as u32;

    // 6. 功率因数 PF × 1000 = (|P| / S) × 1000
    //    S=0 时无负载，功率因数无意义，返回 0
    r.power_factor_x1000 = if r.apparent_power_x10 > 0 {
        let p_abs = r.active_power_x10.unsigned_abs();
        let pf = p_abs.saturating_mul(1000) / r.apparent_power_x10;
        if pf > 1000 {
            1000
        } else {
            pf as u16
        }
    } else {
        0
    };

    r.reserved = 0;
}
