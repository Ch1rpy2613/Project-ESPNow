#include "touch_handler.h"
#include "config.h"
#include <Arduino.h> // 用于 millis, delay, Serial, map 等

// 包含依赖模块的头文件
#include "ui_manager.h"       // 用于UI函数和状态 (inCustomColorMode, currentColor 等)
#include "esp_now_handler.h"  // 用于ESP-NOW函数 (sendSyncMessage) 和数据 (allDrawingHistory 等)

// --- 静态 (文件局部) 全局变量，用于触摸处理状态 ---
static TS_Point lastLocalPoint = {0, 0, 0};  // 本地最后一次触摸点坐标
static unsigned long lastLocalTouchTime = 0; // 本地最后一次触摸事件的时间戳

// 彩蛋相关变量，现为本模块局部变量
static unsigned long lastResetTime = 0;
static int resetPressCount = 0;

// --- 函数实现 ---

void touchHandlerInit() {
    // 如果将来需要任何触摸相关的特定初始化，则为占位符
    // 例如：ts.setThreshold(某个值);
}

// 触摸点平均值计算
XY_TouchPoint_t averageXY() { // 返回类型已更改为 XY_TouchPoint_t
    // TS_Point p = ts.getPoint(); // 初始的 getPoint 由于循环似乎是多余的
    bool fly = false;
    int cnt = 0;
    int i, j, k, min_idx, temp_val; // 将 min 重命名为 min_idx，temp 重命名为 temp_val，以避免与潜在函数冲突
    int tmp[2][10]; // 10个样本的缓冲区
    XY_TouchPoint_t resultXY; // 已更改类型

    for (cnt = 0; cnt < 10; cnt++) { // 收集10个样本
        TS_Point p_sample = ts.getPoint();
        if (p_sample.z > 200) { // 检查压力阈值
            tmp[0][cnt] = p_sample.x;
            tmp[1][cnt] = p_sample.y;
            delay(2); // 样本之间的小延迟
        } else {
            fly = true; // 无效触摸 (太轻或无触摸)
            break;
        }
    }

    if (fly || cnt < 7) { // 如果标记为飞点或有效样本不足 (例如，平均中间4个点至少需要4个)
        resultXY.fly = true;
        return resultXY;
    }

    // 对样本进行排序以移除异常值 (对于小N使用简单冒泡排序)
    for (k = 0; k < 2; k++) { // 对于 X (0) 和 Y (1)
        for (i = 0; i < cnt - 1; i++) {
            min_idx = i;
            for (j = i + 1; j < cnt; j++) {
                if (tmp[k][min_idx] > tmp[k][j]) {
                    min_idx = j;
                }
            }
            // 交换
            temp_val = tmp[k][i];
            tmp[k][i] = tmp[k][min_idx];
            tmp[k][min_idx] = temp_val;
        }
    }

    // 平均中间4个样本 (如果cnt为10且已排序0-9，则索引为3,4,5,6)
    // 原始代码使用固定索引。如果cnt < 7，这会是个问题。
    // 假设循环后cnt通常为10。
    // 如果cnt较小，则需要调整或确保在不是飞点的情况下cnt始终为10。
    // 为确保稳健性，我们检查cnt。如果我们采集10个样本且不是飞点，则cnt将为10。
    if (cnt >= 7) { // 确保至少有7个点可供选择中间4个 (例如，如果10个点，则索引为3,4,5,6)
         resultXY.x = (tmp[0][3] + tmp[0][4] + tmp[0][5] + tmp[0][6]) / 4.0f;
         resultXY.y = (tmp[1][3] + tmp[1][4] + tmp[1][5] + tmp[1][6]) / 4.0f;
         resultXY.fly = false;
    } else { // 特定平均方法所需的点数不足
        resultXY.fly = true;
    }
    return resultXY;
}


