#ifndef CONFIG_H
#define CONFIG_H

// 触摸屏引脚定义
#define XPT2046_IRQ 36     // XPT2046 中断引脚
#define XPT2046_MOSI 32    // XPT2046 SPI MOSI 引脚
#define XPT2046_MISO 39    // XPT2046 SPI MISO 引脚
#define XPT2046_CLK 25     // XPT2046 SPI 时钟引脚
#define XPT2046_CS 33      // XPT2046 SPI 片选引脚

// IO0 按钮引脚定义
#define BUTTON_IO0 0       // GPIO 0, 通常是 Boot 按钮

// LED 及背光引脚定义
#define TFT_BL 21          // GPIO 21 用于控制 TFT 背光
#define GREEN_LED 16       // GPIO 16 用于控制绿色 LED
#define BLUE_LED 17        // GPIO 17 用于控制蓝色 LED
#define RED_LED 22         // GPIO 22 用于控制红色 LED

// 电池检测引脚定义
#define BATTERY_PIN 34     // GPIO 34, 用于读取电池电压

// 触摸校准参数
#define TOUCH_MIN_X 200    // 触摸 X 轴最小值
#define TOUCH_MAX_X 3700   // 触摸 X 轴最大值
#define TOUCH_MIN_Y 300    // 触摸 Y 轴最小值
#define TOUCH_MAX_Y 3800   // 触摸 Y 轴最大值

// 重置按钮位置和大小
#define RESET_BUTTON_X 4   // 重置按钮 X 坐标
#define RESET_BUTTON_Y 4   // 重置按钮 Y 坐标 (调整为 4)
#define RESET_BUTTON_W 28  // 重置按钮宽度 (收窄)
#define RESET_BUTTON_H 10  // 重置按钮高度

// 颜色按钮位置和大小 (收窄并靠左)
#define COLOR_BUTTON_WIDTH 15                                      // 颜色按钮宽度 (收窄)
#define COLOR_BUTTON_HEIGHT 10                                     // 颜色按钮高度
#define COLOR_BUTTON_START_Y (RESET_BUTTON_Y + RESET_BUTTON_H + 2) // 颜色按钮起始 Y 坐标 (更紧凑)
#define COLOR_BUTTON_SPACING 2                                     // 颜色按钮间距

// P (休眠/电源) 按钮位置和大小
#define SLEEP_BUTTON_X RESET_BUTTON_X                                                    // 休眠按钮 X 坐标 (与重置按钮对齐)
#define SLEEP_BUTTON_Y (COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * 4) // 休眠按钮 Y 坐标 (在颜色按钮下方)
#define SLEEP_BUTTON_W 10                                                                        // 休眠按钮宽度
#define SLEEP_BUTTON_H 10                                                                        // 休眠按钮高度

// 自定义颜色按钮 ("*") 位置和大小
#define CUSTOM_COLOR_BUTTON_X (SCREEN_WIDTH - COLOR_BUTTON_WIDTH - 4) // 自定义颜色按钮 X 坐标 (屏幕右侧)
#define CUSTOM_COLOR_BUTTON_Y 4                                       // 自定义颜色按钮 Y 坐标
#define CUSTOM_COLOR_BUTTON_W COLOR_BUTTON_WIDTH                      // 自定义颜色按钮宽度 (与普通颜色按钮相同)
#define CUSTOM_COLOR_BUTTON_H COLOR_BUTTON_HEIGHT                     // 自定义颜色按钮高度 (与普通颜色按钮相同)

// 返回按钮 (调色界面中使用) 位置和大小
#define BACK_BUTTON_X (SCREEN_WIDTH - COLOR_BUTTON_WIDTH - 4)     // 返回按钮 X 坐标 (屏幕右侧)
#define BACK_BUTTON_Y (SCREEN_HEIGHT - COLOR_BUTTON_HEIGHT - 4) // 返回按钮 Y 坐标 (屏幕右下角)
#define BACK_BUTTON_W COLOR_BUTTON_WIDTH                          // 返回按钮宽度
#define BACK_BUTTON_H COLOR_BUTTON_HEIGHT                         // 返回按钮高度

// 屏幕宽高定义
#define SCREEN_WIDTH 320  // 屏幕宽度 (像素)
#define SCREEN_HEIGHT 240 // 屏幕高度 (像素)

// 手动定义 TFT_GRAY (灰色)
// 标准 TFT_eSPI 库通常包含此颜色，但为确保可用性在此处定义
// 格式为 RGB565, 计算方式: ( (R & 0xF8) << 8 ) | ( (G & 0xFC) << 3 ) | ( B >> 3 )
// 对于灰色 (128, 128, 128): R=128 (0x80), G=128 (0x80), B=128 (0x80)
// (0x80 & 0xF8) << 8  => 0x8000
// (0x80 & 0xFC) << 3  => 0x0400 (0x80 >> 2 << 5)
// (0x80 >> 3)         => 0x0010
// 0x8000 | 0x0400 | 0x0010 = 0x8410
#define TFT_GRAY 0x8410

// ESP-NOW 通信相关常量
#define BROADCAST_INTERVAL 2000             // MAC 地址发现广播间隔 (毫秒)
#define UPTIME_INFO_BROADCAST_INTERVAL 5000 // Uptime 信息广播间隔 (毫秒)

// UI 更新相关常量
#define DEBUG_INFO_UPDATE_INTERVAL 200      // 调试信息更新间隔 (毫秒)

// 调色界面相关常量
#define COLOR_SLIDER_WIDTH 20                               // 颜色滑块宽度
#define COLOR_SLIDER_HEIGHT ((SCREEN_HEIGHT - 10) / 3)      // 单个颜色滑块高度

// LED 调光相关常量 (息屏状态下)
#define BLUE_LED_DIM_DUTY_CYCLE 14 // 蓝色 LED 息屏时亮度 (PWM duty cycle, 0-255), 约 5% (14/255)
#define RED_LED_DIM_DUTY_CYCLE 4   // 红色 LED 息屏时亮度 (PWM duty cycle, 0-255), 约 1.5% (4/255)

// 触摸笔划间隔阈值 (毫秒)
// 小于此间隔的触摸/绘制事件被视为连续笔划的一部分
#define TOUCH_STROKE_INTERVAL 50

// ESP-NOW 同步逻辑相关常量
#define MIN_UPTIME_DIFF_FOR_NEW_SYNC_TARGET 200UL // 选择新的同步目标时，对端设备最小原始运行时间差异 (毫秒) - 用于迟滞判断
#define EFFECTIVE_UPTIME_SYNC_THRESHOLD 1000UL    // 有效运行时间同步阈值 (毫秒) - 在此阈值内的差异不触发新的同步以避免抖动

#endif // CONFIG_H
