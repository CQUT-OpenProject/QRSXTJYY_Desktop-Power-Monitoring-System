# 桌面用电监控系统

## 项目结构

- `core/`：启动后的 C 入口、中断文件、`SystemInit`。
- `bsp/`：板级支持代码，包括延时、系统兼容头文件等。
- `middleware/alientek_lcd/`：ALIENTEK TFT LCD 驱动。
- `app/`：应用层模块，包含显示、ADC、DAC、PWM、输入捕获、协议等占位模块。
- `rust_algos/`：`no_std` Rust 静态库，供 C 主工程链接。
- `linker/`：STM32F103RCT6 的 Flash/RAM 链接脚本。
- `startup/`：启动汇编文件。
- `tools/openocd/`：OpenOCD 烧录配置。

## 环境要求

确保以下工具可用：

- CMake、`arm-none-eabi-gcc`
- Rust Toolchain、cbindgen
- OpenOCD、Python 3（可选）

Rust 嵌入式目标只需安装一次：

```sh
rustup target add thumbv7m-none-eabi
```

如果要使用串口烧录，需要安装 `stm32loader`：

```sh
python3 -m pip install --user pyserial stm32loader
```

安装 `cbindgen`：

```sh
cargo install --force cbindgen
```

## 构建与烧录

配置并构建 Debug 版本：

```sh
cmake --preset Debug
cmake --build --preset Debug -j4
```

生成的 ELF 文件位于：

```text
build/Debug/desktop_power_monitor.elf
```

如需串口烧录使用的 BIN 文件，执行：

```sh
arm-none-eabi-objcopy -O binary \
  build/Debug/desktop_power_monitor.elf \
  build/Debug/desktop_power_monitor.bin
```

## 使用 ST-Link 烧录

连接 ST-Link 后执行：

```sh
openocd -f tools/openocd/stm32f103rct6.cfg \
  -c "program build/Debug/desktop_power_monitor.elf verify reset exit"
```

### 使用串口烧录

查看当前串口设备：

```sh
python3 -m serial.tools.list_ports -v
```

烧录命令示例：

```sh
/Users/uednd/Library/Python/3.9/bin/stm32loader \
  -p /dev/tty.usbserial-110 \
  -b 115200 \
  -f F1 \
  -e -w -v \
  -g 0x08000000 \
  build/Debug/desktop_power_monitor.bin
```

## Rust/C 接口与 cbindgen

本工程使用 C + Rust 混合编写。Rust 主要用于编写算法，位于 `rust_algos/`，通过 `staticlib` 形式编译为静态库，再由 CMake 链接进 STM32 固件。

Rust 暴露给 C 调用的函数需要满足以下约定：

- 使用 `pub extern "C" fn` 固定为 C ABI。
- 使用 `#[no_mangle]` 保持符号名不被 Rust 编译器改写。
- FFI 边界优先使用 `u32`、`i32`、指针等 C 侧明确可表达的类型。
- 导出结构体或枚举需要使用 `#[repr(C)]` 固定内存布局。

生成规则位于：

```text
rust_algos/cbindgen.toml
```

正常执行 CMake 构建时，如果系统中存在 `cbindgen`，会自动刷新头文件：

```sh
cmake --build --preset Debug -j4
```

也可以手动生成：

```sh
cd rust_algos
cbindgen --config cbindgen.toml --crate rust_algos --output include/rust_algos.h
```

检查当前头文件是否与 Rust 源码一致：

```sh
cd rust_algos
cbindgen --config cbindgen.toml --crate rust_algos --output include/rust_algos.h --verify
```

新增 Rust 导出接口时，推荐流程是：

1. 在 `rust_algos/src/lib.rs` 中添加 `#[no_mangle] pub extern "C" fn ...`。
2. 为该函数添加 Rust 文档注释，`cbindgen` 会把注释同步到 C 头文件。
3. 运行 CMake 构建或手动运行 `cbindgen` 生成 `rust_algos.h`。
4. 在 C 代码中包含 `rust_algos.h` 并调用新接口。