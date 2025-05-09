// 2025.1.5更新内容：在右侧添加RGB色彩自定义功能（但是左侧四个颜色按钮暂未删除）
// 感谢群友xiao_hj909发布此项更新
// 2025.3.23更新内容：启用息屏功能，短按boot键息屏，长按两秒进入深度睡眠。息屏后红色指示灯为电源指示灯，蓝色为连接指示灯，绿色为息屏后远程更新指示灯。数值可以自由调整，均使用pwm调光，可调节亮度。
// 感谢群友2093416185（shapaper@126.com）发布此项更新
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h> // Added for esp_wifi_get_mac()
#include <queue>
#include <set>
#include <vector>
#include <cmath> // For abs()

// 定义触摸屏引脚
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// 定义 IO0 按钮引脚
#define BUTTON_IO0 0   // GPIO 0
#define TFT_BL 21      // GPIO 21 用于控制背光
#define GREEN_LED 16   // GPIO 16 用于控制绿色LED
#define BLUE_LED 17    // 蓝色LED
#define RED_LED 22     // 红色LED
#define BATTERY_PIN 34 // GPIO 34，用于读取电池电压

// 校准参数
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3700
#define TOUCH_MIN_Y 300
#define TOUCH_MAX_Y 3800

// Reset 按钮位置和大小
#define RESET_BUTTON_X 4
#define RESET_BUTTON_Y 4  // 调整为 4
#define RESET_BUTTON_W 28 // 收窄宽度
#define RESET_BUTTON_H 10

// 颜色按钮位置和大小（收窄并靠左）
#define COLOR_BUTTON_WIDTH 15                                      // 收窄宽度
#define COLOR_BUTTON_HEIGHT 10                                     // 高度
#define COLOR_BUTTON_START_Y (RESET_BUTTON_Y + RESET_BUTTON_H + 2) // 将起始Y坐标调整为更紧凑
#define COLOR_BUTTON_SPACING 2                                     // 按钮间距

// 新按钮 P 的位置和大小
#define SLEEP_BUTTON_X RESET_BUTTON_X
#define SLEEP_BUTTON_Y (COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * 4) // 在颜色按钮下方
#define SLEEP_BUTTON_W 10                                                                        // 按钮宽度
#define SLEEP_BUTTON_H 10                                                                        // 按钮高度

// 自定义颜色按钮的位置和大小
#define CUSTOM_COLOR_BUTTON_X (SCREEN_WIDTH - COLOR_BUTTON_WIDTH - 4)
#define CUSTOM_COLOR_BUTTON_Y 4
#define CUSTOM_COLOR_BUTTON_W COLOR_BUTTON_WIDTH  // 同样宽度
#define CUSTOM_COLOR_BUTTON_H COLOR_BUTTON_HEIGHT // 同样高度

// 返回按钮的位置和大小
#define BACK_BUTTON_X (SCREEN_WIDTH - COLOR_BUTTON_WIDTH - 4)
#define BACK_BUTTON_Y (SCREEN_HEIGHT - COLOR_BUTTON_HEIGHT - 4)
#define BACK_BUTTON_W COLOR_BUTTON_WIDTH  // 同样宽度
#define BACK_BUTTON_H COLOR_BUTTON_HEIGHT // 同样高度

// 屏幕宽高
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// 手动定义 TFT_GRAY
#define TFT_GRAY 0x8410

// 定义 ESP-NOW 的数据结构 (Original TouchData, modified)
typedef struct
{
    int x;                   // Screen-mapped X coordinate for drawing
    int y;                   // Screen-mapped Y coordinate for drawing
    unsigned long timestamp; // Timestamp of the drawing action (local millis() when drawn)
    bool isReset;            // True if this action is a screen reset
    uint32_t color;          // Drawing color
} TouchData;

// ESP-NOW Synchronization Data Structures and Enums
enum MessageType_t
{
    MSG_TYPE_UPTIME_INFO,              // Payload: senderUptime (current millis() of sender), senderOffset
    MSG_TYPE_DRAW_POINT,               // Payload: senderUptime, senderOffset, TouchData
    MSG_TYPE_REQUEST_ALL_DRAWINGS,     // Payload: senderUptime (requester's current millis()), senderOffset
    MSG_TYPE_ALL_DRAWINGS_COMPLETE,    // Payload: senderUptime (sender of data's current millis()), senderOffset
    MSG_TYPE_CLEAR_AND_REQUEST_UPDATE, // Payload: senderUptime (older device telling newer to request), senderOffset
    MSG_TYPE_RESET_CANVAS              // Payload: senderUptime, senderOffset, TouchData (with isReset = true)
};

typedef struct
{
    MessageType_t type;
    unsigned long senderUptime; // Sender's current RAW millis() at the time of sending
    long senderOffset;          // Sender's current relativeBootTimeOffset
    TouchData touch_data;       // Used for DRAW_POINT and RESET_CANVAS.
                                // For other types, fields in touch_data might be default/irrelevant.
} SyncMessage_t;

typedef struct
{
    float x; // Raw touch X
    float y; // Raw touch Y
    bool fly = false;
} XY_structure;

// 添加呼吸灯相关变量
int breathBrightness = 0;                // 呼吸灯亮度
int breathDirection = 5;                 // 呼吸灯亮度变化方向和速度
unsigned long lastBreathTime = 0;        // 上次更新呼吸灯时间
bool hasNewUpdateWhileScreenOff = false; // 标记息屏后是否有新的更新

// 创建 SPI 和触摸屏对象
SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

