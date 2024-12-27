// 这是VerChannel的上一个版本，由群友QWQ督促完成，新版本请见1.0.5

#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>
#include <esp_now.h>
#include <WiFi.h>
#include <queue>
#include <driver/rtc_io.h>

// 定义触摸屏引脚
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// 定义 IO0 按钮引脚
#define BUTTON_IO0 0 // GPIO 0

#define BATTERY_PIN 34 // GPIO 34，用于读取电池电压

// 校准参数
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3700
#define TOUCH_MIN_Y 300
#define TOUCH_MAX_Y 3800

// Reset 按钮位置和大小
#define RESET_BUTTON_X 4
#define RESET_BUTTON_Y 4
#define RESET_BUTTON_W 28  // 收窄宽度
#define RESET_BUTTON_H 10

// 颜色按钮位置和大小（收窄并靠左）
#define COLOR_BUTTON_WIDTH 15   // 收窄宽度
#define COLOR_BUTTON_HEIGHT 10   // 高度
#define COLOR_BUTTON_START_Y (RESET_BUTTON_Y + RESET_BUTTON_H + 2) // 将起始Y坐标调整为更紧凑
#define COLOR_BUTTON_SPACING 2   // 按钮间距

// 橡皮擦按钮位置和大小
#define ERASER_BUTTON_X RESET_BUTTON_X
#define ERASER_BUTTON_Y (COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * 4) // 在颜色按钮下方
#define ERASER_BUTTON_W COLOR_BUTTON_WIDTH
#define ERASER_BUTTON_H COLOR_BUTTON_HEIGHT

// 新按钮 P 的位置和大小
#define SLEEP_BUTTON_X RESET_BUTTON_X
#define SLEEP_BUTTON_Y (COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * 4) // 在颜色按钮下方
#define SLEEP_BUTTON_W 10  // 按钮宽度
#define SLEEP_BUTTON_H 10   // 按钮高度

// 屏幕宽高
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// 定义颜色
#define COLOR_BLUE tft.color565(0, 0, 255)
#define COLOR_GREEN tft.color565(0, 255, 0)
#define COLOR_RED tft.color565(255, 0, 0)
#define COLOR_YELLOW tft.color565(255, 255, 0)

// 定义 ESP-NOW 的数据结构
typedef struct {
    int x;
    int y;
    unsigned long timestamp;
    bool isReset;            // 是否是 Reset 命令
    bool isLocalDevice;      // 标识发送设备
    uint32_t color;          // 画笔颜色
} TouchData;

// 创建 SPI 和触摸屏对象
SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

// 全局变量
std::queue<TouchData> remoteQueue; // 远程触摸点队列
TS_Point lastLocalPoint = {0, 0, 0}; // 本地上一次触摸点
TS_Point lastRemotePoint = {0, 0, 0}; // 远程上一次触摸点
unsigned long lastLocalTouchTime = 0;
unsigned long lastRemoteTime = 0;
unsigned long touchInterval = 50;

// ESP-NOW 广播地址
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// 当前画笔颜色
uint32_t currentColor = COLOR_BLUE; // 默认颜色为蓝色

// 彩蛋相关变量
unsigned long lastResetTime = 0;
int resetPressCount = 0;

void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_IO0, INPUT_PULLUP); // 使用内部上拉电阻
    pinMode(BATTERY_PIN, INPUT); // 设置电池引脚为输入

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 1;  
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    esp_now_register_recv_cb(OnDataRecv);

    mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(mySpi);
    ts.setRotation(1);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    drawResetButton();
    drawColorButtons();
}

// 读取电池电量并转换为百分比
float readBatteryVoltagePercentage() {
    int adcValue = analogRead(BATTERY_PIN);
    float voltage = (adcValue / 4095.0) * 3.3; // 将 ADC 值转换为电压
    voltage *= 2; // 将电压值乘以 2

    // 根据给定的电压范围计算电量百分比
    float percentage = (voltage - 2.25) / (3.9 - 2.25) * 100; 
    return constrain(percentage, 0, 100); // 限制在 0% 到 100% 之间
}

// 绘制 Reset 按钮
void drawResetButton() {
    tft.fillRect(RESET_BUTTON_X, RESET_BUTTON_Y, RESET_BUTTON_W, RESET_BUTTON_H, TFT_RED);
    float batteryPercentage = readBatteryVoltagePercentage(); // 获取电池百分比
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString(String("  ") + String(batteryPercentage, 1) + "%", 
                         RESET_BUTTON_X + RESET_BUTTON_W / 2, 
                         RESET_BUTTON_Y - 2, 2);
}

// 绘制颜色按钮
void drawColorButtons() {
    uint32_t colors[] = {COLOR_BLUE, COLOR_GREEN, COLOR_RED, COLOR_YELLOW};
    
    // 绘制颜色按钮
    for (int i = 0; i < 4; i++) {
        int buttonY = COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * i;
        tft.fillRect(RESET_BUTTON_X, buttonY, COLOR_BUTTON_WIDTH, COLOR_BUTTON_HEIGHT, colors[i]);
    }

    // 绘制新按钮 P
    tft.fillRect(SLEEP_BUTTON_X, SLEEP_BUTTON_Y, SLEEP_BUTTON_W, SLEEP_BUTTON_H, TFT_BLUE);
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString("P", SLEEP_BUTTON_X + SLEEP_BUTTON_W / 2, SLEEP_BUTTON_Y + 2, 2);
}

