//2025.1.5更新内容：在右侧添加RGB色彩自定义功能（但是左侧四个颜色按钮暂未删除）
//感谢群友xiao_hj909发布此项更新
//2025.3.23更新内容：启用息屏功能，短按boot键息屏，长按两秒进入深度睡眠。息屏后红色指示灯为电源指示灯，蓝色为连接指示灯，绿色为息屏后远程更新指示灯。数值可以自由调整，均使用pwm调光，可调节亮度。
//感谢群友2093416185（shapaper@126.com）发布此项更新
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>
#include <esp_now.h>
#include <WiFi.h>
#include <queue>
#include <set>
#include <vector>

// 定义触摸屏引脚
#define XPT2046_IRQ 36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK 25
#define XPT2046_CS 33

// 定义 IO0 按钮引脚
#define BUTTON_IO0 0 // GPIO 0
#define TFT_BL 21 //GPIO 21 用于控制背光
#define GREEN_LED 16 //GPIO 16 用于控制绿色LED
#define BLUE_LED 17  // 蓝色LED
#define RED_LED 22   // 红色LED
#define BATTERY_PIN 34 // GPIO 34，用于读取电池电压

// 校准参数
#define TOUCH_MIN_X 200
#define TOUCH_MAX_X 3700
#define TOUCH_MIN_Y 300
#define TOUCH_MAX_Y 3800

// Reset 按钮位置和大小
#define RESET_BUTTON_X 4
#define RESET_BUTTON_Y 4  // 调整为 4
#define RESET_BUTTON_W 28  // 收窄宽度
#define RESET_BUTTON_H 10

// 颜色按钮位置和大小（收窄并靠左）
#define COLOR_BUTTON_WIDTH 15   // 收窄宽度
#define COLOR_BUTTON_HEIGHT 10   // 高度
#define COLOR_BUTTON_START_Y (RESET_BUTTON_Y + RESET_BUTTON_H + 2) // 将起始Y坐标调整为更紧凑
#define COLOR_BUTTON_SPACING 2   // 按钮间距

// 新按钮 P 的位置和大小
#define SLEEP_BUTTON_X RESET_BUTTON_X
#define SLEEP_BUTTON_Y (COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * 4) // 在颜色按钮下方
#define SLEEP_BUTTON_W 10  // 按钮宽度
#define SLEEP_BUTTON_H 10   // 按钮高度

// 自定义颜色按钮的位置和大小
#define CUSTOM_COLOR_BUTTON_X (SCREEN_WIDTH - COLOR_BUTTON_WIDTH - 4)
#define CUSTOM_COLOR_BUTTON_Y 4
#define CUSTOM_COLOR_BUTTON_W COLOR_BUTTON_WIDTH  // 同样宽度
#define CUSTOM_COLOR_BUTTON_H COLOR_BUTTON_HEIGHT  // 同样高度

// 返回按钮的位置和大小
#define BACK_BUTTON_X (SCREEN_WIDTH - COLOR_BUTTON_WIDTH - 4)
#define BACK_BUTTON_Y (SCREEN_HEIGHT - COLOR_BUTTON_HEIGHT - 4)
#define BACK_BUTTON_W COLOR_BUTTON_WIDTH  // 同样宽度
#define BACK_BUTTON_H COLOR_BUTTON_HEIGHT  // 同样高度

// 屏幕宽高
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

// 手动定义 TFT_GRAY
#define TFT_GRAY 0x8410

// 定义 ESP-NOW 的数据结构
typedef struct {
    int x;
    int y;
    unsigned long timestamp;
    bool isReset;            // 是否是 Reset 命令
    bool isLocalDevice;      // 标识发送设备
    uint32_t color;          // 画笔颜色
} TouchData;

typedef struct{
  float x;
  float y;
  bool fly=false;
} XY_structure;

// 添加呼吸灯相关变量
int breathBrightness = 0;        // 呼吸灯亮度
int breathDirection = 5;         // 呼吸灯亮度变化方向和速度
unsigned long lastBreathTime = 0; // 上次更新呼吸灯时间
bool hasNewUpdateWhileScreenOff = false;  // 标记息屏后是否有新的更新