// 全局变量 for synchronization (同步相关的全局变量)
unsigned long deviceInitialBootMillis = 0;                     // 本机启动时的 millis() 值 (仅供参考)
unsigned long lastKnownPeerUptime = 0;                         // 最后已知的对端设备 RAW UPTIME (millis())
long lastKnownPeerOffset = 0;                                  // 最后已知的对端设备 OFFSET
uint8_t lastPeerMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // 默认为广播地址, 收到特定对端消息后更新
bool initialSyncLogicProcessed = false;                        // 标志位，确保每次与对端交互时启动时间比较逻辑只运行一次
bool iamEffectivelyMoreUptimeDevice = false;                   // 如果此设备比对端有效运行时间长，则为 true
bool iamRequestingAllData = false;                             // 如果此设备有效运行时间较短且当前正在请求数据，则为 true

long relativeBootTimeOffset = 0; // 本设备的补偿值: (older_peer_raw_uptime_at_complete - my_raw_uptime_at_receive_complete - buffer)
                                 // 用于计算 localEffectiveUptime = millis() + relativeBootTimeOffset

std::vector<TouchData> allDrawingHistory;       // 主列表，存储所有本地和远程的绘制操作
std::queue<SyncMessage_t> incomingMessageQueue; // ESP-NOW 传入消息队列

static unsigned long uptimeOfLastPeerSyncedFrom = 0;             // RAW Uptime of the peer this device last synced data FROM (at the moment sync completed). Used for hysteresis.
const unsigned long MIN_UPTIME_DIFF_FOR_NEW_SYNC_TARGET = 200UL; // Min diff to consider a new sync target (hysteresis buffer AND part of offset calc).
const unsigned long EFFECTIVE_UPTIME_SYNC_THRESHOLD = 1000UL;    // 1 second threshold for effective uptime comparisons

// Other global variables (其他全局变量)
std::set<String> macSet;              // 存储接收到的 MAC 地址 (用于设备计数显示)
TS_Point lastLocalPoint = {0, 0, 0};  // 本地上一次触摸点 (用于绘制线条)
TS_Point lastRemotePoint = {0, 0, 0}; // 远程最后一点 (用于以正确的连续性重播历史记录)
unsigned long lastLocalTouchTime = 0; // 用于本地绘制的连续性
unsigned long lastRemoteDrawTime = 0; // 用于以正确的时间/连续性重播历史记录
unsigned long touchInterval = 50;     // 认为是一次新笔划的时间间隔

// ESP-NOW 广播地址
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t broadcastPeerInfo; // 对端信息，用于广播

// 当前画笔颜色
uint32_t currentColor = TFT_BLUE; // 默认颜色为蓝色

// 彩蛋相关变量
unsigned long lastResetTime = 0;
int resetPressCount = 0;

// 定时广播间隔
#define BROADCAST_INTERVAL 2000 // 每2秒广播一次 (for MAC address discovery)
unsigned long lastBroadcastTime = 0;
#define UPTIME_INFO_BROADCAST_INTERVAL 5000 // Every 5 seconds if no peer found yet, or to re-establish
unsigned long lastUptimeInfoBroadcastTime = 0;

#define DEBUG_INFO_UPDATE_INTERVAL 200 // Update debug info every 1 second
unsigned long lastDebugInfoUpdateTime = 0;

// 调色界面状态
bool inCustomColorMode = false;

// 颜色通道值
int redValue = 255;
int greenValue = 255;
int blueValue = 255;

// 颜色选择器尺寸
#define COLOR_SLIDER_WIDTH 20
#define COLOR_SLIDER_HEIGHT ((SCREEN_HEIGHT - 10) / 3)

// 保存调色界面之前的屏幕截图
uint16_t *savedScreenBuffer = nullptr;

// 布尔类型存储是否息屏 true 是开
bool isScreenOn = true;

// 息屏提示灯 红色是电源指示灯 蓝色是连接指示灯
const int BLUE_LED_DIM = 14; // 10% 亮度 (255 * 0.1 ≈ 25)
const int RED_LED_DIM = 4;   // 5% 亮度 (255 * 0.05 ≈ 13)

// Function to draw debug information
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
    tft.setTextFont(1); 

    char buffer[50];

    sprintf(buffer, "Hist: %d", allDrawingHistory.size());
    tft.setCursor(startX + 2, startY + 2);
    tft.print(buffer);

    sprintf(buffer, "Uptime: %lu", millis()); 
    tft.setCursor(startX + 2, startY + 2 + lineHeight);
    tft.print(buffer);

    sprintf(buffer, "Comp: %ld", relativeBootTimeOffset); 
    tft.setCursor(startX + 2, startY + 2 + 2 * lineHeight);
    tft.print(buffer);
    
    sprintf(buffer, "Mem: %d/%dKB", ESP.getFreeHeap() / 1024, ESP.getHeapSize() / 1024);
    tft.setCursor(startX + 2, startY + 2 + 3 * lineHeight);
    tft.print(buffer);
}


