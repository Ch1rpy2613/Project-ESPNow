#include "ui_manager.h"
#include "config.h"
#include <Arduino.h> // For Serial, millis, ESP, sprintf, etc.
#include <cstring>   // For memset if used (though not directly apparent here) // 如果使用 memset (此处不明显直接使用)
#include <TFT_eSPI.h> // 包含 TFT_eSPI 库
#include <cmath>      // 包含 cmath 库，用于 round 函数
#include <algorithm>  // 包含 algorithm 库，用于 min/max 函数
#include "drawing_history.h" // 包含自定义绘图历史头文件

// --- 全局 UI 状态变量 (在此定义) ---
uint32_t currentColor = TFT_BLUE; // Default to blue, consistent with .ino // 默认为蓝色, 与 .ino 文件一致
bool inCustomColorMode = false;
bool isDebugInfoVisible = false;   // 调试信息框默认关闭
bool showDebugToggleButton = true; // 调试信息切换按钮默认显示
bool isProjectInfoPopupVisible = false; // 项目信息弹窗默认关闭
bool isCoffeePopupVisible = false;    // "Coffee" 弹窗默认关闭

// 进度条状态变量定义
int sendProgressTotal = 0;
int sendProgressCurrent = 0;
int receiveProgressTotal = 0;
int receiveProgressCurrent = 0;
bool showSendProgress = false;
bool showReceiveProgress = false;

int redValue = 255;
int greenValue = 255;
int blueValue = 255;
uint16_t *savedScreenBuffer = nullptr;

// --- 来自其他模块/主 .ino 文件的 Extern 变量 ---
extern TFT_eSPI tft;    // 定义于 Project-ESPNow.ino
extern bool isScreenOn; // 来自 power_manager 模块 (通过 ui_manager.h 间接包含 power_manager.h)
// lastLocalPoint 和 lastLocalTouchTime 是 touch_handler 模块的内部状态, 不应在此 extern 或修改

// macSet, allDrawingHistory, relativeBootTimeOffset 已在 ui_manager.h 中 extern 声明
// replayAllDrawings() 已在 esp_now_handler.h 中声明
// lastRemotePoint, lastRemoteDrawTime 已在 esp_now_handler.h 中 extern 声明

// --- 函数实现 ---

void uiManagerInit()
{
    // 占位符
}

void drawMainInterface()
{
    tft.fillScreen(TFT_BLACK);
    drawResetButton();
    drawColorButtons();
    drawSleepButton(); // 此函数内部会调用 updateConnectedDevicesCount
    // drawCustomColorButton(); // 这只是颜色框, drawStarButton 包含 "*"
    drawStarButton(); // 绘制颜色框和 "*"
    if (isScreenOn && !inCustomColorMode)
    {
        if (isDebugInfoVisible)
        {
            drawDebugInfo();
            if (!isProjectInfoPopupVisible && !isCoffeePopupVisible) { // 仅在弹窗未显示时绘制按钮
                drawInfoButton();
            }
        }
        if (showDebugToggleButton) // 并且 Coffee 弹窗未显示
        {
            if (!isCoffeePopupVisible) drawDebugToggleButton();
            drawCoffeeButton(); // Coffee 按钮总是尝试绘制，但其内部会检查 isCoffeePopupVisible
        }
        if (showSendProgress)
        {
            drawSendProgressIndicator();
        }
        if (showReceiveProgress)
        {
            drawReceiveProgressIndicator();
        }
    }
}

void redrawMainScreen()
{
    tft.fillScreen(TFT_BLACK);
    drawMainInterface(); // 这会根据 isDebugInfoVisible 和 showDebugToggleButton 绘制正确的状态
    if (isProjectInfoPopupVisible) { // 如果项目信息弹窗之前是可见的，重绘它
        showProjectInfoPopup();
    } else if (isCoffeePopupVisible) { // 如果 Coffee 弹窗之前是可见的，重绘它
        showCoffeePopup();
    } else {
        replayAllDrawings(); // 否则重绘历史笔迹
    }
}

void drawResetButton()
{
    tft.fillRect(RESET_BUTTON_X, RESET_BUTTON_Y, RESET_BUTTON_W, RESET_BUTTON_H, TFT_RED);
    float batteryPercentage = readBatteryVoltagePercentage();
    tft.setTextColor(TFT_WHITE, TFT_RED); // 设置文本背景为按钮颜色
    tft.setTextDatum(MC_DATUM);           // 居中对齐
    tft.drawString(String(batteryPercentage, 0) + "%",
                   RESET_BUTTON_X + RESET_BUTTON_W / 2,
                   RESET_BUTTON_Y + RESET_BUTTON_H / 2,
                   1);          // 使用1号字体以显示较小文本
    tft.setTextDatum(TL_DATUM); // 重置对齐方式
}

void drawColorButtons()
{
    uint32_t colors[] = {TFT_BLUE, TFT_GREEN, TFT_RED, TFT_YELLOW};
    for (int i = 0; i < 4; i++)
    {
        int buttonY = COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * i;
        tft.fillRect(RESET_BUTTON_X, buttonY, COLOR_BUTTON_WIDTH, COLOR_BUTTON_HEIGHT, colors[i]);
    }
}

void drawSleepButton()
{
    tft.fillRect(SLEEP_BUTTON_X, SLEEP_BUTTON_Y, SLEEP_BUTTON_W, SLEEP_BUTTON_H, TFT_BLUE);
    updateConnectedDevicesCount();
}