// 创建 SPI 和触摸屏对象
SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

// 全局变量
std::queue<TouchData> remoteQueue; // 远程触摸点队列
std::set<String> macSet; // 存储接收到的 MAC 地址
TS_Point lastLocalPoint = {0, 0, 0}; // 本地上一次触摸点
TS_Point lastRemotePoint = {0, 0, 0}; // 远程上一次触摸点
unsigned long lastLocalTouchTime = 0;
unsigned long lastRemoteTime = 0;
unsigned long touchInterval = 50;

// ESP-NOW 广播地址
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// 当前画笔颜色
uint32_t currentColor = TFT_BLUE; // 默认颜色为蓝色

// 彩蛋相关变量
unsigned long lastResetTime = 0;
int resetPressCount = 0;

// 定时广播间隔
#define BROADCAST_INTERVAL 2000 // 每2秒广播一次
unsigned long lastBroadcastTime = 0;

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
uint16_t* savedScreenBuffer = nullptr;

// 保存远程绘制数据
std::vector<TouchData> remoteDrawings;

//布尔类型存储是否息屏 true 是开
bool isScreenOn = true;

//息屏提示灯 红色是电源指示灯 蓝色是连接指示灯
const int BLUE_LED_DIM = 14;  // 10% 亮度 (255 * 0.1 ≈ 25)
const int RED_LED_DIM = 4;   // 5% 亮度 (255 * 0.05 ≈ 13)

void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_IO0, INPUT_PULLUP); // 使用内部上拉电阻
    pinMode(BATTERY_PIN, INPUT); // 设置电池引脚为输入

    pinMode(GREEN_LED, OUTPUT);
    analogWriteResolution(GREEN_LED, 8); // 设置为8位分辨率(0-255)
    analogWriteFrequency(GREEN_LED, 5000); // 设置PWM频率为5KHz
    analogWrite(GREEN_LED, 255); // 初始状态设为关闭

    pinMode(BLUE_LED, OUTPUT);
    pinMode(RED_LED, OUTPUT);
    analogWriteResolution(BLUE_LED, 8);
    analogWriteResolution(RED_LED, 8);
    analogWriteFrequency(BLUE_LED, 5000);
    analogWriteFrequency(RED_LED, 5000);
    analogWrite(BLUE_LED, 255); // 初始状态关闭
    analogWrite(RED_LED, 255);  // 初始状态关闭

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

    drawMainInterface();
}

// 呼吸灯更新函数
void updateBreathLED() {
    unsigned long currentTime = millis();
    
    // 每10毫秒更新一次亮度
    if (currentTime - lastBreathTime >= 10) {
        lastBreathTime = currentTime;
        
        // 更新亮度
        breathBrightness += breathDirection;
        
        // 当达到最大或最小亮度时，改变方向
        if (breathBrightness >= 255 || breathBrightness <= 0) {
            breathDirection = -breathDirection;
            breathBrightness = constrain(breathBrightness, 0, 255);
        }
        
        // 输出PWM信号到LED
        analogWrite(GREEN_LED, 255 - breathBrightness);
    }
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

// 绘制主界面
void drawMainInterface() {
    tft.fillScreen(TFT_BLACK);
    drawResetButton();
    drawColorButtons();
    drawSleepButton();
    drawCustomColorButton();
    updateConnectedDevicesCount();
    drawStarButton(); // 添加星号按钮
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
    uint32_t colors[] = {TFT_BLUE, TFT_GREEN, TFT_RED, TFT_YELLOW};
    
    // 绘制颜色按钮
    for (int i = 0; i < 4; i++) {
        int buttonY = COLOR_BUTTON_START_Y + (COLOR_BUTTON_HEIGHT + COLOR_BUTTON_SPACING) * i;
        tft.fillRect(RESET_BUTTON_X, buttonY, COLOR_BUTTON_WIDTH, COLOR_BUTTON_HEIGHT, colors[i]);
    }
}

// 绘制 Sleep 按钮
void drawSleepButton() {
    tft.fillRect(SLEEP_BUTTON_X, SLEEP_BUTTON_Y, SLEEP_BUTTON_W, SLEEP_BUTTON_H, TFT_BLUE);
    char deviceCountBuffer[10];
    sprintf(deviceCountBuffer, "%d", macSet.size());
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1); // 设置字体大小为 1
    tft.setTextFont(2); // 设置字体为较大字体
    String text = String(deviceCountBuffer);
    int textWidth = tft.textWidth(text);
    int textHeight = 8; // 文字高度近似值
    tft.setCursor(SLEEP_BUTTON_X + (SLEEP_BUTTON_W - textWidth) / 2, SLEEP_BUTTON_Y + (SLEEP_BUTTON_H - textHeight) / 2);
    tft.print(text);
}

