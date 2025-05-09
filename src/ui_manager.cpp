#include "ui_manager.h"
#include "config.h"
#include <Arduino.h> // For Serial, millis, ESP, sprintf, etc.
#include <cstring>   // For memset if used (though not directly apparent here) // 如果使用 memset (此处不明显直接使用)

// --- 全局 UI 状态变量 (在此定义) ---
uint32_t currentColor = TFT_BLUE; // Default to blue, consistent with .ino // 默认为蓝色, 与 .ino 文件一致
bool inCustomColorMode = false;
int redValue = 255;
int greenValue = 255;
int blueValue = 255;
uint16_t *savedScreenBuffer = nullptr;

// --- 来自其他模块/主 .ino 文件的 Extern 变量 ---
extern TFT_eSPI tft; // 定义于 Project-ESPNow.ino
extern bool isScreenOn; // 来自 power_manager 模块 (通过 ui_manager.h 间接包含 power_manager.h)
// lastLocalPoint 和 lastLocalTouchTime 是 touch_handler 模块的内部状态, 不应在此 extern 或修改

// macSet, allDrawingHistory, relativeBootTimeOffset 已在 ui_manager.h 中 extern 声明
// replayAllDrawings() 已在 esp_now_handler.h 中声明
// lastRemotePoint, lastRemoteDrawTime 已在 esp_now_handler.h 中 extern 声明

// --- 函数实现 ---

void uiManagerInit() {
    // 占位符
}

void drawMainInterface() {
    tft.fillScreen(TFT_BLACK);
    drawResetButton();
    drawColorButtons();
    drawSleepButton(); // 此函数内部会调用 updateConnectedDevicesCount
    // drawCustomColorButton(); // 这只是颜色框, drawStarButton 包含 "*"
    drawStarButton();    // 绘制颜色框和 "*"
    if (isScreenOn && !inCustomColorMode) { // 确保初始不在自定义颜色模式时绘制
        drawDebugInfo();
    }
}

void drawResetButton() {
    tft.fillRect(RESET_BUTTON_X, RESET_BUTTON_Y, RESET_BUTTON_W, RESET_BUTTON_H, TFT_RED);
    float batteryPercentage = readBatteryVoltagePercentage(); 
    tft.setTextColor(TFT_WHITE, TFT_RED); // 设置文本背景为按钮颜色
    tft.setTextDatum(MC_DATUM); // 居中对齐
    tft.drawString(String(batteryPercentage, 0) + "%", 
                   RESET_BUTTON_X + RESET_BUTTON_W / 2, 
                   RESET_BUTTON_Y + RESET_BUTTON_H / 2, 
                   1); // 使用1号字体以显示较小文本
    tft.setTextDatum(TL_DATUM); // 重置对齐方式
}

void drawColorButtons() {
    uint32_t colors[] = {TFT_BLUE, TFT_GREEN, TFT_RED, TFT_YELLOW};
    for (int i = 0; i < 4; i++) {
        int buttonY = COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * i;
        tft.fillRect(RESET_BUTTON_X, buttonY, COLOR_BUTTON_WIDTH, COLOR_BUTTON_HEIGHT, colors[i]);
    }
}

void drawSleepButton() { 
    tft.fillRect(SLEEP_BUTTON_X, SLEEP_BUTTON_Y, SLEEP_BUTTON_W, SLEEP_BUTTON_H, TFT_BLUE);
    updateConnectedDevicesCount(); 
}

void drawCustomColorButton() { // 星星按钮的颜色预览部分
    tft.fillRect(CUSTOM_COLOR_BUTTON_X, CUSTOM_COLOR_BUTTON_Y, CUSTOM_COLOR_BUTTON_W, CUSTOM_COLOR_BUTTON_H, currentColor);
}

void drawStarButton() { 
    drawCustomColorButton(); // 绘制颜色部分
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("*", CUSTOM_COLOR_BUTTON_X + CUSTOM_COLOR_BUTTON_W/2, CUSTOM_COLOR_BUTTON_Y + CUSTOM_COLOR_BUTTON_H/2, 2); // 2号字体
    tft.setTextDatum(TL_DATUM);
}