void drawCustomColorButton()
{ // 星星按钮的颜色预览部分
    tft.fillRect(CUSTOM_COLOR_BUTTON_X, CUSTOM_COLOR_BUTTON_Y, CUSTOM_COLOR_BUTTON_W, CUSTOM_COLOR_BUTTON_H, currentColor);
}

void drawStarButton()
{
    drawCustomColorButton(); // 绘制颜色部分
    tft.setTextColor(TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("*", CUSTOM_COLOR_BUTTON_X + CUSTOM_COLOR_BUTTON_W / 2, CUSTOM_COLOR_BUTTON_Y + CUSTOM_COLOR_BUTTON_H / 2, 2); // 2号字体
    tft.setTextDatum(TL_DATUM);
}

void drawDebugInfo()
{
    if (!isScreenOn || inCustomColorMode || !isDebugInfoVisible)
        return;

    int startX = 2;
    int startY = SCREEN_HEIGHT - 42;
    int lineHeight = 10;
    uint16_t bgColor = TFT_GRAY;
    uint16_t textColor = TFT_WHITE;
    int rectHeight = 4 * lineHeight + 2;

    // 如果弹窗可见，则不绘制调试信息背景，避免覆盖弹窗
    if (!isProjectInfoPopupVisible) {
        tft.fillRect(startX, startY, 120, rectHeight, bgColor);
    }

    tft.setTextColor(textColor, bgColor); // 背景色用于文本背景，使其在弹窗上也可见
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

bool isResetButtonPressed(int x, int y)
{
    return x >= RESET_BUTTON_X && x <= RESET_BUTTON_X + RESET_BUTTON_W &&
           y >= RESET_BUTTON_Y && y <= RESET_BUTTON_Y + RESET_BUTTON_H;
}

bool isColorButtonPressed(int x, int y, uint32_t &selectedColor)
{
    uint32_t colors[] = {TFT_BLUE, TFT_GREEN, TFT_RED, TFT_YELLOW};
    for (int i = 0; i < 4; i++)
    {
        int buttonY = COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * i;
        if (x >= RESET_BUTTON_X && x <= RESET_BUTTON_X + COLOR_BUTTON_WIDTH &&
            y >= buttonY && y <= buttonY + COLOR_BUTTON_HEIGHT)
        {
            selectedColor = colors[i];
            return true;
        }
    }
    return false;
}

bool isSleepButtonPressed(int x, int y)
{
    return x >= SLEEP_BUTTON_X && x <= SLEEP_BUTTON_X + SLEEP_BUTTON_W &&
           y >= SLEEP_BUTTON_Y && y <= SLEEP_BUTTON_Y + SLEEP_BUTTON_H;
}

bool isCustomColorButtonPressed(int x, int y)
{
    return x >= CUSTOM_COLOR_BUTTON_X && x <= CUSTOM_COLOR_BUTTON_X + CUSTOM_COLOR_BUTTON_W &&
           y >= CUSTOM_COLOR_BUTTON_Y && y <= CUSTOM_COLOR_BUTTON_Y + CUSTOM_COLOR_BUTTON_H;
}

bool isBackButtonPressed(int x, int y)
{
    return x >= BACK_BUTTON_X && x <= BACK_BUTTON_X + BACK_BUTTON_W &&
           y >= BACK_BUTTON_Y && y <= BACK_BUTTON_Y + BACK_BUTTON_H;
}

bool isDebugToggleButtonPressed(int x, int y)
{
    // 假设按钮在左下角，尺寸为 DEBUG_TOGGLE_BUTTON_SIZE
    return x >= DEBUG_TOGGLE_BUTTON_X && x <= DEBUG_TOGGLE_BUTTON_X + DEBUG_TOGGLE_BUTTON_W &&
           y >= DEBUG_TOGGLE_BUTTON_Y && y <= DEBUG_TOGGLE_BUTTON_Y + DEBUG_TOGGLE_BUTTON_H;
}

void drawDebugToggleButton()
{
    if (!showDebugToggleButton || inCustomColorMode)
        return;

    tft.fillRect(DEBUG_TOGGLE_BUTTON_X, DEBUG_TOGGLE_BUTTON_Y, DEBUG_TOGGLE_BUTTON_W, DEBUG_TOGGLE_BUTTON_H, TFT_DARKCYAN);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("D", DEBUG_TOGGLE_BUTTON_X + DEBUG_TOGGLE_BUTTON_W / 2, DEBUG_TOGGLE_BUTTON_Y + DEBUG_TOGGLE_BUTTON_H / 2, 2); // 字体大小2
    tft.setTextDatum(TL_DATUM);
}

void toggleDebugInfo()
{
    isDebugInfoVisible = !isDebugInfoVisible;
    showDebugToggleButton = !isDebugInfoVisible;
    if (isProjectInfoPopupVisible && !isDebugInfoVisible) { // 如果关闭调试信息时弹窗是开的，也关掉弹窗
        hideProjectInfoPopup(); // 这会触发 redrawMainScreen
    } else {
        redrawMainScreen();
    }
}

void handleCustomColorTouch(int x, int y)
{
    if (x >= (SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4) && x <= (SCREEN_WIDTH - 4))
    {
        if (y >= 0 && y < COLOR_SLIDER_HEIGHT)
        {
            updateSingleColorSlider(y, TFT_RED, redValue);
        }
        else if (y >= COLOR_SLIDER_HEIGHT && y < 2 * COLOR_SLIDER_HEIGHT)
        {
            updateSingleColorSlider(y - COLOR_SLIDER_HEIGHT, TFT_GREEN, greenValue);
        }
        else if (y >= 2 * COLOR_SLIDER_HEIGHT && y < 3 * COLOR_SLIDER_HEIGHT)
        {
            updateSingleColorSlider(y - (2 * COLOR_SLIDER_HEIGHT), TFT_BLUE, blueValue);
        }
        else if (isBackButtonPressed(x, y))
        {
            closeColorSelectors();
            return;
        }
        refreshAllColorSliders();
        updateCustomColorPreview();
        updateCurrentColor(tft.color565(redValue, greenValue, blueValue));
    }
}

void updateSingleColorSlider(int yPos, uint32_t sliderColor, int &channelValue)
{
    channelValue = constrain(map(yPos, 0, COLOR_SLIDER_HEIGHT - 1, 255, 0), 0, 255);
    // 绘制由 refreshAllColorSliders 处理
}

void drawColorSelectors()
{
    refreshAllColorSliders();

    tft.fillRect(BACK_BUTTON_X, BACK_BUTTON_Y, BACK_BUTTON_W, BACK_BUTTON_H, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("B", BACK_BUTTON_X + BACK_BUTTON_W / 2, BACK_BUTTON_Y + BACK_BUTTON_H / 2, 2);
    tft.setTextDatum(TL_DATUM);
}

void updateCustomColorPreview()
{
    uint32_t previewColor = tft.color565(redValue, greenValue, blueValue);
    int previewY = 2 * COLOR_SLIDER_HEIGHT + COLOR_SLIDER_HEIGHT;
    int previewBoxHeight = SCREEN_HEIGHT - previewY - (BACK_BUTTON_H + 4);
    if (previewBoxHeight < 10)
        previewBoxHeight = COLOR_SLIDER_HEIGHT;

    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, previewY, COLOR_SLIDER_WIDTH, previewBoxHeight, previewColor);
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, previewY, COLOR_SLIDER_WIDTH, previewBoxHeight, TFT_WHITE);
}

void refreshAllColorSliders()
{
    float barHeightRed = redValue * COLOR_SLIDER_HEIGHT / 255.0;
    float barHeightGreen = greenValue * COLOR_SLIDER_HEIGHT / 255.0;
    float barHeightBlue = blueValue * COLOR_SLIDER_HEIGHT / 255.0;

    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_BLACK);
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_RED);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT - barHeightRed, COLOR_SLIDER_WIDTH, barHeightRed, TFT_RED);

    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_BLACK);
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_GREEN);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT - barHeightGreen, COLOR_SLIDER_WIDTH, barHeightGreen, TFT_GREEN);

    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_BLACK);
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_BLUE);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 3 * COLOR_SLIDER_HEIGHT - barHeightBlue, COLOR_SLIDER_WIDTH, barHeightBlue, TFT_BLUE);

    updateCustomColorPreview();
}