// 绘制 Custom Color 按钮
void drawCustomColorButton() {
    tft.fillRect(CUSTOM_COLOR_BUTTON_X, CUSTOM_COLOR_BUTTON_Y, CUSTOM_COLOR_BUTTON_W, CUSTOM_COLOR_BUTTON_H, currentColor);
}

// 绘制 Star 按钮
void drawStarButton() {
    tft.fillRect(SCREEN_WIDTH - COLOR_BUTTON_WIDTH - 4, 4, COLOR_BUTTON_WIDTH, COLOR_BUTTON_HEIGHT, currentColor);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1); // 设置字体大小为 1
    tft.setTextFont(2); // 设置字体为较大字体
    tft.setCursor(SCREEN_WIDTH - COLOR_BUTTON_WIDTH - 4 + 1, 4 + 1);
    tft.print("*"); // 添加星号
}

// 接收数据回调函数
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingDataPtr, int len) {
    TouchData incomingData;
    memcpy(&incomingData, incomingDataPtr, sizeof(incomingData));

    // 存储接收到的 MAC 地址
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", 
             info->src_addr[0], info->src_addr[1], info->src_addr[2], 
             info->src_addr[3], info->src_addr[4], info->src_addr[5]);
    macSet.insert(String(macStr));

    if (incomingData.isReset) {
        // 如果是 Reset 命令，清空屏幕并重置状态
        clearScreenAndCache();
    } else {
        // 普通触摸数据加入队列
        remoteQueue.push(incomingData);
        //点亮LED绿灯
        if (!isScreenOn) {
            hasNewUpdateWhileScreenOff = true;
            Serial.println("remote update and screen is closed, start breath effect");
        }
    }
}

// 发送触摸数据
void sendTouchData(TouchData data) {
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)&data, sizeof(data));
    if (result != ESP_OK) {
        Serial.println("Error sending data");
    }
}

// 广播 MAC 地址
void broadcastMacAddress() {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", 
             broadcastAddress[0], broadcastAddress[1], broadcastAddress[2], 
             broadcastAddress[3], broadcastAddress[4], broadcastAddress[5]);
    esp_now_send(nullptr, (uint8_t *)macStr, strlen(macStr));
}

// 检查是否点击了 Reset 按钮
bool isResetButtonPressed(int x, int y) {
    return x >= RESET_BUTTON_X && x <= RESET_BUTTON_X + RESET_BUTTON_W &&
           y >= RESET_BUTTON_Y && y <= RESET_BUTTON_Y + RESET_BUTTON_H;
}