void setup()
{
    Serial.begin(115200);
    pinMode(BUTTON_IO0, INPUT_PULLUP); 
    pinMode(BATTERY_PIN, INPUT);       

    pinMode(GREEN_LED, OUTPUT);
    analogWriteResolution(GREEN_LED, 8);   
    analogWriteFrequency(GREEN_LED, 5000); 
    analogWrite(GREEN_LED, 255);           

    pinMode(BLUE_LED, OUTPUT);
    pinMode(RED_LED, OUTPUT);
    analogWriteResolution(BLUE_LED, 8);
    analogWriteResolution(RED_LED, 8);
    analogWriteFrequency(BLUE_LED, 5000);
    analogWriteFrequency(RED_LED, 5000);
    analogWrite(BLUE_LED, 255); 
    analogWrite(RED_LED, 255);  

    deviceInitialBootMillis = millis(); 
    uptimeOfLastPeerSyncedFrom = 0; 
    relativeBootTimeOffset = 0;     
    Serial.print("Device Initial Boot Millis: ");
    Serial.println(deviceInitialBootMillis);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(); 

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    esp_now_register_send_cb(OnSyncDataSent); 
    esp_now_register_recv_cb(OnSyncDataRecv); 

    memcpy(broadcastPeerInfo.peer_addr, broadcastAddress, 6);
    broadcastPeerInfo.channel = 0;         
    broadcastPeerInfo.ifidx = WIFI_IF_STA; 
    broadcastPeerInfo.encrypt = false;     
    if (esp_now_add_peer(&broadcastPeerInfo) != ESP_OK)
    {
        Serial.println("添加广播对端失败");
        esp_now_del_peer(broadcastPeerInfo.peer_addr);
        if (esp_now_add_peer(&broadcastPeerInfo) != ESP_OK)
        {
            Serial.println("尝试删除后重新添加广播对端仍然失败");
            return;
        }
        Serial.println("初次失败后成功重新添加广播对端。");
    }
    else
    {
        Serial.println("广播对端添加成功。");
    }

    SyncMessage_t uptimeMsg;
    uptimeMsg.type = MSG_TYPE_UPTIME_INFO;
    uptimeMsg.senderUptime = millis(); 
    uptimeMsg.senderOffset = relativeBootTimeOffset; // Will be 0 initially
    memset(&uptimeMsg.touch_data, 0, sizeof(TouchData)); 

    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&uptimeMsg, sizeof(uptimeMsg));
    if (result == ESP_OK)
    {
        Serial.println("初始 UPTIME_INFO 发送成功。");
    }
    else
    {
        Serial.print("发送初始 UPTIME_INFO 错误: ");
        Serial.println(esp_err_to_name(result));
    }
    lastUptimeInfoBroadcastTime = millis(); 

    mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(mySpi);
    ts.setRotation(1);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    drawMainInterface();
}

void updateBreathLED()
{
    unsigned long currentTime = millis();
    if (currentTime - lastBreathTime >= 10)
    {
        lastBreathTime = currentTime;
        breathBrightness += breathDirection;
        if (breathBrightness >= 255 || breathBrightness <= 0)
        {
            breathDirection = -breathDirection;
            breathBrightness = constrain(breathBrightness, 0, 255);
        }
        analogWrite(GREEN_LED, 255 - breathBrightness);
    }
}

float readBatteryVoltagePercentage()
{
    int adcValue = analogRead(BATTERY_PIN);
    float voltage = (adcValue / 4095.0) * 3.3; 
    voltage *= 2;                              
    float percentage = (voltage - 2.25) / (3.9 - 2.25) * 100;
    return constrain(percentage, 0, 100); 
}

void drawMainInterface()
{
    tft.fillScreen(TFT_BLACK);
    drawResetButton();
    drawColorButtons();
    drawSleepButton();
    drawCustomColorButton();
    updateConnectedDevicesCount();
    drawStarButton(); 
    if (isScreenOn) { 
        drawDebugInfo();
    }
}

void drawResetButton()
{
    tft.fillRect(RESET_BUTTON_X, RESET_BUTTON_Y, RESET_BUTTON_W, RESET_BUTTON_H, TFT_RED);
    float batteryPercentage = readBatteryVoltagePercentage(); 
    tft.setTextColor(TFT_WHITE);
    tft.drawCentreString(String("  ") + String(batteryPercentage, 1) + "%",
                         RESET_BUTTON_X + RESET_BUTTON_W / 2,
                         RESET_BUTTON_Y - 2, 2);
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
    char deviceCountBuffer[10];
    sprintf(deviceCountBuffer, "%d", macSet.size());
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1); 
    tft.setTextFont(2); 
    String text = String(deviceCountBuffer);
    int textWidth = tft.textWidth(text);
    int textHeight = 8; 
    tft.setCursor(SLEEP_BUTTON_X + (SLEEP_BUTTON_W - textWidth) / 2, SLEEP_BUTTON_Y + (SLEEP_BUTTON_H - textHeight) / 2);
    tft.print(text);
}

void drawCustomColorButton()
{
    tft.fillRect(CUSTOM_COLOR_BUTTON_X, CUSTOM_COLOR_BUTTON_Y, CUSTOM_COLOR_BUTTON_W, CUSTOM_COLOR_BUTTON_H, currentColor);
}

void drawStarButton()
{
    tft.fillRect(SCREEN_WIDTH - COLOR_BUTTON_WIDTH - 4, 4, COLOR_BUTTON_WIDTH, COLOR_BUTTON_HEIGHT, currentColor);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1); 
    tft.setTextFont(2); 
    tft.setCursor(SCREEN_WIDTH - COLOR_BUTTON_WIDTH - 4 + 1, 4 + 1);
    tft.print("*"); 
}

void OnSyncDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    if (status != ESP_NOW_SEND_SUCCESS)
    {
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        Serial.print("发送到 ");
        Serial.print(macStr);
        Serial.print(" 失败。状态: ");
        Serial.println(status == ESP_NOW_SEND_SUCCESS ? "成功" : "失败");
    }
}

void OnSyncDataRecv(const esp_now_recv_info *info, const uint8_t *incomingDataPtr, int len)
{
    if (len == sizeof(SyncMessage_t))
    {
        SyncMessage_t receivedMsg;
        memcpy(&receivedMsg, incomingDataPtr, sizeof(receivedMsg));
        memcpy(lastPeerMac, info->src_addr, 6);
        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 info->src_addr[0], info->src_addr[1], info->src_addr[2],
                 info->src_addr[3], info->src_addr[4], info->src_addr[5]);
        macSet.insert(String(macStr)); 
        incomingMessageQueue.push(receivedMsg);
    }
    else if (len == strlen("XX:XX:XX:XX:XX:XX") && incomingDataPtr[0] != '{')
    { 
        char macStr[18];
        memcpy(macStr, incomingDataPtr, len);
        macStr[len] = '\0'; 
        macSet.insert(String(macStr));
    }
    else
    {
        Serial.print("收到意外长度的数据: ");
        Serial.print(len);
        Serial.print(", 期望长度: ");
        Serial.println(sizeof(SyncMessage_t));
    }
}