void drawDebugInfo() {
    if (!isScreenOn || inCustomColorMode) return;

    int startX = 2;
    int startY = SCREEN_HEIGHT - 42;
    int lineHeight = 10;
    uint16_t bgColor = TFT_GRAY;
    uint16_t textColor = TFT_WHITE;
    int rectHeight = 4 * lineHeight + 2;

    tft.fillRect(startX, startY, 120, rectHeight, bgColor);
    tft.setTextColor(textColor, bgColor);
    tft.setTextSize(1);
    tft.setTextFont(1); // 默认字体

    char buffer[50];

    sprintf(buffer, "Hist: %u", allDrawingHistory.size()); 
    tft.setCursor(startX + 2, startY + 2);
    tft.print(buffer);

    sprintf(buffer, "Uptime: %lu", millis());
    tft.setCursor(startX + 2, startY + 2 + lineHeight);
    tft.print(buffer);

    sprintf(buffer, "Comp: %ld", relativeBootTimeOffset); 
    tft.setCursor(startX + 2, startY + 2 + 2 * lineHeight);
    tft.print(buffer);

    sprintf(buffer, "Mem: %u/%uKB", ESP.getFreeHeap() / 1024, ESP.getHeapSize() / 1024);
    tft.setCursor(startX + 2, startY + 2 + 3 * lineHeight);
    tft.print(buffer);
}

bool isResetButtonPressed(int x, int y) {
    return x >= RESET_BUTTON_X && x <= RESET_BUTTON_X + RESET_BUTTON_W &&
           y >= RESET_BUTTON_Y && y <= RESET_BUTTON_Y + RESET_BUTTON_H;
}

bool isColorButtonPressed(int x, int y, uint32_t &selectedColor) {
    uint32_t colors[] = {TFT_BLUE, TFT_GREEN, TFT_RED, TFT_YELLOW};
    for (int i = 0; i < 4; i++) {
        int buttonY = COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * i;
        if (x >= RESET_BUTTON_X && x <= RESET_BUTTON_X + COLOR_BUTTON_WIDTH &&
            y >= buttonY && y <= buttonY + COLOR_BUTTON_HEIGHT) {
            selectedColor = colors[i];
            return true;
        }
    }
    return false;
}

bool isSleepButtonPressed(int x, int y) {
    return x >= SLEEP_BUTTON_X && x <= SLEEP_BUTTON_X + SLEEP_BUTTON_W &&
           y >= SLEEP_BUTTON_Y && y <= SLEEP_BUTTON_Y + SLEEP_BUTTON_H;
}

bool isCustomColorButtonPressed(int x, int y) { 
    return x >= CUSTOM_COLOR_BUTTON_X && x <= CUSTOM_COLOR_BUTTON_X + CUSTOM_COLOR_BUTTON_W &&
           y >= CUSTOM_COLOR_BUTTON_Y && y <= CUSTOM_COLOR_BUTTON_Y + CUSTOM_COLOR_BUTTON_H;
}

bool isBackButtonPressed(int x, int y) { 
    return x >= BACK_BUTTON_X && x <= BACK_BUTTON_X + BACK_BUTTON_W &&
           y >= BACK_BUTTON_Y && y <= BACK_BUTTON_Y + BACK_BUTTON_H;
}

void handleCustomColorTouch(int x, int y) {
    if (x >= (SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4) && x <= (SCREEN_WIDTH - 4)) {
        if (y >= 0 && y < COLOR_SLIDER_HEIGHT) {
            updateSingleColorSlider(y, TFT_RED, redValue);
        } else if (y >= COLOR_SLIDER_HEIGHT && y < 2 * COLOR_SLIDER_HEIGHT) {
            updateSingleColorSlider(y - COLOR_SLIDER_HEIGHT, TFT_GREEN, greenValue);
        } else if (y >= 2 * COLOR_SLIDER_HEIGHT && y < 3 * COLOR_SLIDER_HEIGHT) {
            updateSingleColorSlider(y - (2 * COLOR_SLIDER_HEIGHT), TFT_BLUE, blueValue);
        } else if (isBackButtonPressed(x, y)) { // 在此X坐标范围内检查返回按钮
             closeColorSelectors();
             return; 
        }
        refreshAllColorSliders();
        updateCustomColorPreview();
        updateCurrentColor(tft.color565(redValue, greenValue, blueValue));
    }
}

void updateSingleColorSlider(int yPos, uint32_t sliderColor, int &channelValue) {
    channelValue = constrain(map(yPos, 0, COLOR_SLIDER_HEIGHT - 1, 255, 0), 0, 255); // 映射使顶部为255
    // 绘制由 refreshAllColorSliders 处理
}