// 检查是否点击了颜色按钮
bool isColorButtonPressed(int x, int y, uint32_t &color) {
    uint32_t colors[] = {TFT_BLUE, TFT_GREEN, TFT_RED, TFT_YELLOW};
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

XY_structure averageXY(void){
  TS_Point p = ts.getPoint();
  bool fly = false;
  int cnt = 0; 
  int i,j,k,min,temp;
  int tmp[2][10];
  XY_structure XY;
  for(cnt = 0;cnt <= 9;cnt++){
    TS_Point p = ts.getPoint();
    if (p.z > 200){
      tmp[0][cnt] = p.x;
      tmp[1][cnt] = p.y;
      delay(2);
    }
    else 
    {fly = true;
      break;
    }
  }
  if(fly){
    XY.fly = fly;
    return XY;
  }
  for(k=0; k<2; k++)
  { // 降序排列
    for(i=0; i<cnt-1; i++)
    {
      min=i;
      for (j=i+1; j<cnt; j++)
      {
        if (tmp[k][min] > tmp[k][j]) min=j;
      }
        temp = tmp[k][i];
        tmp[k][i] = tmp[k][min];
        tmp[k][min] = temp;
        }
    
    
    }
  XY.x = (tmp[0][3]+tmp[0][4]+tmp[0][5]+tmp[0][6]) / 4;
  XY.y = (tmp[1][3]+tmp[1][4]+tmp[1][5]+tmp[1][6]) / 4;
  return XY;
}

// 检查是否点击了新按钮 P
bool isSleepButtonPressed(int x, int y) {
    return x >= SLEEP_BUTTON_X && x <= SLEEP_BUTTON_X + SLEEP_BUTTON_W &&
           y >= SLEEP_BUTTON_Y && y <= SLEEP_BUTTON_Y + SLEEP_BUTTON_H;
}

// 检查是否点击了自定义颜色按钮
bool isCustomColorButtonPressed(int x, int y) {
    return x >= CUSTOM_COLOR_BUTTON_X && x <= CUSTOM_COLOR_BUTTON_X + CUSTOM_COLOR_BUTTON_W &&
           y >= CUSTOM_COLOR_BUTTON_Y && y <= CUSTOM_COLOR_BUTTON_Y + CUSTOM_COLOR_BUTTON_H;
}

// 检查是否点击了返回按钮
bool isBackButtonPressed(int x, int y) {
    return x >= BACK_BUTTON_X && x <= BACK_BUTTON_X + BACK_BUTTON_W &&
           y >= BACK_BUTTON_Y && y <= BACK_BUTTON_Y + BACK_BUTTON_H;
}

// 处理本地触摸绘制
void handleLocalTouch() {
    float x1, y1;
    bool touched = ts.tirqTouched() && ts.touched();
    unsigned long currentTime = millis();
    XY_structure xy1;
    if (touched) {
        xy1 = averageXY();
        x1 = xy1.x;
        y1 = xy1.y;
        // 映射触摸点坐标到屏幕范围
        if (!xy1.fly) {
            int mapX = map(x1, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH);
            int mapY = map(y1, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT);
            char buffer[50];
            sprintf(buffer, "x1:%.2f,y1:%.2f", x1, y1);
            Serial.println(buffer);

            if (inCustomColorMode) {
                // 处理调色界面中的触摸事件
                handleCustomColorTouch(mapX, mapY);
            } else {
                // 检查是否点击了 Reset 按钮
                if (isResetButtonPressed(mapX, mapY)) {
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
                    clearScreenAndCache();

                    return; // 不再处理其他触摸
                }

                // 检查是否点击了颜色按钮
                if (isColorButtonPressed(mapX, mapY, currentColor)) {
                    // 更新当前颜色
                    updateCurrentColor(currentColor);
                    // 发送颜色数据到远程设备
                    TouchData data = {0, 0, millis(), false, true, currentColor};
                    sendTouchData(data);
                    redrawStarButton(); // 更新星号按钮颜色
                    return; // 不再处理其他触摸
                }

                // 检查是否点击了新按钮 P
                if (isSleepButtonPressed(mapX, mapY)) {
                    Serial.println("Entering deep sleep mode...");
                    esp_deep_sleep_start();  // 进入深度睡眠模式
                    return; // 不再处理其他触摸
                }

                // 检查是否点击了自定义颜色按钮
                if (isCustomColorButtonPressed(mapX, mapY)) {
                    inCustomColorMode = true;
                    applyRemoteDrawings();
                    saveScreenArea();
                    drawColorSelectors();
                    hideStarButton(); // 隐藏星号按钮
                    return; // 不再处理其他触摸
                }

                // 判断是否是新笔画（时间间隔）
                if (currentTime - lastLocalTouchTime > touchInterval) {
                    tft.drawPixel(mapX, mapY, currentColor);
                } else {
                    // 使用插值优化笔迹平滑度
                    int steps = 5; // 插值步数，可以根据需要调整
                    for (int i = 1; i <= steps; i++) {
                        float t = i / (float)steps;
                        int interpX = lastLocalPoint.x + (mapX - lastLocalPoint.x) * t;
                        int interpY = lastLocalPoint.y + (mapY - lastLocalPoint.y) * t;
                        tft.drawPixel(interpX, interpY, currentColor);
                    }
                }

                // 更新本地触摸点和时间
                lastLocalPoint = {mapX, mapY, 1};
                lastLocalTouchTime = currentTime;

                // 创建触摸数据结构并发送
                TouchData data = {x1, y1, currentTime, false, true, currentColor};
                sendTouchData(data);
            }
        }
    } else {
        lastLocalPoint.z = 0;
    }
}

// 处理调色界面中的触摸事件
void handleCustomColorTouch(int x, int y) {
    if (x >= (SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4) && x <= SCREEN_WIDTH) {
        if (y >= 0 && y <= COLOR_SLIDER_HEIGHT) {
            // 红色进度条
            updateSingleColorSlider(y, TFT_RED, redValue);
        } else if (y >= COLOR_SLIDER_HEIGHT && y <= 2 * COLOR_SLIDER_HEIGHT) {
            // 绿色进度条
            updateSingleColorSlider(y - COLOR_SLIDER_HEIGHT, TFT_GREEN, greenValue);
        } else if (y >= 2 * COLOR_SLIDER_HEIGHT && y <= 3 * COLOR_SLIDER_HEIGHT) {
            // 蓝色进度条
            updateSingleColorSlider(y - 2 * COLOR_SLIDER_HEIGHT, TFT_BLUE, blueValue);
        } else if (isBackButtonPressed(x, y)) {
            // 返回按钮
            closeColorSelectors();
            return;
        }
        refreshAllColorSliders();
        updateCustomColorPreview();
        updateCurrentColor(tft.color565(redValue, greenValue, blueValue)); // 更新当前颜色但不刷新 *
    }
}

// 更新单个颜色进度条
void updateSingleColorSlider(int y, uint32_t color, int &value) {
    value = constrain(map(y, 0, COLOR_SLIDER_HEIGHT, 0, 255), 0, 255); // 确保值在 0 到 255 之间
}

// 绘制颜色选择器
void drawColorSelectors() {
    // 绘制红色进度条
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_RED);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, redValue * (COLOR_SLIDER_HEIGHT / 255.0), TFT_RED);

    // 绘制绿色进度条
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_GREEN);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, greenValue * (COLOR_SLIDER_HEIGHT / 255.0), TFT_GREEN);

    // 绘制蓝色进度条
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_BLUE);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, blueValue * (COLOR_SLIDER_HEIGHT / 255.0), TFT_BLUE);

    // 绘制返回按钮
    tft.fillRect(BACK_BUTTON_X, BACK_BUTTON_Y, BACK_BUTTON_W, BACK_BUTTON_H, TFT_GRAY);

    // 更新颜色预览
    updateCustomColorPreview();
}

