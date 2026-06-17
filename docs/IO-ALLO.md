# IO 分配

| GPIO | STM32 外设/模式 | 本项目分配 | 连接对象 / miniIO 引脚 | 方向 | 是否实际使用 | 冲突与处理 |
| --- | --- | --- | --- | --- | --- | --- |
| PA0 | GPIO 输入下拉 | KEYUP `MENU_CONFIRM` | WK_UP / DS18B20_DQ | 输入 | 是 | KEYUP 高有效；不接 DS18B20 跳线 |
| PA1 | TIM2_CH2 输入捕获 | 市电频率测量输入 `FR_IN` | miniIO `fr`，JP1-13/14 | 输入 | 是 | 原 NRF_IRQ / REMOTE_IN；不插 NRF，不接红外跳线 |
| PA2 | GPIO 推挽输出 | W25Q64 片选禁用 `FLASH_CS` | 板载 W25Q64 F_CS | 输出 | 是，安全态 | 上电后置高，避免 SPI Flash 被误选中 |
| PA3 | GPIO 推挽输出 | SD 卡片选禁用 `SD_CS` | 板载 SD_CS | 输出 | 是，安全态 | 上电后置高，避免 SD 卡被误选中 |
| PA4 | DAC_OUT1 | 正弦波输出 1 `DAC_CH1` | 外接示波器 / 测试点 | 模拟输出 | 是 | 原 NRF_CE；不插 NRF 模块 |
| PA5 | DAC_OUT2 | 正弦波输出 2 `DAC_CH2` | 外接示波器 / 测试点 | 模拟输出 | 是 | 原 SPI1_SCK；因此 MAX7219 不用硬件 SPI1 |
| PA6 | GPIO / SPI1_MISO | 保留 | SPI1_MISO 共享线 | 输入/保留 | 否 | 与 W25Q64/SD/NRF 共享，不建议另作输出 |
| PA7 | GPIO 推挽输出 | MAX7219 数据 `MAX7219_DIN` | miniIO `DIN`，JP1-7/8 | 输出 | 预留，当前未启用 | 原 SPI1_MOSI；使用软件 SPI，PA2/PA3 保持高 |
| PA8 | TIM1_CH1 PWM 输出 | PWM 测试频率源 `PWM_OUT` | 板载 LED0；可飞线到 PA1 自测 | 输出 | 是 | LED0 会随 PWM 闪烁；演示 50Hz 方波可用 |
| PA9 | USART1_TX | 串口发送 `USART1_TX` | 板载 CH340 RX | 输出 | 是 | 用于通信协议、参数下发、数据显示 |
| PA10 | USART1_RX | 串口接收 `USART1_RX` | 板载 CH340 TX | 输入 | 是 | 用于通信协议、参数下发 |
| PA11 | USB_D- | 保留 | 板载 USB D- | USB/保留 | 否 | 不作普通 GPIO |
| PA12 | USB_D+ | 保留 | 板载 USB D+ | USB/保留 | 否 | 不作普通 GPIO |
| PA13 | SWDIO | 下载调试接口 | ST-LINK / JTAG-SWD | 双向 | 是，调试 | 必须保留 |
| PA14 | SWCLK | 下载调试接口 | ST-LINK / JTAG-SWD | 输入 | 是，调试 | 必须保留 |
| PA15 | GPIO 输入上拉 | KEY1 `MENU_UP` | KEY1 / PS_CLK / JTDI | 输入 | 是 | 关闭 JTAG、保留 SWD；KEY1 低有效 |
| PB0 | GPIO 推挽输出 | LCD_D0 | TFTLCD 数据线 D0 | 输出 | 是 | LCD 占用 |
| PB1 | GPIO 推挽输出 | LCD_D1 | TFTLCD 数据线 D1 | 输出 | 是 | LCD 占用 |
| PB2 | GPIO 推挽输出 | LCD_D2 | TFTLCD 数据线 D2 | 输出 | 是 | 同 BOOT1；仅上电采样，运行期给 LCD 用 |
| PB3 | GPIO 推挽输出 | LCD_D3 | TFTLCD 数据线 D3 | 输出 | 是 | 原 JTDO；需关闭 JTAG、保留 SWD |
| PB4 | GPIO 推挽输出 | LCD_D4 | TFTLCD 数据线 D4 | 输出 | 是 | 原 JTRST；需关闭 JTAG、保留 SWD |
| PB5 | GPIO 推挽输出 | LCD_D5 | TFTLCD 数据线 D5 | 输出 | 是 | LCD 占用 |
| PB6 | GPIO 推挽输出 | LCD_D6 | TFTLCD 数据线 D6 | 输出 | 是 | LCD 占用 |
| PB7 | GPIO 推挽输出 | LCD_D7 | TFTLCD 数据线 D7 | 输出 | 是 | LCD 占用 |
| PB8 | GPIO 推挽输出 | LCD_D8 | TFTLCD 数据线 D8 | 输出 | 是 | LCD 占用 |
| PB9 | GPIO 推挽输出 | LCD_D9 | TFTLCD 数据线 D9 | 输出 | 是 | LCD 占用 |
| PB10 | GPIO 推挽输出 | LCD_D10 | TFTLCD 数据线 D10 | 输出 | 是 | LCD 占用 |
| PB11 | GPIO 推挽输出 | LCD_D11 | TFTLCD 数据线 D11 | 输出 | 是 | LCD 占用 |
| PB12 | GPIO 推挽输出 | LCD_D12 | TFTLCD 数据线 D12 | 输出 | 是 | LCD 占用 |
| PB13 | GPIO 推挽输出 | LCD_D13 | TFTLCD 数据线 D13 | 输出 | 是 | LCD 占用 |
| PB14 | GPIO 推挽输出 | LCD_D14 | TFTLCD 数据线 D14 | 输出 | 是 | LCD 占用 |
| PB15 | GPIO 推挽输出 | LCD_D15 | TFTLCD 数据线 D15 | 输出 | 是 | LCD 占用 |
| PC0 | ADC1_IN10 | 电压采样 `ADC_VL` | miniIO `VL`，JP1-19/20 | 模拟输入 | 是 | 原触摸屏 T_SCK；本方案禁用触摸屏 |
| PC1 | ADC1_IN11 | **禁用** — 被触摸屏 T_PEN 硬件拉高 | — | — | 否 | T_PEN 上拉导致 ADC 读数异常，不可用作模拟输入 |
| PC2 | ADC1_IN12 | 漏电电流采样 `ADC_ILK` | miniIO `iLK`，JP1-15/16 | 模拟输入 | 是 | 原触摸屏 T_MISO；本方案禁用触摸屏 |
| PC3 | ADC1_IN13 | 电流采样 `ADC_IL` | miniIO 飞线至 JP1-17/18 | 模拟输入 | 是 | 原触摸屏 T_MOSI；触摸屏禁用后可安全使用 |
| PC4 | GPIO 推挽输出 | MAX7219 时钟 `MAX7219_CLK` | miniIO `CLK`，JP1-3/4 | 输出 | 预留，当前未启用 | 原 NRF_CS；不插 NRF 模块 |
| PC5 | GPIO 输入上拉 | KEY0 `MENU_DOWN` | KEY0 / PS_DAT | 输入 | 是 | KEY0 低有效；当前不启用 MAX7219 |
| PC6 | GPIO 推挽输出 | LCD_RD | TFTLCD RD | 输出 | 是 | LCD 占用 |
| PC7 | GPIO 推挽输出 | LCD_WR | TFTLCD WR | 输出 | 是 | LCD 占用 |
| PC8 | GPIO 推挽输出 | LCD_RS | TFTLCD RS | 输出 | 是 | LCD 占用 |
| PC9 | GPIO 推挽输出 | LCD_CS | TFTLCD CS | 输出 | 是 | LCD 占用 |
| PC10 | GPIO 推挽输出 | LCD_BL | TFTLCD 背光 BL | 输出 | 是 | LCD 占用 |
| PC11 | GPIO 推挽输出 | 蜂鸣器控制 `BEEP` | miniIO `beep`，JP1-9/10 | 输出 | 预留，当前未启用 | 原 24C02 SDA；本项目不使用 EEPROM |
| PC12 | GPIO 推挽输出 | 继电器控制 `RELAY` | miniIO `relay`，JP1-11/12 | 输出 | 预留，当前未启用 | 原 24C02 SCL；高电平继电器动作 |
| PC13 | GPIO 推挽输出 / 输入上拉 | 触摸片选禁用 `TP_CS_DISABLE` | TFTLCD 触摸 CS | 输出/保留 | 是，安全态 | 置高或保持上拉，禁用触摸控制器 |
| PC14 | RTC_OSC32_IN | 禁用 | 32.768kHz 晶振 | 晶振 | 否 | 不可作 GPIO |
| PC15 | RTC_OSC32_OUT | 禁用 | 32.768kHz 晶振 | 晶振 | 否 | 不可作 GPIO |
| PD0 | OSC_IN | 禁用 | HSE 晶振 | 晶振 | 否 | 不可作 GPIO |
| PD1 | OSC_OUT | 禁用 | HSE 晶振 | 晶振 | 否 | 不可作 GPIO |
| PD2 | GPIO 推挽输出 | 运行状态灯 `LED_RUN` | 板载 LED1 | 输出 | 可选 | 可用于心跳灯、错误状态指示 |