void drawColorSelectors() { 
    // 保存滑块将覆盖的区域。
    // 原始的 saveScreenArea 保存的是滑块区域本身，这不是我们想要恢复的。
    // 我们想要保存的是滑块将绘制位置下方的区域（如果它是主画布的一部分）。
    // 然而，滑块位于边缘。如果主画布没有延伸到那里，这没问题。
    // 原始代码的 saveScreenArea/restoreSavedScreenArea 是针对滑块列的。
    // 并且 closeColorSelectors 之后会进行全屏清除和重绘。
    // 因此，saveScreenArea 可能用于快速更新滑块UI本身，而无需重绘其他所有内容。
    // 目前，假设如果需要，调用者在此之前已调用 saveScreenArea()。
    // 此函数仅绘制选择器。

    refreshAllColorSliders(); 

    tft.fillRect(BACK_BUTTON_X, BACK_BUTTON_Y, BACK_BUTTON_W, BACK_BUTTON_H, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("B", BACK_BUTTON_X + BACK_BUTTON_W/2, BACK_BUTTON_Y + BACK_BUTTON_H/2, 2); // "B" 使用2号字体
    tft.setTextDatum(TL_DATUM);
}

void updateCustomColorPreview() {
    uint32_t previewColor = tft.color565(redValue, greenValue, blueValue);
    int previewY = 2 * COLOR_SLIDER_HEIGHT + COLOR_SLIDER_HEIGHT; // 预览框的起始Y坐标 (蓝色滑块下方)
    int previewBoxHeight = SCREEN_HEIGHT - previewY - (BACK_BUTTON_H + 4); // 预览框高度
    if (previewBoxHeight < 10) previewBoxHeight = COLOR_SLIDER_HEIGHT; // 如果太小则使用默认高度

    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, previewY, COLOR_SLIDER_WIDTH, previewBoxHeight, previewColor);
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, previewY, COLOR_SLIDER_WIDTH, previewBoxHeight, TFT_WHITE);
}

void refreshAllColorSliders() {
    // 滑块填充方向：原始代码从y=0向上填充到 value*(height/255.0)
    // 这意味着0在滑块顶部，255在底部。
    // 我们把它改得更直观：0在底部，255在顶部。
    // 因此，从 (滑块起始Y + 滑块高度 - 填充条高度) 开始填充，填充高度为 bar_height。

    float barHeightRed = redValue * COLOR_SLIDER_HEIGHT / 255.0;
    float barHeightGreen = greenValue * COLOR_SLIDER_HEIGHT / 255.0;
    float barHeightBlue = blueValue * COLOR_SLIDER_HEIGHT / 255.0;

    // 红色滑块
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_BLACK); // 清除旧的填充
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_RED);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT - barHeightRed, COLOR_SLIDER_WIDTH, barHeightRed, TFT_RED);

    // 绿色滑块
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_BLACK); // 清除旧的填充
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_GREEN);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT - barHeightGreen, COLOR_SLIDER_WIDTH, barHeightGreen, TFT_GREEN);

    // 蓝色滑块
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_BLACK); // 清除旧的填充
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_BLUE);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 3 * COLOR_SLIDER_HEIGHT - barHeightBlue, COLOR_SLIDER_WIDTH, barHeightBlue, TFT_BLUE);

    updateCustomColorPreview();
}

void closeColorSelectors() {
    inCustomColorMode = false;
    // currentColor = tft.color565(redValue, greenValue, blueValue); // 已由 handleCustomColorTouch 设置
    
    // 原始顺序:
    // 1. 将滑块区域填充为黑色 (通过恢复或完全重绘完成)
    // 2. 更新 currentColor (已完成)
    // 3. 绘制 drawCustomColorButton (星星按钮)
    // 4. restoreSavedScreenArea (恢复滑块下方的区域)
    // 5. fillScreen(TFT_BLACK)
    // 6. drawMainInterface()
    // 7. replayAllDrawings()
    // 8. showStarButton() / drawStarButton()

    // 简化：如果该小区域是主画布的一部分，则完全重绘使得恢复它变得不那么重要。
    // 如果保存/恢复是为了避免完全重播以提高性能，则顺序很重要。
    // 鉴于原始代码的完全 fillScreen，保存/恢复似乎是针对滑块列本身的。

    // 在完全重绘之前专门清除滑块区域，或依赖 fillScreen。
    // tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH + 4, SCREEN_HEIGHT, TFT_BLACK);
    
    if (savedScreenBuffer != nullptr) { // 如果我们确实保存了某些内容 (例如滑块下方的画布)
        restoreSavedScreenArea(); // 恢复它
    } else {
        // 如果没有保存任何内容，或者保存/恢复是针对滑块列本身的，
        // 那么无论如何都会进行 fillScreen。
    }

    tft.fillScreen(TFT_BLACK); // 清除整个屏幕
    drawMainInterface();       // 重绘静态UI元素
    replayAllDrawings();       // 重播所有绘图历史 (来自 esp_now_handler)
    // drawStarButton(); // drawMainInterface 应该调用此函数。
}