// 更新颜色预览
void updateCustomColorPreview() {
    uint32_t previewColor = tft.color565(redValue, greenValue, blueValue);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 3 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, previewColor);
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 3 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_WHITE);
}

// 刷新所有颜色进度条
void refreshAllColorSliders() {
    // 清除原有颜色选择器区域
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, 4 * COLOR_SLIDER_HEIGHT, TFT_BLACK);

    // 绘制红色进度条
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_RED);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, redValue * (COLOR_SLIDER_HEIGHT / 255.0), TFT_RED);

    // 绘制绿色进度条
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_GREEN);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, greenValue * (COLOR_SLIDER_HEIGHT / 255.0), TFT_GREEN);

    // 绘制蓝色进度条
    tft.drawRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, COLOR_SLIDER_HEIGHT, TFT_BLUE);
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 2 * COLOR_SLIDER_HEIGHT, COLOR_SLIDER_WIDTH, blueValue * (COLOR_SLIDER_HEIGHT / 255.0), TFT_BLUE);

    // 更新颜色预览
    updateCustomColorPreview();
}

// 关闭颜色选择器
void closeColorSelectors() {
    inCustomColorMode = false;
    // 清除颜色选择器区域
    tft.fillRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, 4 * COLOR_SLIDER_HEIGHT, TFT_BLACK);
    // 更新当前颜色
    currentColor = tft.color565(redValue, greenValue, blueValue);
    drawCustomColorButton();
    restoreSavedScreenArea();
    redrawRemoteDrawings();
    showStarButton(); // 显示星号按钮
    drawStarButton(); // 确保星号按钮重新绘制
}