void sendSyncMessage(const SyncMessage_t *msg)
{
    // Ensure senderUptime and senderOffset are set before calling this
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)msg, sizeof(SyncMessage_t));
    if (result != ESP_OK)
    {
        Serial.print("发送 SyncMessage 类型 ");
        Serial.print(msg->type);
        Serial.print(" 错误: ");
        Serial.println(esp_err_to_name(result));
    }
}

bool isResetButtonPressed(int x, int y)
{
    return x >= RESET_BUTTON_X && x <= RESET_BUTTON_X + RESET_BUTTON_W &&
           y >= RESET_BUTTON_Y && y <= RESET_BUTTON_Y + RESET_BUTTON_H;
}

bool isColorButtonPressed(int x, int y, uint32_t &color)
{
    uint32_t colors[] = {TFT_BLUE, TFT_GREEN, TFT_RED, TFT_YELLOW};
    for (int i = 0; i < 4; i++)
    {
        int buttonY = COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * i;
        if (x >= RESET_BUTTON_X && x <= RESET_BUTTON_X + COLOR_BUTTON_WIDTH &&
            y >= buttonY && y <= buttonY + COLOR_BUTTON_HEIGHT)
        {
            color = colors[i];
            return true;
        }
    }
    return false;
}

XY_structure averageXY(void)
{
    TS_Point p = ts.getPoint();
    bool fly = false;
    int cnt = 0;
    int i, j, k, min, temp;
    int tmp[2][10];
    XY_structure XY;
    for (cnt = 0; cnt <= 9; cnt++)
    {
        TS_Point p = ts.getPoint();
        if (p.z > 200)
        {
            tmp[0][cnt] = p.x;
            tmp[1][cnt] = p.y;
            delay(2);
        }
        else
        {
            fly = true;
            break;
        }
    }
    if (fly)
    {
        XY.fly = fly;
        return XY;
    }
    for (k = 0; k < 2; k++)
    { 
        for (i = 0; i < cnt - 1; i++)
        {
            min = i;
            for (j = i + 1; j < cnt; j++)
            {
                if (tmp[k][min] > tmp[k][j])
                    min = j;
            }
            temp = tmp[k][i];
            tmp[k][i] = tmp[k][min];
            tmp[k][min] = temp;
        }
    }
    XY.x = (tmp[0][3] + tmp[0][4] + tmp[0][5] + tmp[0][6]) / 4;
    XY.y = (tmp[1][3] + tmp[1][4] + tmp[1][5] + tmp[1][6]) / 4;
    return XY;
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

void handleLocalTouch()
{
    float x1, y1;
    bool touched = ts.tirqTouched() && ts.touched();
    unsigned long currentRawUptime = millis(); // Local raw uptime for this touch event
    XY_structure xy1;
    if (touched)
    {
        xy1 = averageXY();
        x1 = xy1.x;
        y1 = xy1.y;
        if (!xy1.fly)
        {
            int mapX = map(x1, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH);
            int mapY = map(y1, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT);

            if (inCustomColorMode)
            {
                handleCustomColorTouch(mapX, mapY);
            }
            else
            {
                if (isResetButtonPressed(mapX, mapY))
                {
                    if (currentRawUptime - lastResetTime < 1000) {
                        resetPressCount++;
                    } else {
                        resetPressCount = 1; 
                    }
                    lastResetTime = currentRawUptime; 

                    if (resetPressCount >= 10) {
                        Serial.println("Kurio Reiko thanks all the recognition and redistribution,");
                        Serial.println("but if someone commercializes this project without declaring Kurio Reiko's originality, then he is a bitch");
                        resetPressCount = 0; 
                    }

                    allDrawingHistory.clear(); 
                    clearScreenAndCache();     
                    relativeBootTimeOffset = 0; 
                    iamEffectivelyMoreUptimeDevice = false;
                    iamRequestingAllData = false;
                    initialSyncLogicProcessed = false; // Allow re-sync after reset

                    SyncMessage_t resetMsg;
                    resetMsg.type = MSG_TYPE_RESET_CANVAS;
                    resetMsg.senderUptime = currentRawUptime; 
                    resetMsg.senderOffset = relativeBootTimeOffset; // Will be 0
                    resetMsg.touch_data.isReset = true;
                    resetMsg.touch_data.timestamp = currentRawUptime; 
                    resetMsg.touch_data.x = 0;                
                    resetMsg.touch_data.y = 0;                
                    resetMsg.touch_data.color = currentColor; 
                    sendSyncMessage(&resetMsg);
                    allDrawingHistory.push_back(resetMsg.touch_data);
                    return; 
                }

                if (isColorButtonPressed(mapX, mapY, currentColor))
                {
                    updateCurrentColor(currentColor);
                    redrawStarButton(); 
                    return;             
                }

                if (isSleepButtonPressed(mapX, mapY))
                {
                    Serial.println("Entering deep sleep mode...");
                    esp_deep_sleep_start(); 
                    return;                 
                }

                if (isCustomColorButtonPressed(mapX, mapY))
                {
                    inCustomColorMode = true;
                    saveScreenArea();
                    drawColorSelectors();
                    hideStarButton(); 
                    return;           
                }

                if (currentRawUptime - lastLocalTouchTime > touchInterval || lastLocalPoint.z == 0)
                {
                    tft.drawPixel(mapX, mapY, currentColor);
                }
                else
                {
                    tft.drawLine(lastLocalPoint.x, lastLocalPoint.y, mapX, mapY, currentColor);
                }

                lastLocalPoint = {mapX, mapY, 1}; 
                lastLocalTouchTime = currentRawUptime;

                TouchData currentDrawPoint;
                currentDrawPoint.x = mapX; 
                currentDrawPoint.y = mapY;
                currentDrawPoint.timestamp = currentRawUptime; 
                currentDrawPoint.isReset = false;
                currentDrawPoint.color = currentColor;
                allDrawingHistory.push_back(currentDrawPoint);

                SyncMessage_t drawMsg;
                drawMsg.type = MSG_TYPE_DRAW_POINT;
                drawMsg.senderUptime = currentRawUptime; 
                drawMsg.senderOffset = relativeBootTimeOffset;
                drawMsg.touch_data = currentDrawPoint;
                sendSyncMessage(&drawMsg);
            }
        }
    }
    else
    {
        lastLocalPoint.z = 0;
    }
}

void handleCustomColorTouch(int x, int y)
{
    if (x >= (SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4) && x <= SCREEN_WIDTH)
    {
        if (y >= 0 && y <= COLOR_SLIDER_HEIGHT)
        {
            updateSingleColorSlider(y, TFT_RED, redValue);
        }
        else if (y >= COLOR_SLIDER_HEIGHT && y <= 2 * COLOR_SLIDER_HEIGHT)
        {
            updateSingleColorSlider(y - COLOR_SLIDER_HEIGHT, TFT_GREEN, greenValue);
        }
        else if (y >= 2 * COLOR_SLIDER_HEIGHT && y <= 3 * COLOR_SLIDER_HEIGHT)
        {
            updateSingleColorSlider(y - 2 * COLOR_SLIDER_HEIGHT, TFT_BLUE, blueValue);
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

void updateSingleColorSlider(int y, uint32_t color, int &value)
{
    value = constrain(map(y, 0, COLOR_SLIDER_HEIGHT, 0, 255), 0, 255); 
}

void drawColorSelectors()
{
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_RED);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, redValue * (COLOR_SLIDER_HEIGHT / 255.0), TFT_RED);
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_GREEN);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, greenValue * (COLOR_SLIDER_HEIGHT / 255.0), TFT_GREEN);
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_BLUE);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, blueValue * (COLOR_SLIDER_HEIGHT / 255.0), TFT_BLUE);
    tft.fillRect(BACK_BUTTON_X, BACK_BUTTON_Y, BACK_BUTTON_W, BACK_BUTTON_H, TFT_GRAY);
    updateCustomColorPreview();
}