void updateCurrentColor(uint32_t newColor) {
    currentColor = newColor;
}

void updateConnectedDevicesCount() {
    char deviceCountBuffer[10];
    sprintf(deviceCountBuffer, "%d", macSet.size()); // macSet 来自 esp_now_handler

    // 通过首先绘制背景颜色来清除先前的文本
    tft.fillRect(SLEEP_BUTTON_X, SLEEP_BUTTON_Y, SLEEP_BUTTON_W, SLEEP_BUTTON_H, TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE); // 文本颜色，背景颜色
    tft.setTextDatum(MC_DATUM);
    // 2号字体对于10x10的按钮可能太大。原始代码使用2号字体。
    tft.drawString(String(deviceCountBuffer), 
                   SLEEP_BUTTON_X + SLEEP_BUTTON_W / 2, 
                   SLEEP_BUTTON_Y + SLEEP_BUTTON_H / 2, 
                   1); // 使用1号字体以获得更好的适应性
    tft.setTextDatum(TL_DATUM); // 重置对齐方式
}

void saveScreenArea() { // 保存通常绘制颜色滑块的区域
    if (savedScreenBuffer == nullptr) {
        // 计算高度：3个滑块 + 预览 + 返回按钮，或者只是滑块列。
        // 原始保存：COLOR_SLIDER_WIDTH * (4 * COLOR_SLIDER_HEIGHT)
        // 这覆盖了3个滑块和同样高度的预览框。
        // 目前我们坚持使用该尺寸。
        int bufferHeight = 4 * COLOR_SLIDER_HEIGHT;
        if (bufferHeight > SCREEN_HEIGHT) bufferHeight = SCREEN_HEIGHT;
        savedScreenBuffer = new uint16_t[COLOR_SLIDER_WIDTH * bufferHeight];
    }
    int readHeight = 4 * COLOR_SLIDER_HEIGHT;
    if (readHeight > SCREEN_HEIGHT) readHeight = SCREEN_HEIGHT;
    tft.readRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, readHeight, savedScreenBuffer);
}

void restoreSavedScreenArea() {
    if (savedScreenBuffer != nullptr) {
        int pushHeight = 4 * COLOR_SLIDER_HEIGHT;
        if (pushHeight > SCREEN_HEIGHT) pushHeight = SCREEN_HEIGHT;
        tft.pushImage(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, pushHeight, savedScreenBuffer);
        delete[] savedScreenBuffer;
        savedScreenBuffer = nullptr;
    }
}

void clearScreenAndCache() { // 这是一个主要重置操作
    tft.fillScreen(TFT_BLACK);
    drawMainInterface();
    
    // 本地触摸点 (lastLocalPoint, lastLocalTouchTime) 的重置由 touch_handler.cpp 内部管理。
    // 远程触摸点 (lastRemotePoint, lastRemoteDrawTime) 的重置由 esp_now_handler.cpp 内部管理，
    // 通常通过 MSG_TYPE_RESET_CANVAS 消息同步。
    // ui_manager 的 clearScreenAndCache 主要负责清屏和重绘UI。
    // 如果需要显式触发其他模块的状态重置，应通过调用其公共接口函数。
}

void hideStarButton() { // 隐藏星星/自定义颜色按钮区域
    tft.fillRect(CUSTOM_COLOR_BUTTON_X, CUSTOM_COLOR_BUTTON_Y, CUSTOM_COLOR_BUTTON_W, CUSTOM_COLOR_BUTTON_H, TFT_BLACK);
}

void showStarButton() { // 重绘星星按钮
    drawStarButton();
}

void redrawStarButton() { // 便于先隐藏后显示
    hideStarButton();
    drawStarButton();
}

// 如果 readBatteryVoltagePercentage 是 ui_manager 的一部分，则在此定义
// 然而，它更像是一个系统工具或电源管理功能。
// 目前，它是 extern 声明的，假设它在 Project-ESPNow.ino 或 power_manager 中。
// float readBatteryVoltagePercentage() { /* ... 实现 ... */ }