void closeColorSelectors()
{
    inCustomColorMode = false;

    if (savedScreenBuffer != nullptr)
    {
        restoreSavedScreenArea();
    }

    tft.fillScreen(TFT_BLACK);
    drawMainInterface();
    replayAllDrawings();
}

void updateCurrentColor(uint32_t newColor)
{
    currentColor = newColor;
}

void updateConnectedDevicesCount()
{
    char deviceCountBuffer[10];
    sprintf(deviceCountBuffer, "%d", macSet.size());

    tft.fillRect(SLEEP_BUTTON_X, SLEEP_BUTTON_Y, SLEEP_BUTTON_W, SLEEP_BUTTON_H, TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String(deviceCountBuffer),
                   SLEEP_BUTTON_X + SLEEP_BUTTON_W / 2,
                   SLEEP_BUTTON_Y + SLEEP_BUTTON_H / 2,
                   1);
    tft.setTextDatum(TL_DATUM);
}

void saveScreenArea()
{
    if (savedScreenBuffer == nullptr)
    {
        int bufferHeight = 4 * COLOR_SLIDER_HEIGHT;
        if (bufferHeight > SCREEN_HEIGHT)
            bufferHeight = SCREEN_HEIGHT;
        savedScreenBuffer = new uint16_t[COLOR_SLIDER_WIDTH * bufferHeight];
    }
    int readHeight = 4 * COLOR_SLIDER_HEIGHT;
    if (readHeight > SCREEN_HEIGHT)
        readHeight = SCREEN_HEIGHT;
    tft.readRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, readHeight, savedScreenBuffer);
}

void restoreSavedScreenArea()
{
    if (savedScreenBuffer != nullptr)
    {
        int pushHeight = 4 * COLOR_SLIDER_HEIGHT;
        if (pushHeight > SCREEN_HEIGHT)
            pushHeight = SCREEN_HEIGHT;
        tft.pushImage(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, pushHeight, savedScreenBuffer);
        delete[] savedScreenBuffer;
        savedScreenBuffer = nullptr;
    }
}

void clearScreenAndCache()
{
    allDrawingHistory.clear();
    tft.fillScreen(TFT_BLACK);
    drawMainInterface();
}

void hideStarButton()
{
    tft.fillRect(CUSTOM_COLOR_BUTTON_X, CUSTOM_COLOR_BUTTON_Y, CUSTOM_COLOR_BUTTON_W, CUSTOM_COLOR_BUTTON_H, TFT_BLACK);
}

void showStarButton()
{
    drawStarButton();
}

void redrawStarButton()
{
    hideStarButton();
    drawStarButton();
}

