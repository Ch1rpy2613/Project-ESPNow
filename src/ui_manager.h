#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "config.h"
#include <TFT_eSPI.h>
#include <set>    // For std::set
#include <string> // For String (used in std::set<String> macSet)
#include <vector> // For std::vector (used in allDrawingHistory)
#include "esp_now_handler.h" // For TouchData_t, macSet, allDrawingHistory, relativeBootTimeOffset, replayAllDrawings

// --- Extern Global Variables ---
// These are defined in Project-ESPNow.ino or other modules and used here.
extern TFT_eSPI tft; // Defined in Project-ESPNow.ino

// UI state variables that will be defined in ui_manager.cpp
extern uint32_t currentColor;      // 当前画笔颜色
extern bool inCustomColorMode;     // 是否处于自定义颜色模式
extern int redValue;               // 红色通道值 (0-255)
extern int greenValue;             // 绿色通道值 (0-255)
extern int blueValue;              // 蓝色通道值 (0-255)
extern uint16_t *savedScreenBuffer; // 用于保存调色界面覆盖区域的屏幕缓冲

// Variables from other modules needed by UI functions
extern std::set<String> macSet; // 来自 esp_now_handler.h (用于设备计数)
extern std::vector<TouchData_t> allDrawingHistory; // 来自 esp_now_handler.h (用于调试信息)
extern long relativeBootTimeOffset; // 来自 esp_now_handler.h (用于调试信息)
// isScreenOn (如果 drawDebugInfo 需要) 会通过包含 power_manager.h 在 ui_manager.cpp 中获得

// --- 函数声明 ---

// 初始化函数
void uiManagerInit(); // UI 相关初始化占位符 (TFT 初始化之外)

// 主界面绘制函数
void drawMainInterface();
void drawResetButton();
void drawColorButtons();
void drawSleepButton(); // 显示来自 macSet 的设备数量
void drawCustomColorButton(); // 显示当前颜色
void drawStarButton(); // 显示当前颜色, 自定义颜色入口的占位符

// 调试信息函数
void drawDebugInfo(); // 显示历史记录大小、运行时间、偏移量、内存

// 按钮按下检测函数 (基于坐标)
bool isResetButtonPressed(int x, int y);
bool isColorButtonPressed(int x, int y, uint32_t &selectedColor); // 输出选中的颜色
bool isSleepButtonPressed(int x, int y);
bool isCustomColorButtonPressed(int x, int y); // 用于进入自定义颜色模式
bool isBackButtonPressed(int x, int y);    // 用于退出自定义颜色模式

// 自定义颜色选择器 UI 函数
void handleCustomColorTouch(int x, int y); // 处理颜色选择器内的触摸
void updateSingleColorSlider(int yPos, uint32_t sliderColor, int &channelValue);
void drawColorSelectors();    // 绘制 RGB 滑块和返回按钮
void updateCustomColorPreview(); // 更新颜色预览框
void refreshAllColorSliders();   // 重绘所有滑块 (例如触摸后)
void closeColorSelectors();      // 恢复屏幕，退出自定义颜色模式

// 屏幕缓冲区管理 (用于自定义颜色选择器)
void saveScreenArea();    // 保存颜色选择器将覆盖的屏幕区域
void restoreSavedScreenArea(); // 恢复保存的屏幕区域

// UI 工具函数
void updateCurrentColor(uint32_t newColor); // 设置全局当前颜色
void updateConnectedDevicesCount(); // 更新休眠按钮上的设备计数
void clearScreenAndCache(); // 清屏、重绘UI、重置相关触摸点 (影响广泛)
void hideStarButton();
void showStarButton();
void redrawStarButton();

// 可能需要从其他模块获取的函数
extern float readBatteryVoltagePercentage(); // drawResetButton 使用，已移至 power_manager

#endif // UI_MANAGER_H