// 更新当前颜色
void updateCurrentColor(uint32_t newColor) {
    currentColor = newColor;
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
            // 使用插值优化笔迹平滑度
            int steps = 5; // 插值步数，可以根据需要调整
            for (int i = 1; i <= steps; i++) {
                float t = i / (float)steps;
                int interpX = lastRemotePoint.x + (mapX - lastRemotePoint.x) * t;
                int interpY = lastRemotePoint.y + (mapY - lastRemotePoint.y) * t;
                tft.drawPixel(interpX, interpY, data.color);
            }
        }

        // 更新远程触摸点和时间
        lastRemotePoint = {mapX, mapY, 1};
        lastRemoteTime = data.timestamp;

        // 保存远程绘制数据
        remoteDrawings.push_back(data);
    }
}

// 更新连接的设备数量
void updateConnectedDevicesCount() {
    char deviceCountBuffer[10];
    sprintf(deviceCountBuffer, "%d", macSet.size());
    tft.fillRect(SLEEP_BUTTON_X, SLEEP_BUTTON_Y, SLEEP_BUTTON_W, SLEEP_BUTTON_H, TFT_BLUE);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1); // 设置字体大小为 1
    tft.setTextFont(2); // 设置字体为较大字体
    String text = String(deviceCountBuffer);
    int textWidth = tft.textWidth(text);
    int textHeight = 8; // 文字高度近似值
    tft.setCursor(SLEEP_BUTTON_X + (SLEEP_BUTTON_W - textWidth) / 2, SLEEP_BUTTON_Y + (SLEEP_BUTTON_H - textHeight) / 2);
    tft.print(text);
}

// 保存屏幕区域
void saveScreenArea() {
    if (savedScreenBuffer == nullptr) {
        savedScreenBuffer = new uint16_t[COLOR_SLIDER_WIDTH * 4 * SCREEN_HEIGHT];
    }
    tft.readRect(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, 4 * COLOR_SLIDER_HEIGHT, savedScreenBuffer);
}

// 恢复保存的屏幕区域
void restoreSavedScreenArea() {
    if (savedScreenBuffer != nullptr) {
        tft.pushImage(SCREEN_WIDTH - COLOR_SLIDER_WIDTH - 4, 0, COLOR_SLIDER_WIDTH, 4 * COLOR_SLIDER_HEIGHT, savedScreenBuffer);
        delete[] savedScreenBuffer;
        savedScreenBuffer = nullptr;
    }
}

// 应用远程绘制的数据
void applyRemoteDrawings() {
    for (const auto& data : remoteDrawings) {
        // 映射远程坐标到屏幕范围
        int mapX = map(data.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH);
        int mapY = map(data.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT);

        // 判断是否是新笔画（时间间隔）
        if (data.timestamp - lastRemoteTime > touchInterval) {
            tft.drawPixel(mapX, mapY, data.color);
        } else {
            // 使用插值优化笔迹平滑度
            int steps = 5; // 插值步数，可以根据需要调整
            for (int i = 1; i <= steps; i++) {
                float t = i / (float)steps;
                int interpX = lastRemotePoint.x + (mapX - lastRemotePoint.x) * t;
                int interpY = lastRemotePoint.y + (mapY - lastRemotePoint.y) * t;
                tft.drawPixel(interpX, interpY, data.color);
            }
        }

        // 更新远程触摸点和时间
        lastRemotePoint = {mapX, mapY, 1};
        lastRemoteTime = data.timestamp;
    }
}