// --- 进度条函数实现 ---
void drawArcSlice(int x, int y, int r, int thickness, int start_angle, int end_angle, uint16_t color)
{
    if (start_angle == end_angle)
        return;

    start_angle = start_angle % 360;
    end_angle = end_angle % 360;
    if (end_angle == 0 && start_angle < 359)
        end_angle = 360;

    int angle_offset = -90;
    uint32_t sa = (start_angle + angle_offset + 360) % 360;
    uint32_t ea = (end_angle + angle_offset + 360) % 360;

    tft.drawArc(x, y, r, r - thickness, sa, ea, color, color, false);
}

void drawSendProgressIndicator()
{
    if (!showSendProgress || inCustomColorMode)
        return;

    tft.drawCircle(SEND_PROGRESS_X, SEND_PROGRESS_Y, PROGRESS_CIRCLE_RADIUS, PROGRESS_BG_COLOR);
    tft.drawCircle(SEND_PROGRESS_X, SEND_PROGRESS_Y, PROGRESS_CIRCLE_RADIUS - PROGRESS_CIRCLE_THICKNESS, PROGRESS_BG_COLOR);

    if (sendProgressTotal > 0 && sendProgressCurrent > 0)
    {
        int progress_angle = (sendProgressCurrent * 360) / sendProgressTotal;
        if (progress_angle > 360)
            progress_angle = 360;
        if (progress_angle < 0)
            progress_angle = 0;

        for (int i = 0; i < PROGRESS_CIRCLE_THICKNESS; ++i)
        {
            tft.drawCircle(SEND_PROGRESS_X, SEND_PROGRESS_Y, PROGRESS_CIRCLE_RADIUS - i, PROGRESS_BG_COLOR);
        }
        drawArcSlice(SEND_PROGRESS_X, SEND_PROGRESS_Y, PROGRESS_CIRCLE_RADIUS, PROGRESS_CIRCLE_THICKNESS, 0, progress_angle, PROGRESS_SEND_COLOR);
    }
}

void drawReceiveProgressIndicator()
{
    if (!showReceiveProgress || inCustomColorMode)
        return;

    if (receiveProgressTotal > 0 && receiveProgressCurrent > 0)
    {
        int progress_angle = (receiveProgressCurrent * 360) / receiveProgressTotal;
        if (progress_angle > 360)
            progress_angle = 360;
        if (progress_angle < 0)
            progress_angle = 0;

        for (int i = 0; i < PROGRESS_CIRCLE_THICKNESS; ++i)
        {
            tft.drawCircle(RECEIVE_PROGRESS_X, RECEIVE_PROGRESS_Y, PROGRESS_CIRCLE_RADIUS - i, PROGRESS_BG_COLOR);
        }
        drawArcSlice(RECEIVE_PROGRESS_X, RECEIVE_PROGRESS_Y, PROGRESS_CIRCLE_RADIUS, PROGRESS_CIRCLE_THICKNESS, 0, progress_angle, PROGRESS_RECEIVE_COLOR);
    }
}

void updateSendProgress(int current, int total)
{
    if (total <= 0)
    {
        hideSendProgress();
        return;
    }
    sendProgressCurrent = current;
    sendProgressTotal = total;
    showSendProgress = true;

    if (!inCustomColorMode)
    {
        drawSendProgressIndicator();
    }

    if (current >= total)
    {
        hideSendProgress();
    }
}

void updateReceiveProgress(int current, int total)
{
    if (total <= 0)
    {
        hideReceiveProgress();
        return;
    }
    receiveProgressCurrent = current;
    receiveProgressTotal = total;
    showReceiveProgress = true;

    if (!inCustomColorMode)
    {
        drawReceiveProgressIndicator();
    }

    // 移除: if (current >= total) { hideReceiveProgress(); }
    // 隐藏操作将由 esp_now_handler 在接收完成后显式调用
}

void hideSendProgress()
{
    if (showSendProgress)
    {
        showSendProgress = false;
        tft.fillRect(SEND_PROGRESS_X - PROGRESS_CIRCLE_RADIUS - 1, SEND_PROGRESS_Y - PROGRESS_CIRCLE_RADIUS - 1,
                     2 * PROGRESS_CIRCLE_RADIUS + 2, 2 * PROGRESS_CIRCLE_RADIUS + 2, TFT_BLACK);
    }
}

void hideReceiveProgress()
{
    if (showReceiveProgress)
    {
        showReceiveProgress = false;
        tft.fillRect(RECEIVE_PROGRESS_X - PROGRESS_CIRCLE_RADIUS - 1, RECEIVE_PROGRESS_Y - PROGRESS_CIRCLE_RADIUS - 1,
                     2 * PROGRESS_CIRCLE_RADIUS + 2, 2 * PROGRESS_CIRCLE_RADIUS + 2, TFT_BLACK);
    }
}