// 接收数据回调函数
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingDataPtr, int len) {
    TouchData incomingData;
    memcpy(&incomingData, incomingDataPtr, sizeof(incomingData));

    if (incomingData.isReset) {
        // 如果是 Reset 命令，清空屏幕并重置状态
        tft.fillScreen(TFT_BLACK);
        drawResetButton();
        drawColorButtons();
        lastRemotePoint = {0, 0, 0};
        lastRemoteTime = 0;
    } else {
        // 普通触摸数据加入队列
        remoteQueue.push(incomingData);
    }
}

// 发送触摸数据
void sendTouchData(TouchData data) {
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&data, sizeof(data));
    if (result != ESP_OK) {
        Serial.println("Error sending data");
    }
}

// 检查是否点击了 Reset 按钮
bool isResetButtonPressed(int x, int y) {
    return x >= RESET_BUTTON_X && x <= RESET_BUTTON_X + RESET_BUTTON_W &&
           y >= RESET_BUTTON_Y && y <= RESET_BUTTON_Y + RESET_BUTTON_H;
}

// 检查是否点击了颜色按钮
bool isColorButtonPressed(int x, int y, uint32_t &color) {
    uint32_t colors[] = {COLOR_BLUE, COLOR_GREEN, COLOR_RED, COLOR_YELLOW};
    for (int i = 0; i < 4; i++) {
        int buttonY = COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * i;
        if (x >= RESET_BUTTON_X && x <= RESET_BUTTON_X + COLOR_BUTTON_WIDTH &&
            y >= buttonY && y <= buttonY + COLOR_BUTTON_HEIGHT) {
            color = colors[i];
            return true;
        }
    }
    return false;
}

// 检查是否点击了新按钮 P
bool isSleepButtonPressed(int x, int y) {
    return x >= SLEEP_BUTTON_X && x <= SLEEP_BUTTON_X + SLEEP_BUTTON_W &&
           y >= SLEEP_BUTTON_Y && y <= SLEEP_BUTTON_Y + SLEEP_BUTTON_H;
}

// 处理本地触摸绘制
void handleLocalTouch() {
    bool touched = ts.tirqTouched() && ts.touched();
    unsigned long currentTime = millis();

    if (touched) {
        TS_Point p = ts.getPoint();

        // 映射触摸点坐标到屏幕范围
        int mapX = map(p.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH);
        int mapY = map(p.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT);

        // 检查是否点击了 Reset 按钮
        if (isResetButtonPressed(mapX, mapY)) {
            // 处理 Reset 按钮逻辑...
            // 记录按下时间
            if (currentTime - lastResetTime < 1000) {
                resetPressCount++;
            } else {
                resetPressCount = 1; // 重置计数
            }
            lastResetTime = currentTime; // 更新最后按压时间

            // 如果按下次数达到 10 次，输出消息
            if (resetPressCount >= 10) {
                Serial.println("Kurio Reiko thanks all the recognition and redistribution,");
                Serial.println("but if someone commercializes this project without declaring Kurio Reiko's originality, then he is a bitch");
                resetPressCount = 0; // 重置计数
            }

            // 发送 Reset 命令到远程设备
            TouchData data = {0, 0, millis(), true, true, currentColor};
            sendTouchData(data);

            // 本地重置屏幕和状态
            tft.fillScreen(TFT_BLACK);
            drawResetButton();
            drawColorButtons();
            lastLocalPoint = {0, 0, 0};
            lastLocalTouchTime = 0;

            return; // 不再处理其他触摸
        }

        // 检查是否点击了颜色按钮
        if (isColorButtonPressed(mapX, mapY, currentColor)) {
            // 发送颜色数据到远程设备
            TouchData data = {0, 0, millis(), false, true, currentColor};
            sendTouchData(data);
            return; // 不再处理其他触摸
        }

        // 检查是否点击了新按钮 P
        if (isSleepButtonPressed(mapX, mapY)) {
            Serial.println("Entering deep sleep mode...");
            esp_deep_sleep_start();  // 进入深度睡眠模式
            return; // 不再处理其他触摸
        }

        // 判断是否是新笔画（时间间隔）
        if (currentTime - lastLocalTouchTime > touchInterval) {
            tft.drawPixel(mapX, mapY, currentColor);
        } else {
            tft.drawLine(lastLocalPoint.x, lastLocalPoint.y, mapX, mapY, currentColor);
        }

        // 更新本地触摸点和时间
        lastLocalPoint = {mapX, mapY, 1};
        lastLocalTouchTime = currentTime;

        // 创建触摸数据结构并发送
        TouchData data = {p.x, p.y, currentTime, false, true, currentColor};
        sendTouchData(data);
    } else {
        lastLocalPoint.z = 0;
    }
}

// 处理远程触摸绘制
void handleRemoteTouch() {
    while (!remoteQueue.empty()) {
        TouchData data = remoteQueue.front();
        remoteQueue.pop();

        // 映射远程坐标到屏幕范围
        int mapX = map(data.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH);
        int mapY = map(data.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT);

        // 判断是否是新笔画（时间间隔）
        if (data.timestamp - lastRemoteTime > touchInterval) {
            tft.drawPixel(mapX, mapY, data.color);
        } else {
            tft.drawLine(lastRemotePoint.x, lastRemotePoint.y, mapX, mapY, data.color);
        }

        // 更新远程触摸点和时间
        lastRemotePoint = {mapX, mapY, 1};
        lastRemoteTime = data.timestamp;
    }
}

void loop() {
    handleLocalTouch();
    handleRemoteTouch();

    // 检测 IO0 按钮是否被按下
    if (digitalRead(BUTTON_IO0) == LOW) {
        Serial.println("Entering deep sleep mode...");
        esp_deep_sleep_start();  // 进入深度睡眠模式
    }
}