void updateCustomColorPreview()
{
    uint32_t previewColor = tft.color565(redValue, greenValue, blueValue);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 3 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, previewColor);
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 3 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_WHITE);
}

void refreshAllColorSliders()
{
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, 4 * COLOR_SLIDER_HEIGHT, TFT_BLACK);
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_RED);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, redValue * (COLOR_SLIDER_HEIGHT / 255.0), TFT_RED);
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_GREEN);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, greenValue * (COLOR_SLIDER_HEIGHT / 255.0), TFT_GREEN);
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_BLUE);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, blueValue * (COLOR_SLIDER_HEIGHT / 255.0), TFT_BLUE);
    updateCustomColorPreview();
}

void closeColorSelectors()
{
    inCustomColorMode = false;
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, 4 * COLOR_SLIDER_HEIGHT, TFT_BLACK);
    currentColor = tft.color565(redValue, greenValue, blueValue);
    drawCustomColorButton(); 
    restoreSavedScreenArea();
    tft.fillScreen(TFT_BLACK); 
    drawMainInterface();       
    replayAllDrawings();       
    showStarButton(); 
    drawStarButton(); 
}

void updateCurrentColor(uint32_t newColor)
{
    currentColor = newColor;
}

void processIncomingMessages()
{
    while (!incomingMessageQueue.empty())
    {
        SyncMessage_t msg = incomingMessageQueue.front();
        incomingMessageQueue.pop();

        unsigned long localCurrentRawUptime = millis();
        long localCurrentOffset = relativeBootTimeOffset; 
        unsigned long localEffectiveUptime = localCurrentRawUptime + localCurrentOffset;

        unsigned long peerRawUptime = msg.senderUptime;
        long peerReceivedOffset = msg.senderOffset; 
        unsigned long peerEffectiveUptime = peerRawUptime + peerReceivedOffset;

        switch (msg.type)
        {
        case MSG_TYPE_UPTIME_INFO:
        { 
            Serial.println("收到 MSG_TYPE_UPTIME_INFO");
            
            if (uptimeOfLastPeerSyncedFrom != 0 && !iamRequestingAllData) {
                unsigned long diffFromLastSyncSource = (peerRawUptime > uptimeOfLastPeerSyncedFrom) ? 
                                                       (peerRawUptime - uptimeOfLastPeerSyncedFrom) : 
                                                       (uptimeOfLastPeerSyncedFrom - peerRawUptime);
                if (diffFromLastSyncSource < MIN_UPTIME_DIFF_FOR_NEW_SYNC_TARGET) {
                    Serial.print("  Hysteresis: Peer raw uptime ("); Serial.print(peerRawUptime);
                    Serial.print(") is too similar to last sync source raw uptime ("); Serial.print(uptimeOfLastPeerSyncedFrom);
                    Serial.print(", diff "); Serial.print(diffFromLastSyncSource);
                    Serial.print("ms < "); Serial.print(MIN_UPTIME_DIFF_FOR_NEW_SYNC_TARGET);
                    Serial.println("ms). Skipping full sync evaluation for this peer.");
                    if (lastKnownPeerUptime != peerRawUptime || lastKnownPeerOffset != peerReceivedOffset) {
                        lastKnownPeerUptime = peerRawUptime;
                        lastKnownPeerOffset = peerReceivedOffset;
                        initialSyncLogicProcessed = false; 
                    }
                    if (!initialSyncLogicProcessed) initialSyncLogicProcessed = true; 
                    break; 
                }
            }
            
            if (lastKnownPeerUptime != peerRawUptime || lastKnownPeerOffset != peerReceivedOffset || !initialSyncLogicProcessed) {
                lastKnownPeerUptime = peerRawUptime; 
                lastKnownPeerOffset = peerReceivedOffset;

                Serial.println("  Processing UPTIME_INFO for sync decision.");
                Serial.print("  Local Effective Uptime: "); Serial.print(localEffectiveUptime);
                Serial.print(" (Raw: "); Serial.print(localCurrentRawUptime); Serial.print(", Offset: "); Serial.print(localCurrentOffset); Serial.println(")");
                Serial.print("  Peer Effective Uptime: "); Serial.print(peerEffectiveUptime);
                Serial.print(" (Raw: "); Serial.print(peerRawUptime); Serial.print(", Offset: "); Serial.print(peerReceivedOffset); Serial.println(")");

                if (localEffectiveUptime == peerEffectiveUptime) { 
                    uint8_t myMacAddr[6];
                    esp_wifi_get_mac(WIFI_IF_STA, myMacAddr);
                    if (memcmp(myMacAddr, lastPeerMac, 6) < 0) { 
                        Serial.println("  决策 (有效运行时间相同, MAC较小): 本机行为类似较新设备：清空并请求数据。");
                        iamEffectivelyMoreUptimeDevice = false; iamRequestingAllData = true;
                        allDrawingHistory.clear(); tft.fillScreen(TFT_BLACK); drawMainInterface();
                        SyncMessage_t requestMsg; requestMsg.type = MSG_TYPE_REQUEST_ALL_DRAWINGS;
                        requestMsg.senderUptime = localCurrentRawUptime; requestMsg.senderOffset = localCurrentOffset;
                        memset(&requestMsg.touch_data, 0, sizeof(TouchData)); sendSyncMessage(&requestMsg);
                    } else { 
                        Serial.println("  决策 (有效运行时间相同, MAC较大/相等): 本机行为类似较旧设备：通知对端清空并请求数据。");
                        iamEffectivelyMoreUptimeDevice = true; iamRequestingAllData = false;
                        SyncMessage_t promptMsg; promptMsg.type = MSG_TYPE_CLEAR_AND_REQUEST_UPDATE;
                        promptMsg.senderUptime = localCurrentRawUptime; promptMsg.senderOffset = localCurrentOffset;
                        memset(&promptMsg.touch_data, 0, sizeof(TouchData)); sendSyncMessage(&promptMsg);
                    }
                } else {
                    unsigned long effectiveUptimeDifference = (localEffectiveUptime > peerEffectiveUptime) ? 
                                                              (localEffectiveUptime - peerEffectiveUptime) : 
                                                              (peerEffectiveUptime - localEffectiveUptime);

                    if (effectiveUptimeDifference <= EFFECTIVE_UPTIME_SYNC_THRESHOLD) {
                        Serial.println("  决策: 有效运行时间差在1秒阈值内。不启动新的同步以避免抖动。");
                    } else if (localEffectiveUptime < peerEffectiveUptime) { 
                        Serial.println("  决策: 本机有效运行时间较短 (超出阈值)。判定本机为较新设备。清空本机数据并向对端 (较旧设备) 请求所有绘图数据。");
                        iamEffectivelyMoreUptimeDevice = false; iamRequestingAllData = true;
                        allDrawingHistory.clear(); tft.fillScreen(TFT_BLACK); drawMainInterface();
                        SyncMessage_t requestMsg; requestMsg.type = MSG_TYPE_REQUEST_ALL_DRAWINGS;
                        requestMsg.senderUptime = localCurrentRawUptime; requestMsg.senderOffset = localCurrentOffset;
                        memset(&requestMsg.touch_data, 0, sizeof(TouchData)); sendSyncMessage(&requestMsg);
                    } else { // localEffectiveUptime > peerEffectiveUptime && diff > threshold
                        Serial.println("  决策: 本机有效运行时间较长 (超出阈值)。判定本机为较旧设备。通知对端 (较新设备) 清空并向本机请求更新。");
                        iamEffectivelyMoreUptimeDevice = true; iamRequestingAllData = false;
                        SyncMessage_t promptMsg; promptMsg.type = MSG_TYPE_CLEAR_AND_REQUEST_UPDATE;
                        promptMsg.senderUptime = localCurrentRawUptime; promptMsg.senderOffset = localCurrentOffset;
                        memset(&promptMsg.touch_data, 0, sizeof(TouchData)); sendSyncMessage(&promptMsg);
                    }
                }
                initialSyncLogicProcessed = true; 
            } else {
                 // Serial.println("  收到 MSG_TYPE_UPTIME_INFO, 但已针对此对端 (raw uptime + offset) 处理过。忽略。");
            }
            break;
        }
        case MSG_TYPE_DRAW_POINT:
        { 
            allDrawingHistory.push_back(msg.touch_data); 
            TouchData data = msg.touch_data;
            int mapX = data.x; int mapY = data.y;
            if (data.timestamp - lastRemoteDrawTime > touchInterval || lastRemotePoint.z == 0) {
                tft.drawPixel(mapX, mapY, data.color);
            } else {
                tft.drawLine(lastRemotePoint.x, lastRemotePoint.y, mapX, mapY, data.color);
            }
            lastRemotePoint = {mapX, mapY, 1}; lastRemoteDrawTime = data.timestamp; 
            if (!isScreenOn) hasNewUpdateWhileScreenOff = true; 
            break;
        }
        case MSG_TYPE_REQUEST_ALL_DRAWINGS: 
        { 
            Serial.println("收到 MSG_TYPE_REQUEST_ALL_DRAWINGS.");
            if (localEffectiveUptime > peerEffectiveUptime) { // No threshold here, just confirm older
                Serial.print("  决策: 本机有效运行时间较长。发送所有 "); Serial.print(allDrawingHistory.size()); Serial.println(" 个点。");
                iamEffectivelyMoreUptimeDevice = true; 
                lastKnownPeerUptime = peerRawUptime; lastKnownPeerOffset = peerReceivedOffset;
                initialSyncLogicProcessed = true; 
                for (const auto& drawData : allDrawingHistory) {
                    SyncMessage_t historyPointMsg; historyPointMsg.type = MSG_TYPE_DRAW_POINT;
                    historyPointMsg.senderUptime = localCurrentRawUptime; historyPointMsg.senderOffset = localCurrentOffset;
                    historyPointMsg.touch_data = drawData; sendSyncMessage(&historyPointMsg); delay(10); 
                }
                SyncMessage_t completeMsg; completeMsg.type = MSG_TYPE_ALL_DRAWINGS_COMPLETE;
                completeMsg.senderUptime = localCurrentRawUptime; completeMsg.senderOffset = localCurrentOffset;
                memset(&completeMsg.touch_data, 0, sizeof(TouchData)); sendSyncMessage(&completeMsg);
                Serial.println("  所有绘图数据已发送。发送了 ALL_DRAWINGS_COMPLETE。");
            } else {
                 Serial.println("  决策: 收到 REQUEST_ALL_DRAWINGS，但本机有效运行时间并非较长。忽略。");
                 if(peerRawUptime != 0) lastKnownPeerUptime = peerRawUptime;
                 if(peerReceivedOffset != 0 || lastKnownPeerOffset != 0) lastKnownPeerOffset = peerReceivedOffset;
                 initialSyncLogicProcessed = true;
            }
            break;
        }
        case MSG_TYPE_ALL_DRAWINGS_COMPLETE: 
        { 
            Serial.println("收到 MSG_TYPE_ALL_DRAWINGS_COMPLETE.");
            if (iamRequestingAllData && !iamEffectivelyMoreUptimeDevice) { 
                Serial.println("  同步完成 (本机为较新设备)。");
                iamRequestingAllData = false; 
                relativeBootTimeOffset = (long)peerRawUptime +(long)lastKnownPeerOffset- (long)localCurrentRawUptime ;
                uptimeOfLastPeerSyncedFrom = peerRawUptime; 
                Serial.print("  relativeBootTimeOffset 计算并设置为: "); Serial.println(relativeBootTimeOffset);
                Serial.print("  uptimeOfLastPeerSyncedFrom 设置为: "); Serial.println(uptimeOfLastPeerSyncedFrom);
                lastKnownPeerUptime = peerRawUptime; lastKnownPeerOffset = peerReceivedOffset; 
                initialSyncLogicProcessed = true; 
            } else {
                Serial.println("  收到 ALL_DRAWINGS_COMPLETE，但本机状态不符。忽略。");
            }
            break;
        }
        case MSG_TYPE_CLEAR_AND_REQUEST_UPDATE: 
        { 
            Serial.println("收到 MSG_TYPE_CLEAR_AND_REQUEST_UPDATE.");
            unsigned long effectiveUptimeDifference = (localEffectiveUptime > peerEffectiveUptime) ? 
                                                      (localEffectiveUptime - peerEffectiveUptime) : 
                                                      (peerEffectiveUptime - localEffectiveUptime);

            if (localEffectiveUptime < peerEffectiveUptime && effectiveUptimeDifference > EFFECTIVE_UPTIME_SYNC_THRESHOLD) { 
                Serial.println("  决策: 本机有效运行时间较短 (超出阈值)。执行清空并请求。");
                iamEffectivelyMoreUptimeDevice = false; iamRequestingAllData = true;  
                allDrawingHistory.clear(); tft.fillScreen(TFT_BLACK); drawMainInterface();       
                SyncMessage_t requestMsg; requestMsg.type = MSG_TYPE_REQUEST_ALL_DRAWINGS;
                requestMsg.senderUptime = localCurrentRawUptime; requestMsg.senderOffset = localCurrentOffset;
                memset(&requestMsg.touch_data, 0, sizeof(TouchData)); sendSyncMessage(&requestMsg); 
                lastKnownPeerUptime = peerRawUptime; lastKnownPeerOffset = peerReceivedOffset;
                initialSyncLogicProcessed = true; 
            } else {
                Serial.println("  决策: 收到 CLEAR_AND_REQUEST_UPDATE，但本机有效运行时间并非较短 (或在阈值内)。忽略。");
                if(peerRawUptime != 0) lastKnownPeerUptime = peerRawUptime;
                if(peerReceivedOffset != 0 || lastKnownPeerOffset != 0) lastKnownPeerOffset = peerReceivedOffset;
                initialSyncLogicProcessed = true;
            }
            break;
        }
        case MSG_TYPE_RESET_CANVAS:
        { 
            Serial.println("收到 MSG_TYPE_RESET_CANVAS.");
            allDrawingHistory.clear(); clearScreenAndCache();     
            relativeBootTimeOffset = 0; iamEffectivelyMoreUptimeDevice = false; iamRequestingAllData = false;
            initialSyncLogicProcessed = false; lastKnownPeerUptime = 0; lastKnownPeerOffset = 0;
            uptimeOfLastPeerSyncedFrom = 0; 
            SyncMessage_t uptimeInfoMsg; uptimeInfoMsg.type = MSG_TYPE_UPTIME_INFO;
            uptimeInfoMsg.senderUptime = localCurrentRawUptime; uptimeInfoMsg.senderOffset = relativeBootTimeOffset;
            memset(&uptimeInfoMsg.touch_data, 0, sizeof(TouchData)); sendSyncMessage(&uptimeInfoMsg);
            Serial.println("  画布已重置。发送了新的 UPTIME_INFO。");
            if (!isScreenOn) hasNewUpdateWhileScreenOff = true; 
            break;
        }
        default:
        {
            Serial.print("收到未知消息类型: "); Serial.println(msg.type);
            break;
        }
        }
    }
}