// 处理本地触摸输入
void handleLocalTouch() {
    // 来自 ui_manager.h 的依赖项:
    // extern bool inCustomColorMode;
    // extern uint32_t currentColor;
    // ... (其他UI相关函数)

    // 来自 esp_now_handler.h 的依赖项:
    // extern std::vector<TouchData_t> allDrawingHistory;
    // extern long relativeBootTimeOffset;
    // ... (其他ESP-NOW相关函数和变量)

    // tft 和 ts 是来自 touch_handler.h 的 extern 变量 (在 .ino 中定义)

    float x1, y1;
    bool touched = ts.tirqTouched() && ts.touched(); // 检查IRQ，然后通过压力确认
    unsigned long currentRawUptime = millis();       // 此触摸事件的本地原始运行时间
    XY_TouchPoint_t xy1;

    if (touched) {
        xy1 = averageXY(); // 获取滤波后的触摸点
        x1 = xy1.x;
        y1 = xy1.y;

        if (!xy1.fly) { // 如果触摸点有效
            // 将原始触摸坐标映射到屏幕坐标
            int mapX = map(x1, TOUCH_MIN_X, TOUCH_MAX_X, 0, SCREEN_WIDTH);
            int mapY = map(y1, TOUCH_MIN_Y, TOUCH_MAX_Y, 0, SCREEN_HEIGHT);

            if (inCustomColorMode) { // 如果处于自定义颜色选择模式
                handleCustomColorTouch(mapX, mapY); // 传递给UI管理器的自定义颜色处理函数
            } else { // 正常绘图/按钮模式
                // 检查复位按钮是否按下
                if (isResetButtonPressed(mapX, mapY)) {
                    if (currentRawUptime - lastResetTime < 1000) { // 检查快速按下 (彩蛋)
                        resetPressCount++;
                    } else {
                        resetPressCount = 1;
                    }
                    lastResetTime = currentRawUptime;

                    if (resetPressCount >= 10) { // 彩蛋触发
                        Serial.println("Kurio Reiko thanks all the recognition and redistribution,");
                        Serial.println("but if someone commercializes this project without declaring Kurio Reiko's originality, then he is a bitch");
                        resetPressCount = 0; // 重置计数器
                    }

                    // 清除本地绘图历史 (allDrawingHistory 是来自 esp_now_handler 的 extern 变量)
                    allDrawingHistory.clear(); 
                    clearScreenAndCache();     // 调用UI管理器的清屏函数

                    // 重置ESP-NOW同步状态变量 (来自 esp_now_handler 的 extern 变量)
                    // 这部分逻辑可能更适合放在 esp_now_handler 内部，
                    // 由 "local_reset" 事件/标志触发。
                    relativeBootTimeOffset = 0; 
                    iamEffectivelyMoreUptimeDevice = false; // 这些现在位于 esp_now_handler 中
                    iamRequestingAllData = false;
                    initialSyncLogicProcessed = false; 

                    // 通过ESP-NOW发送复位消息
                    SyncMessage_t resetMsg;
                    resetMsg.type = MSG_TYPE_RESET_CANVAS;
                    resetMsg.senderUptime = currentRawUptime;
                    resetMsg.senderOffset = relativeBootTimeOffset; // 现在应为0
                    resetMsg.touch_data.isReset = true;
                    resetMsg.touch_data.timestamp = currentRawUptime;
                    resetMsg.touch_data.x = 0; // 与复位无关
                    resetMsg.touch_data.y = 0; // 与复位无关
                    resetMsg.touch_data.color = currentColor; // currentColor 来自 ui_manager
                    sendSyncMessage(&resetMsg); // sendSyncMessage 来自 esp_now_handler
                    
                    // 同时将复位添加到本地历史 (虽然刚被清除，但这标记了复位点)
                    allDrawingHistory.push_back(resetMsg.touch_data); 
                    return; // 操作已处理
                }

                uint32_t selectedColorHolder; // 颜色按钮的临时占位符
                if (isColorButtonPressed(mapX, mapY, selectedColorHolder)) { // 检查标准颜色按钮
                    updateCurrentColor(selectedColorHolder); // 在 ui_manager 中更新颜色
                    redrawStarButton();                      // 用新颜色重绘星星按钮 (ui_manager)
                    return; // 操作已处理
                }

                if (isSleepButtonPressed(mapX, mapY)) { // 检查休眠按钮
                    Serial.println("准备进入深度睡眠模式...");
                    esp_deep_sleep_start(); // ESP32 API 用于深度睡眠
                    return; // 操作已处理 (尽管设备将休眠)
                }

                if (isCustomColorButtonPressed(mapX, mapY)) { // 检查自定义颜色 ("*") 按钮
                    inCustomColorMode = true;   // 设置UI状态 (ui_manager)
                    saveScreenArea();           // 保存屏幕区域 (ui_manager)
                    drawColorSelectors();       // 绘制颜色选择器 (ui_manager)
                    hideStarButton();           // 隐藏星星按钮 (ui_manager)
                    return; // 操作已处理
                }

                // 如果没有按钮被按下，则继续执行绘图逻辑
                if (currentRawUptime - lastLocalTouchTime > TOUCH_STROKE_INTERVAL || lastLocalPoint.z == 0) {
                    // 新的笔划或抬起后的第一个点
                    tft.drawPixel(mapX, mapY, currentColor); // currentColor 来自 ui_manager
                } else {
                    // 继续现有笔划
                    tft.drawLine(lastLocalPoint.x, lastLocalPoint.y, mapX, mapY, currentColor);
                }

                // 更新最后本地触摸点状态
                lastLocalPoint = {mapX, mapY, 1}; // 存储映射后的坐标
                lastLocalTouchTime = currentRawUptime;

                // 创建用于历史记录和发送的触摸数据
                TouchData_t currentDrawPoint;
                currentDrawPoint.x = mapX;
                currentDrawPoint.y = mapY;
                currentDrawPoint.timestamp = currentRawUptime;
                currentDrawPoint.isReset = false;
                currentDrawPoint.color = currentColor; // currentColor 来自 ui_manager
                
                allDrawingHistory.push_back(currentDrawPoint); // 添加到本地历史 (esp_now_handler 的 extern 变量)

                // 准备并通过ESP-NOW发送绘图数据
                SyncMessage_t drawMsg;
                drawMsg.type = MSG_TYPE_DRAW_POINT;
                drawMsg.senderUptime = currentRawUptime;
                drawMsg.senderOffset = relativeBootTimeOffset; // relativeBootTimeOffset 来自 esp_now_handler
                drawMsg.touch_data = currentDrawPoint;
                sendSyncMessage(&drawMsg); // sendSyncMessage 来自 esp_now_handler
            }
        }
    } else { // 当前未检测到触摸
        lastLocalPoint.z = 0; // 标记为无触摸 (压力 = 0)
    }
}