// 重新绘制远程绘制的数据
void redrawRemoteDrawings() {
    for (const auto& data : remoteDrawings) {
        // 映射远程坐标到屏幕范围
        int mapX = map(data.x, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH);
        int mapY = map(data.y, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT);

        // 判断是否是新笔画（时间间隔）
        if (data.timestamp - lastRemoteTime > touchInterval) {
            tft.drawPixel(mapX, mapY, data.color);
        } else {
            // 使用插值优化笔迹平滑度
            int steps = 5; // 插值步数，可以根据需要调整
            for (int i = 1; i <= steps; i++) {
                float t = i / (float)steps;
                int interpX = lastRemotePoint.x + (mapX - lastRemotePoint.x) * t;
                int interpY = lastRemotePoint.y + (mapY - lastRemotePoint.y) * t;
                tft.drawPixel(interpX, interpY, data.color);
            }
        }

        // 更新远程触摸点和时间
        lastRemotePoint = {mapX, mapY, 1};
        lastRemoteTime = data.timestamp;
    }
}

// 清除屏幕和缓存
void clearScreenAndCache() {
    tft.fillScreen(TFT_BLACK);
    drawMainInterface();
    lastLocalPoint = {0, 0, 0};
    lastLocalTouchTime = 0;
    remoteDrawings.clear(); // 清除远程绘制数据
}

// 隐藏星号按钮
void hideStarButton() {
    tft.fillRect(SCREEN_WIDTH - COLOR_BUTTON_WIDTH - 4, 4, COLOR_BUTTON_WIDTH, COLOR_BUTTON_HEIGHT, TFT_BLACK);
}

// 显示星号按钮
void showStarButton() {
    drawStarButton();
}

// 重新绘制星号按钮
void redrawStarButton() {
    hideStarButton();
    drawStarButton();
}

void loop() {
    handleLocalTouch();
    handleRemoteTouch();

    // 检测 IO0 按钮是否被按下
    //if (digitalRead(BUTTON_IO0) == LOW) {
    //     Serial.println("Entering deep sleep mode...");
    //     esp_deep_sleep_start();  // 进入深度睡眠模式
    // }

    // 如果屏幕关闭且有新的更新，显示呼吸灯效果
    if (!isScreenOn && hasNewUpdateWhileScreenOff) {
        updateBreathLED();
    } else {
        analogWrite(GREEN_LED, 255); // 关闭LED
    }

    // 红色电源知识点和蓝色连接指示灯
    if (!isScreenOn) {
        // 如果有设备连接，点亮蓝色LED
        if (macSet.size() > 0) {
            analogWrite(BLUE_LED, 255 - BLUE_LED_DIM); // 10%亮度
        } else {
            analogWrite(BLUE_LED, 255); // 关闭蓝色LED
        }
        
        // 息屏时点亮红色LED
        analogWrite(RED_LED, 255 - RED_LED_DIM); // 5%亮度
    } else {
        // 屏幕开启时关闭所有指示LED
        analogWrite(BLUE_LED, 255);
        analogWrite(RED_LED, 255);
    }

    static unsigned long pressStartTime = 0; // 记录按下时间

    if (digitalRead(BUTTON_IO0) == LOW) { // 检测按键按下
        if (pressStartTime == 0) {
            pressStartTime = millis();  // 记录按下的时间
        }

        if (millis() - pressStartTime >= 2000) { // 长按超过 2 秒
            Serial.println("Entering deep sleep mode...");
            esp_deep_sleep_start();
        }
    } else { // 按键释放
        if (pressStartTime > 0 && millis() - pressStartTime < 2000) {
            Serial.println("short click buttom");
            // 在这里写你的普通功能
            if (isScreenOn) {
              Serial.println("close screen");
              digitalWrite(TFT_BL, LOW);  // 关闭屏幕
              isScreenOn=false;
            } else {
              Serial.println("open screen");
              digitalWrite(TFT_BL, HIGH);  // 开启屏幕
              analogWrite(GREEN_LED, 255);
              hasNewUpdateWhileScreenOff = false;
              isScreenOn=true;
            }
        }
        pressStartTime = 0; // 复位计时
    }

    // 定时广播 MAC 地址
    unsigned long currentTime = millis();
    if (currentTime - lastBroadcastTime >= BROADCAST_INTERVAL) {
        broadcastMacAddress();
        lastBroadcastTime = currentTime;

        // 更新设备数量显示
        if (!inCustomColorMode) {
            updateConnectedDevicesCount(); // 更新连接设备数量
        }
        

        Serial.print("Connected devices: ");
        Serial.println(macSet.size());
    }
}