void replayAllDrawings()
{
    lastRemotePoint = {0, 0, 0}; 
    lastRemoteDrawTime = 0;      

    for (const auto &data : allDrawingHistory) {
        if (data.isReset) { 
            tft.fillScreen(TFT_BLACK); 
            drawMainInterface();       
            lastRemotePoint = {0, 0, 0}; 
            lastRemoteDrawTime = data.timestamp; 
            continue; 
        }
        int mapX = data.x; 
        int mapY = data.y;
        if (data.timestamp - lastRemoteDrawTime > touchInterval || lastRemotePoint.z == 0) { 
            tft.drawPixel(mapX, mapY, data.color); 
        } else { 
            tft.drawLine(lastRemotePoint.x, lastRemotePoint.y, mapX, mapY, data.color); 
        }
        lastRemotePoint = {mapX, mapY, 1}; 
        lastRemoteDrawTime = data.timestamp;   
    }
}

void updateConnectedDevicesCount()
{
    char deviceCountBuffer[10];
    sprintf(deviceCountBuffer, "%d", macSet.size());
    tft.fillRect(SLEEP_BUTTON_X, SLEEP_BUTTON_Y, SLEEP_BUTTON_W, SLEEP_BUTTON_H, TFT_BLUE); 
    tft.setTextColor(TFT_WHITE);                                                            
    tft.setTextSize(1);                                                                     
    tft.setTextFont(2);                                                                     
    String text = String(deviceCountBuffer);
    int textWidth = tft.textWidth(text);
    int textHeight = 8; 
    tft.setCursor(SLEEP_BUTTON_X + (SLEEP_BUTTON_W - textWidth) / 2, SLEEP_BUTTON_Y + (SLEEP_BUTTON_H - textHeight) / 2);
    tft.print(text);
}