// --- "Coffee" 按钮和弹窗函数 ---
void drawCoffeeButton() {
    // 即使弹窗可见，按钮本身也绘制，只是点击行为可能不同或被忽略
    // 或者，如果希望在弹窗时隐藏此按钮，可以添加: if (isCoffeePopupVisible) return;
    if (!isScreenOn || inCustomColorMode || !showDebugToggleButton) return; // 如果D按钮不显示，C按钮也不显示

    tft.fillRect(COFFEE_BUTTON_X, COFFEE_BUTTON_Y, COFFEE_BUTTON_W, COFFEE_BUTTON_H, TFT_ORANGE); // 使用橙色
    tft.setTextColor(TFT_BLACK, TFT_ORANGE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("C", COFFEE_BUTTON_X + COFFEE_BUTTON_W / 2, COFFEE_BUTTON_Y + COFFEE_BUTTON_H / 2, 2); // 字体大小2
    tft.setTextDatum(TL_DATUM);
}

bool isCoffeeButtonPressed(int x, int y) {
    if (!showDebugToggleButton) return false; // 如果D按钮不显示，C按钮也无效
    return x >= COFFEE_BUTTON_X && x <= COFFEE_BUTTON_X + COFFEE_BUTTON_W &&
           y >= COFFEE_BUTTON_Y && y <= COFFEE_BUTTON_Y + COFFEE_BUTTON_H;
}

void showCoffeePopup() {
    if (!isScreenOn || inCustomColorMode) return;

    isCoffeePopupVisible = true;
    // isDebugInfoVisible = false; // 打开C弹窗时，可以考虑隐藏D的调试信息区域
    // showDebugToggleButton = false; // 同时隐藏D按钮

    // 弹窗区域和颜色
    int popupX = 10;
    int popupY = 10;
    int popupW = SCREEN_WIDTH - 2 * popupX;
    int popupH = SCREEN_HEIGHT - 2 * popupY;
    uint16_t popupBgColor = tft.color565(70, 70, 70); // 深灰色
    uint16_t popupBorderColor = TFT_WHITE;
    uint16_t textColor = TFT_WHITE;
    uint16_t qrPixelColor = TFT_WHITE;
    uint16_t qrBgColor = popupBgColor; // 二维码背景与弹窗背景一致

    tft.fillRect(popupX, popupY, popupW, popupH, popupBgColor);
    tft.drawRect(popupX, popupY, popupW, popupH, popupBorderColor);

    tft.setTextColor(textColor, popupBgColor);
    tft.setTextDatum(TC_DATUM);
    tft.setTextSize(1);
    tft.drawString("Coffee for Shapaper!", popupX + popupW / 2, popupY + 5, 2); // 标题

    tft.setTextDatum(TL_DATUM);
    int textX = popupX + 5;
    int textY = popupY + 25;
    int lineHeight = 9; // 英文小字体行高，稍微调小一点以容纳更多内容

    const char* line1 = "This project was largely refactored by Shapaper,";
    const char* line2 = "consuming much time and effort.";
    const char* line3 = "Welcome to buy me a coffee!";
    // const char* line4 = "a coffee!"; // 合并到上一行

    tft.setCursor(textX, textY);
    tft.print(line1);
    textY += lineHeight;
    tft.setCursor(textX, textY);
    tft.print(line2);
    textY += lineHeight;
    tft.setCursor(textX, textY);
    tft.print(line3);
    // textY += lineHeight;
    // tft.setCursor(textX, textY);
    // tft.print(line4);

    textY += lineHeight * 1.5; // 文本和二维码之间的间距

    // 支付宝二维码数据
    const char *qr_data_alipay =
        "EEEEEEE.E....EEEE.E...EEEEEEE\n"
        "E.....E.E..EEEE..E.E..E.....E\n"
        "E.EEE.E.EE..E.E..E.EE.E.EEE.E\n"
        "E.EEE.E.EEE.EEEEE.EE..E.EEE.E\n"
        "E.EEE.E..EE.E..EEEE.E.E.EEE.E\n"
        "E.....E.EEE...E.EEEE..E.....E\n"
        "EEEEEEE.E.E.E.E.E.E.E.EEEEEEE\n"
        "..........EE.....EE.E........\n"
        "EE..EEE...E.E...EE..E..E.EEEE\n"
        "EEEE.E.E.....E.E.E....EEEEEEE\n"
        "E..EE.E.EEE....EEE..E.......E\n"
        "....EE..EE..E.EE.E.EEEEE.E.EE\n"
        "EE....E.E..E..E.EEE.EE.....E.\n"
        ".E..EE.EEEE.EE.E......EEEEEEE\n"
        ".E..E.EE.EEEE..E.E..EE.EEEE.E\n"
        "..E..E.E.EEE..EE.EEE..E.E..EE\n"
        "EEEEEEEE..E.E....EEEEE.....E.\n"
        "EEEEEE.E..E..EEEE....EE.EE.EE\n"
        "..E..EEEEEE..E.E.....EEEE.E.E\n"
        "...E......E.E....E.EEEEE...EE\n"
        "EEE.EEEE...EE...EEE.EEEEEE..E\n"
        "........E.E.EEEE.E.EE...E...E\n"
        "EEEEEEE....EEE.EEE.EE.E.EEE.E\n"
        "E.....E.EEEEE..E.EE.E...E...E\n"
        "E.EEE.E.E.E.....EEEEEEEEEE.EE\n"
        "E.EEE.E..E.EE..EE...EE.E....E\n"
        "E.EEE.E..E.EEE.EE.E.EE...EEEE\n"
        "E.....E.EE.EE..EEE.EEEE..E.EE\n"
        "EEEEEEE.EEE.E..EEEE.EE..E..E.";

    // 微信二维码数据
    const char *qr_data_wechat =
        "EEEEEEE.EE.EE..E...E..EEEEEEE\n"
        "E.....E.E.E..EE...EE..E.....E\n"
        "E.EEE.E.EE.E...EEEEE..E.EEE.E\n"
        "E.EEE.E.E..EE.E.....E.E.EEE.E\n"
        "E.EEE.E...E..E.E...E..E.EEE.E\n"
        "E.....E.E.E.....EEEE..E.....E\n"
        "EEEEEEE.E.E.E.E.E.E.E.EEEEEEE\n"
        ".........E.E..EEEEEEE........\n"
        "EE..EEE...EE.E..E...E..E.EEEE\n"
        "E.E.EE..E.EE.EEEEEEE...EEE.EE\n"
        ".EEEEEEE.EEEE.EEE...EE.EE.EEE\n"
        "EEEEE...EE.EE..E.E.E..E.E..EE\n"
        "...EE.E....EEE.....EEE..EE...\n"
        "EE...E.EEE....E.E....E.EE..EE\n"
        "E...EEEEE..EEEEE....E.EEEEE.E\n"
        "...EE.....E.E...EE.E.E.....EE\n"
        ".E...EEEEEE.E..E.E.E.E.E.E..E\n"
        "EEE..........EE.E..E....E..EE\n"
        "..EEEEEEEE.EE.EE..E..E.E.EE.E\n"
        "..E.EE..E.E.E.EE.EEEEE.E.E...\n"
        "EE...EE.EEE........EEEEEEE.E.\n"
        "........E....EEEEEE.E...E.EEE\n"
        "EEEEEEE....EE..EE.E.E.E.E.E.E\n"
        "E.....E.E..EE.E.EE.EE...E...E\n"
        "E.EEE.E.E.....EE...EEEEEE..E.\n"
        "E.EEE.E...E..E..E...EE.E.EEEE\n"
        "E.EEE.E..E.E...E.EE.E.EE..EEE\n"
        "E.....E.E..E..E..E.EEE.EEE.EE\n"
        "EEEEEEE.EEEE..E.E...E.EEEE.E.";

    int qrPixelSize = 2; // 每个二维码“像素”的大小
    int qrWidthInPixels = 29 * qrPixelSize; // 二维码的屏幕宽度
    int qrHeightInPixels = 29 * qrPixelSize; // 二维码的屏幕高度
    int spacingBetweenQRs = 10; // 两个二维码之间的间距
    int totalQRWidth = 2 * qrWidthInPixels + spacingBetweenQRs;

    int qrCommonY = textY + lineHeight; // 二维码的共同起始Y坐标 (在标签下方)

    // 绘制支付宝二维码
    int qrAlipayOffsetX = popupX + (popupW - totalQRWidth) / 2;
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Alipay", qrAlipayOffsetX + qrWidthInPixels / 2, textY, 1); // 标签字体大小1

    char lineBuffer[35];
    const char *p_alipay = qr_data_alipay;
    int currentY_alipay = qrCommonY;

    while (*p_alipay) {
        int i = 0;
        while (*p_alipay && *p_alipay != '\n' && i < 30) {
            lineBuffer[i++] = *p_alipay++;
        }
        lineBuffer[i] = '\0';
        if (*p_alipay == '\n') p_alipay++;

        for (int j = 0; j < i; ++j) {
            if (lineBuffer[j] == 'E') {
                tft.fillRect(qrAlipayOffsetX + j * qrPixelSize, currentY_alipay, qrPixelSize, qrPixelSize, qrPixelColor);
            }
        }
        currentY_alipay += qrPixelSize;
        if (currentY_alipay > popupY + popupH - 5 - qrPixelSize) break;
    }

    // 绘制微信二维码
    int qrWechatOffsetX = qrAlipayOffsetX + qrWidthInPixels + spacingBetweenQRs;
    tft.drawString("WeChat", qrWechatOffsetX + qrWidthInPixels / 2, textY, 1); // 标签字体大小1

    const char *p_wechat = qr_data_wechat;
    int currentY_wechat = qrCommonY;

    while (*p_wechat) {
        int i = 0;
        while (*p_wechat && *p_wechat != '\n' && i < 30) {
            lineBuffer[i++] = *p_wechat++;
        }
        lineBuffer[i] = '\0';
        if (*p_wechat == '\n') p_wechat++;

        for (int j = 0; j < i; ++j) {
            if (lineBuffer[j] == 'E') {
                tft.fillRect(qrWechatOffsetX + j * qrPixelSize, currentY_wechat, qrPixelSize, qrPixelSize, qrPixelColor);
            }
        }
        currentY_wechat += qrPixelSize;
        if (currentY_wechat > popupY + popupH - 5 - qrPixelSize) break;
    }

    // --- 新增 Shapaper's Blog 二维码 ---
    textY = currentY_alipay > currentY_wechat ? currentY_alipay : currentY_wechat; // 取两个二维码中较低的Y值作为基准
    textY += lineHeight * 0.5; // 在二维码下方留出一些间距

    tft.setTextDatum(TC_DATUM);
    tft.drawString("Shapaper's Blog(https://blog.dimeta.top/)", popupX + popupW / 2, textY, 1); // 博客标题
    textY += lineHeight;                                                                  // 在二维码下方留出一些间距
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Have many interesting things,Scan to visit", popupX + 5, textY, 1); // 博客二维码标签
    textY += lineHeight; // 为二维码留出空间

    const char *qr_data_blog =
        "EEEEEEE..E....E.E.EEEEEEE\n"
        "E.....E.EE.EE.EEE.E.....E\n"
        "E.EEE.E..EEE...EE.E.EEE.E\n"
        "E.EEE.E.EE.E.EEE..E.EEE.E\n"
        "E.EEE.E...E.E...E.E.EEE.E\n"
        "E.....E.E.E....EE.E.....E\n"
        "EEEEEEE.E.E.E.E.E.E.EEEEEEE\n"
        "............E.EEE........\n"
        "EEEEE.EEEEE.EE...E.E.E.E.\n"
        "EE.EEE.E.E...E..E..E...E.\n"
        "EE....E..E.EEEEEEE..EE.EE\n"
        "....E..EEEEE...E..E.....E\n"
        "..E..EE..E.E.EE..EE.E.EEE\n"
        "E.EE.E....E.E.E.E..E.E.E.\n"
        "E.EE.EEE......EE.E.EEE.EE\n"
        "E..EEE..E...E..EEE.EE...E\n"
        "E...E.E.EE..EEEEEEEEE.E..\n"
        "........EE...E.EE...EE...\n"
        "EEEEEEE.E.EEEEE.E.E.E.EEE\n"
        "E.....E....E..E.E...EE..E\n"
        "E.EEE.E.E.EEEEEEEEEEE.E..\n"
        "E.EEE.E.EEE.E.EEEEE.EEEEE\n"
        "E.EEE.E.EEE.....E....EE.E\n"
        "E.....E.E.EE...EEEEEEE..E\n"
        "EEEEEEE.E..EEE.E.E.EEEEEE";

    int qrBlogOffsetX = popupX + (popupW - qrWidthInPixels) / 2; // 单个二维码居中
    int currentY_blog = textY;
    const char *p_blog = qr_data_blog;

    for (int row = 0; row < 25 && *p_blog; ++row) { // 假设博客二维码也是25行 (根据txt内容调整)
        int i = 0;
        while (*p_blog && *p_blog != '\n' && i < 30) { // 假设最大宽度30
            lineBuffer[i++] = *p_blog++;
        }
        lineBuffer[i] = '\0';
        if (*p_blog == '\n') p_blog++;

        for (int j = 0; j < i; ++j) {
            if (lineBuffer[j] == 'E') {
                tft.fillRect(qrBlogOffsetX + j * qrPixelSize, currentY_blog, qrPixelSize, qrPixelSize, qrPixelColor);
            }
        }
        currentY_blog += qrPixelSize;
        if (currentY_blog > popupY + popupH - 5 - qrPixelSize - lineHeight) break; // 避免覆盖关闭提示
    }
    // --- 结束新增 Shapaper's Blog 二维码 ---

    tft.setTextDatum(BC_DATUM);
    tft.drawString("(Tap to close)", popupX + popupW / 2, popupY + popupH - 5, 1);
    tft.setTextDatum(TL_DATUM);
}

void hideCoffeePopup() {
    if (isCoffeePopupVisible) {
        isCoffeePopupVisible = false;
        // showDebugToggleButton = true; // 恢复D按钮的显示（如果之前隐藏了）
        redrawMainScreen();
    }
}


// --- 新增的项目信息按钮和弹窗函数 ---
void drawInfoButton() {
    if (!isScreenOn || inCustomColorMode || !isDebugInfoVisible || isProjectInfoPopupVisible || isCoffeePopupVisible) return; // 如果Coffee弹窗也显示，则不绘制

    tft.fillRect(INFO_BUTTON_X, INFO_BUTTON_Y, INFO_BUTTON_W, INFO_BUTTON_H, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("*", INFO_BUTTON_X + INFO_BUTTON_W / 2, INFO_BUTTON_Y + INFO_BUTTON_H / 2, 2); // 字体大小2
    tft.setTextDatum(TL_DATUM);
}

bool isInfoButtonPressed(int x, int y) {
    if (!isDebugInfoVisible || isCoffeePopupVisible) return false; // 仅当调试信息可见且Coffee弹窗关闭时按钮才有效
    return x >= INFO_BUTTON_X && x <= INFO_BUTTON_X + INFO_BUTTON_W &&
           y >= INFO_BUTTON_Y && y <= INFO_BUTTON_Y + INFO_BUTTON_H;
}

void showProjectInfoPopup() {
    if (!isScreenOn || inCustomColorMode || isCoffeePopupVisible) return; // 如果Coffee弹窗显示，则不显示此弹窗

    isProjectInfoPopupVisible = true;

    // 弹窗区域和颜色 - 增大弹窗
    int popupX = 10; // 减小边距，使弹窗更大
    int popupY = 10; // 减小边距
    int popupW = SCREEN_WIDTH - 2 * popupX;
    int popupH = SCREEN_HEIGHT - 2 * popupY; // 占据更多高度
    uint16_t popupBgColor = tft.color565(40, 40, 80); // 深蓝紫色
    uint16_t popupBorderColor = TFT_LIGHTGREY;
    uint16_t textColor = TFT_WHITE;
    uint16_t qrPixelColor = TFT_WHITE; // 二维码像素颜色

    tft.fillRect(popupX, popupY, popupW, popupH, popupBgColor);
    tft.drawRect(popupX, popupY, popupW, popupH, popupBorderColor);

    tft.setTextColor(textColor, popupBgColor);
    tft.setTextDatum(TC_DATUM); // 顶部居中对齐
    tft.setTextSize(1); // 使用小号字体
    tft.drawString("Project-ESPNow Info", popupX + popupW / 2, popupY + 5, 2); // 标题使用2号字体

    tft.setTextDatum(TL_DATUM); // 左上角对齐
    int textX = popupX + 5;
    int textY = popupY + 25;
    int lineHeight = 10; // 减小行高以容纳更多内容

    tft.setCursor(textX, textY);
    tft.print("Github: github.com/");
    textY += lineHeight;
    tft.setCursor(textX + 10, textY); // 缩进
    tft.print("Kur1oR3iko/Project-ESPNow");

    textY += lineHeight * 1.5;
    tft.setCursor(textX, textY);
    tft.print("Maintainers:");
    textY += lineHeight;
    tft.setCursor(textX + 10, textY);
    tft.print("- Kur1oR3iko");
    textY += lineHeight;
    tft.setCursor(textX + 10, textY);
    tft.print("- xiao_hj909");
    textY += lineHeight;
    tft.setCursor(textX + 10, textY);
    tft.print("- Shapaper223");
    textY += lineHeight;
    tft.setCursor(textX + 10, textY);
    tft.print("  (shapaper@126.com)");

    textY += lineHeight * 1.5;
    tft.setCursor(textX, textY);
    tft.print("Kur1oR3iko's Bilibili Space:"); // 新增内容
    textY += lineHeight;

    // 二维码数据 (原作者空间)
    const char *qr_data_author =
        "EEEEEEE.E..EE.EEE.E.E.EEEEEEE\n"
        "E.....E..EE.....EE.E..E.....E\n"
        "E.EEE.E...E.E..EE.EE..E.EEE.E\n"
        "E.EEE.E.....E..E.E.EE.E.EEE.E\n"
        "E.EEE.E..E.E..E..EEE..E.EEE.E\n"
        "E.....E...E.E...E.E...E.....E\n"
        "EEEEEEE.E.E.E.E.E.E.E.EEEEEEE\n"
        "........E.EE.E.EE..E.........\n"
        "EE.EE.E..EEE.E.E.E....E.....E\n"
        "EEEE...E.E..EE.EEEEE...EE.EE.\n"
        "...EEEE...EE.E.E..........E..\n"
        ".EE....EEEEEEE..EE...EEEEE..E\n"
        "E..E..EE.E.EEE......EEEE....E\n"
        "EEE.E........EEE.EE...EEEEEEE\n"
        ".EE.E.EEEEEEEE.EE..E...EE.E.E\n"
        "EEEEEE.EE.EE....E..EE...E.E.E\n"
        "E.E.EEE.E.E...E.E.E.E..E.E...\n"
        "E.E..E.EEEEEEEEEE.EEE...E.EE.\n"
        "EE.E.EE...EE...E...E.EEEEE..E\n"
        "EEE..E..EE..E.E....EE.E.EEE..\n"
        "EEE.EEEEE.E.EEEE.EE.EEEEEEEE.\n"
        "........EE....EEE...E...EE...\n"
        "EEEEEEE...E.E.EEE.EEE.E.EE...\n"
        "E.....E..E....E.EE.EE...E...E\n"
        "E.EEE.E.E.EEEEEEEEEEE.E..E.E.\n"
        "E.EEE.E.EEE.E.EEEEE.EEEEE.E.E\n"
        "E.EEE.E.EEE.....E....EE.E.E.E\n"
        "E.....E.E.EE...EEEEEEE..E.E.E\n"
        "EEEEEEE.E..EEE.E.E.EEEEEE.E.E";

    int qrPixelSize = 2; // 每个二维码“像素”的大小
    int qrWidthInChars = 29; // 二维码的字符宽度
    int qrHeightInChars = 29; // 二维码的字符高度
    int qrDisplayWidth = qrWidthInChars * qrPixelSize;
    // int qrDisplayHeight = qrHeightInChars * qrPixelSize; // 未使用

    // 将二维码绘制在文本下方，居中显示
    int qrOffsetX = popupX + (popupW - qrDisplayWidth) / 2;
    int qrOffsetY = textY + lineHeight / 2; // 在文本下方留出一些空间

    char lineBuffer[35];
    const char *p_author = qr_data_author;
    int currentY_qr = qrOffsetY;

    for (int row = 0; row < qrHeightInChars && *p_author; ++row) {
        int i = 0;
        while (*p_author && *p_author != '\n' && i < qrWidthInChars) {
            lineBuffer[i++] = *p_author++;
        }
        lineBuffer[i] = '\0';
        if (*p_author == '\n') p_author++;

        for (int col = 0; col < i; ++col) {
            if (lineBuffer[col] == 'E') {
                tft.fillRect(qrOffsetX + col * qrPixelSize, currentY_qr, qrPixelSize, qrPixelSize, qrPixelColor);
            }
        }
        currentY_qr += qrPixelSize;
        if (currentY_qr > popupY + popupH - 5 - qrPixelSize - lineHeight) break; // 避免覆盖关闭提示
    }

    textY = currentY_qr + lineHeight / 2; // 更新textY到二维码下方

    // Shapaper的贡献信息移到二维码下方
    tft.setCursor(textX, textY);
    tft.print("Shapaper did a lot of");
    textY += lineHeight;
    tft.setCursor(textX, textY);
    tft.print("refactoring.");

    tft.setTextDatum(BC_DATUM); // 底部居中
    tft.drawString("(Tap to close)", popupX + popupW / 2, popupY + popupH - 5, 1); // 提示关闭
    tft.setTextDatum(TL_DATUM); // 重置
}

void hideProjectInfoPopup() {
    if (isProjectInfoPopupVisible) {
        isProjectInfoPopupVisible = false;
        redrawMainScreen(); // 重绘整个屏幕以清除弹窗并恢复UI
    }
}


// 如果 readBatteryVoltagePercentage 是 ui_manager 的一部分，则在此定义
// 然而，它更像是一个系统工具或电源管理功能。
// 目前，它是 extern 声明的，假设它在 Project-ESPNow.ino 或 power_manager 中。
// float readBatteryVoltagePercentage() { /* ... 实现 ... */ }