void saveScreenArea()
{
    if (savedScreenBuffer == nullptr)
    {
        savedScreenBuffer = new uint16_t[COLOR_SLIDER_WIDTH * (4 * COLOR_SLIDER_HEIGHT)];
    }
    tft.readRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, (4 * COLOR_SLIDER_HEIGHT), savedScreenBuffer);
}

void restoreSavedScreenArea()
{
    if (savedScreenBuffer != nullptr)
    {
        tft.pushImage(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, (4 * COLOR_SLIDER_HEIGHT), savedScreenBuffer);
        delete[] savedScreenBuffer; 
        savedScreenBuffer = nullptr;
    }
}

void clearScreenAndCache() 
{
    tft.fillScreen(TFT_BLACK); 
    drawMainInterface();       
    lastLocalPoint = {0, 0, 0};  
    lastLocalTouchTime = 0;
    lastRemotePoint = {0,0,0};   
    lastRemoteDrawTime = 0;
}

void hideStarButton()
{
    tft.fillRect(SCREEN_WIDTH - COLOR_BUTTON_WIDTH - 4, 4, COLOR_BUTTON_WIDTH, COLOR_BUTTON_HEIGHT, TFT_BLACK);
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

void loop()
{
    handleLocalTouch();        
    processIncomingMessages(); 

    unsigned long currentTimeForLoop = millis();
    if (currentTimeForLoop - lastUptimeInfoBroadcastTime >= UPTIME_INFO_BROADCAST_INTERVAL) {
        SyncMessage_t uptimeMsg;
        uptimeMsg.type = MSG_TYPE_UPTIME_INFO;
        uptimeMsg.senderUptime = currentTimeForLoop; 
        uptimeMsg.senderOffset = relativeBootTimeOffset; // Send current local offset
        memset(&uptimeMsg.touch_data, 0, sizeof(TouchData)); 
        sendSyncMessage(&uptimeMsg);
        lastUptimeInfoBroadcastTime = currentTimeForLoop; 
    }

    if (isScreenOn && !inCustomColorMode && (currentTimeForLoop - lastDebugInfoUpdateTime >= DEBUG_INFO_UPDATE_INTERVAL)) {
        drawDebugInfo();
        lastDebugInfoUpdateTime = currentTimeForLoop;
    }

    if (!isScreenOn && hasNewUpdateWhileScreenOff)
    {
        updateBreathLED(); 
    }
    else
    {
        analogWrite(GREEN_LED, 255); 
    }

    if (!isScreenOn)
    {
        if (macSet.size() > 0)
        {
            analogWrite(BLUE_LED, 255 - BLUE_LED_DIM); 
        }
        else
        {
            analogWrite(BLUE_LED, 255); 
        }
        analogWrite(RED_LED, 255 - RED_LED_DIM); 
    }
    else
    {
        analogWrite(BLUE_LED, 255);
        analogWrite(RED_LED, 255);
    }

    static unsigned long pressStartTime = 0; 

    if (digitalRead(BUTTON_IO0) == LOW) 
    { 
        if (pressStartTime == 0) 
        {
            pressStartTime = millis(); 
        }

        if (millis() - pressStartTime >= 2000) 
        { 
            Serial.println("检测到长按IO0按钮，进入深度睡眠模式...");
            esp_deep_sleep_start(); 
        }
    }
    else 
    { 
        if (pressStartTime > 0 && millis() - pressStartTime < 2000) 
        {
            Serial.println("检测到短按IO0按钮");
            if (isScreenOn) 
            {
                Serial.println("关闭屏幕");
                digitalWrite(TFT_BL, LOW); 
                isScreenOn = false;        
            }
            else 
            {
                Serial.println("打开屏幕");
                digitalWrite(TFT_BL, HIGH); 
                isScreenOn = true;         
                analogWrite(GREEN_LED, 255); 
                hasNewUpdateWhileScreenOff = false; 
                if (!inCustomColorMode) { 
                    drawDebugInfo();
                }
            }
        }
        pressStartTime = 0; 
    }

    unsigned long currentTimeForUI = millis();
    if (currentTimeForUI - lastBroadcastTime >= BROADCAST_INTERVAL) 
    {
        lastBroadcastTime = currentTimeForUI; 
        if (!inCustomColorMode) 
        {
            updateConnectedDevicesCount(); 
        }
    }
}
