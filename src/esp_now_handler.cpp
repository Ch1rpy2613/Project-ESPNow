#include "esp_now_handler.h"
#include "config.h"  // 包含项目配置常量
#include <Arduino.h> // For Serial, millis, etc.
#include <cstring>   // For memcpy, memset, snprintf

// TFT_eSPI tft 对象和 drawMainInterface 函数在 Project-ESPNow.ino 中定义
// 通过 extern 声明来在此文件中使用它们
extern TFT_eSPI tft;
extern void drawMainInterface(); // 用于清屏后重绘UI骨架
// 如果 clearScreenAndCache 也需要从这里调用，也需要 extern
extern void clearScreenAndCache();

// 定义在 esp_now_handler.h 中声明的全局变量
esp_now_peer_info_t broadcastPeerInfo;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // ESP-NOW 广播地址
std::queue<SyncMessage_t> incomingMessageQueue;                    // ESP-NOW 接收消息队列
std::vector<TouchData_t> allDrawingHistory;                        // 所有绘图操作的历史记录
std::set<String> macSet;                                           // 已发现的对端设备 MAC 地址

unsigned long lastKnownPeerUptime = 0;
long lastKnownPeerOffset = 0;
uint8_t lastPeerMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
bool initialSyncLogicProcessed = false;
bool iamEffectivelyMoreUptimeDevice = false;
bool iamRequestingAllData = false;
long relativeBootTimeOffset = 0;
unsigned long uptimeOfLastPeerSyncedFrom = 0;
unsigned long timeRequestSentForAllDrawings = 0; // 新增：记录请求所有绘图数据的时间戳

// 触摸点处理相关 (用于远程点绘制)
TS_Point lastRemotePoint = {0, 0, 0}; // 远程最后一点
unsigned long lastRemoteDrawTime = 0; // 远程最后绘制时间
// unsigned long touchInterval = 50;     // 触摸笔划间隔阈值 (毫秒) -> 已移至 config.h 作为 TOUCH_STROKE_INTERVAL

// ESP-NOW 初始化函数
void espNowInit()
{
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("错误：ESP-NOW 初始化失败");
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
        esp_now_del_peer(broadcastPeerInfo.peer_addr); // 尝试删除后重新添加
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
}

// ESP-NOW 数据发送回调函数
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

// ESP-NOW 数据接收回调函数
void OnSyncDataRecv(const esp_now_recv_info *info, const uint8_t *incomingDataPtr, int len)
{
    if (len == sizeof(SyncMessage_t))
    {
        SyncMessage_t receivedMsg;
        memcpy(&receivedMsg, incomingDataPtr, sizeof(receivedMsg));
        memcpy(lastPeerMac, info->src_addr, 6); // 更新最后通信的对端 MAC

        char macStr[18];
        snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                 info->src_addr[0], info->src_addr[1], info->src_addr[2],
                 info->src_addr[3], info->src_addr[4], info->src_addr[5]);
        macSet.insert(String(macStr)); // 添加到 MAC 地址集合中用于计数

        incomingMessageQueue.push(receivedMsg); // 将消息放入队列等待处理
    }
    else if (len == strlen("XX:XX:XX:XX:XX:XX") && incomingDataPtr[0] != '{')
    {
        // 处理旧版或特定的 MAC 地址广播 (如果项目中有这种逻辑)
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

// 发送同步消息的辅助函数
void sendSyncMessage(const SyncMessage_t *msg)
{
    // 调用前应确保 msg->senderUptime 和 msg->senderOffset 已正确设置
    esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)msg, sizeof(SyncMessage_t));
    if (result != ESP_OK)
    {
        Serial.print("发送 SyncMessage 类型 ");
        Serial.print(msg->type);
        Serial.print(" 错误: ");
        Serial.println(esp_err_to_name(result));
    }
}

// 处理接收到的消息队列
void processIncomingMessages()
{
    // 声明外部变量/函数，如果它们在 .ino 或其他模块中定义
    extern bool isScreenOn;                 // 来自主 .ino 或 power_manager
    extern bool hasNewUpdateWhileScreenOff; // 来自主 .ino 或 power_manager

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
            if (uptimeOfLastPeerSyncedFrom != 0 && !iamRequestingAllData)
            {
                unsigned long diffFromLastSyncSource = (peerRawUptime > uptimeOfLastPeerSyncedFrom) ? (peerRawUptime - uptimeOfLastPeerSyncedFrom) : (uptimeOfLastPeerSyncedFrom - peerRawUptime);
                if (diffFromLastSyncSource < MIN_UPTIME_DIFF_FOR_NEW_SYNC_TARGET)
                {
                    Serial.print("  迟滞判断: 对端原始运行时间 (");
                    Serial.print(peerRawUptime);
                    Serial.print(") 与上次同步源的原始运行时间 (");
                    Serial.print(uptimeOfLastPeerSyncedFrom);
                    Serial.print(") 差异 ");
                    Serial.print(diffFromLastSyncSource);
                    Serial.print("ms < ");
                    Serial.print(MIN_UPTIME_DIFF_FOR_NEW_SYNC_TARGET);
                    Serial.println("ms。跳过对此对端的完整同步评估。");
                    if (lastKnownPeerUptime != peerRawUptime || lastKnownPeerOffset != peerReceivedOffset)
                    {
                        lastKnownPeerUptime = peerRawUptime;
                        lastKnownPeerOffset = peerReceivedOffset;
                        initialSyncLogicProcessed = false;
                    }
                    if (!initialSyncLogicProcessed)
                        initialSyncLogicProcessed = true;
                    break;
                }
            }

            if (lastKnownPeerUptime != peerRawUptime || lastKnownPeerOffset != peerReceivedOffset || !initialSyncLogicProcessed)
            {
                lastKnownPeerUptime = peerRawUptime;
                lastKnownPeerOffset = peerReceivedOffset;

                Serial.println("  处理 UPTIME_INFO 进行同步决策。");
                Serial.print("  本地有效运行时间: ");
                Serial.print(localEffectiveUptime);
                Serial.print(" (原始: ");
                Serial.print(localCurrentRawUptime);
                Serial.print(", 偏移: ");
                Serial.print(localCurrentOffset);
                Serial.println(")");
                Serial.print("  对端有效运行时间: ");
                Serial.print(peerEffectiveUptime);
                Serial.print(" (原始: ");
                Serial.print(peerRawUptime);
                Serial.print(", 偏移: ");
                Serial.print(peerReceivedOffset);
                Serial.println(")");

                if (localEffectiveUptime == peerEffectiveUptime)
                {
                    uint8_t myMacAddr[6];
                    esp_wifi_get_mac(WIFI_IF_STA, myMacAddr);
                    if (memcmp(myMacAddr, lastPeerMac, 6) < 0)
                    {
                        Serial.println("  决策 (有效运行时间相同, MAC较小): 本机行为类似较新设备：清空并请求数据。");
                        iamEffectivelyMoreUptimeDevice = false;
                        iamRequestingAllData = true;
                        allDrawingHistory.clear();
                        tft.fillScreen(TFT_BLACK);
                        drawMainInterface();
                        SyncMessage_t requestMsg;
                        requestMsg.type = MSG_TYPE_REQUEST_ALL_DRAWINGS;
                        requestMsg.senderUptime = localCurrentRawUptime;
                        requestMsg.senderOffset = localCurrentOffset;
                        memset(&requestMsg.touch_data, 0, sizeof(TouchData_t));
                        sendSyncMessage(&requestMsg);
                        timeRequestSentForAllDrawings = millis(); // 记录发送请求的时间
                    }
                    else
                    {
                        Serial.println("  决策 (有效运行时间相同, MAC较大/相等): 本机行为类似较旧设备：通知对端清空并请求数据。");
                        iamEffectivelyMoreUptimeDevice = true;
                        iamRequestingAllData = false;
                        SyncMessage_t promptMsg;
                        promptMsg.type = MSG_TYPE_CLEAR_AND_REQUEST_UPDATE;
                        promptMsg.senderUptime = localCurrentRawUptime;
                        promptMsg.senderOffset = localCurrentOffset;
                        memset(&promptMsg.touch_data, 0, sizeof(TouchData_t));
                        sendSyncMessage(&promptMsg);
                    }
                }
                else
                {
                    unsigned long effectiveUptimeDifference = (localEffectiveUptime > peerEffectiveUptime) ? (localEffectiveUptime - peerEffectiveUptime) : (peerEffectiveUptime - localEffectiveUptime);

                    if (effectiveUptimeDifference <= EFFECTIVE_UPTIME_SYNC_THRESHOLD)
                    {
                        Serial.println("  决策: 有效运行时间差在阈值内。不启动新的同步以避免抖动。");
                    }
                    else if (localEffectiveUptime < peerEffectiveUptime)
                    {
                        Serial.println("  决策: 本机有效运行时间较短 (超出阈值)。判定本机为较新设备。清空本机数据并向对端 (较旧设备) 请求所有绘图数据。");
                        iamEffectivelyMoreUptimeDevice = false;
                        iamRequestingAllData = true;
                        allDrawingHistory.clear();
                        tft.fillScreen(TFT_BLACK);
                        drawMainInterface();
                        SyncMessage_t requestMsg;
                        requestMsg.type = MSG_TYPE_REQUEST_ALL_DRAWINGS;
                        requestMsg.senderUptime = localCurrentRawUptime;
                        requestMsg.senderOffset = localCurrentOffset;
                        memset(&requestMsg.touch_data, 0, sizeof(TouchData_t));
                        sendSyncMessage(&requestMsg);
                        timeRequestSentForAllDrawings = millis(); // 记录发送请求的时间
                    }
                    else
                    { // localEffectiveUptime > peerEffectiveUptime && diff > threshold
                        Serial.println("  决策: 本机有效运行时间较长 (超出阈值)。判定本机为较旧设备。通知对端 (较新设备) 清空并向本机请求更新。");
                        iamEffectivelyMoreUptimeDevice = true;
                        iamRequestingAllData = false;
                        SyncMessage_t promptMsg;
                        promptMsg.type = MSG_TYPE_CLEAR_AND_REQUEST_UPDATE;
                        promptMsg.senderUptime = localCurrentRawUptime;
                        promptMsg.senderOffset = localCurrentOffset;
                        memset(&promptMsg.touch_data, 0, sizeof(TouchData_t));
                        sendSyncMessage(&promptMsg);
                    }
                }
                initialSyncLogicProcessed = true;
            }
            else
            {
                // Serial.println("  收到 MSG_TYPE_UPTIME_INFO, 但已针对此对端 (raw uptime + offset) 处理过。忽略。");
            }
            break;
        }
        case MSG_TYPE_DRAW_POINT:
        {
            allDrawingHistory.push_back(msg.touch_data);
            TouchData_t data = msg.touch_data;
            int mapX = data.x;
            int mapY = data.y;
            if (data.timestamp - lastRemoteDrawTime > TOUCH_STROKE_INTERVAL || lastRemotePoint.z == 0) // 使用配置中的宏
            {
                tft.drawPixel(mapX, mapY, data.color);
            }
            else
            {
                tft.drawLine(lastRemotePoint.x, lastRemotePoint.y, mapX, mapY, data.color);
            }
            lastRemotePoint = {mapX, mapY, 1};
            lastRemoteDrawTime = data.timestamp;
            if (!isScreenOn)
                hasNewUpdateWhileScreenOff = true;
            break;
        }
        case MSG_TYPE_REQUEST_ALL_DRAWINGS:
        {
            Serial.println("收到 MSG_TYPE_REQUEST_ALL_DRAWINGS.");
            if (localEffectiveUptime > peerEffectiveUptime)
            {
                Serial.print("  决策: 本机有效运行时间较长。发送所有 ");
                Serial.print(allDrawingHistory.size());
                Serial.println(" 个点。");
                iamEffectivelyMoreUptimeDevice = true;
                lastKnownPeerUptime = peerRawUptime;
                lastKnownPeerOffset = peerReceivedOffset;
                initialSyncLogicProcessed = true;
                for (const auto &drawData : allDrawingHistory)
                {
                    SyncMessage_t historyPointMsg;
                    historyPointMsg.type = MSG_TYPE_DRAW_POINT;
                    historyPointMsg.senderUptime = localCurrentRawUptime;
                    historyPointMsg.senderOffset = localCurrentOffset;
                    historyPointMsg.touch_data = drawData;
                    sendSyncMessage(&historyPointMsg);
                    delay(10);
                }
                SyncMessage_t completeMsg;
                completeMsg.type = MSG_TYPE_ALL_DRAWINGS_COMPLETE;
                completeMsg.senderUptime = localCurrentRawUptime;
                completeMsg.senderOffset = localCurrentOffset;
                memset(&completeMsg.touch_data, 0, sizeof(TouchData_t));
                sendSyncMessage(&completeMsg);
                Serial.println("  所有绘图数据已发送。发送了 ALL_DRAWINGS_COMPLETE。");
            }
            else
            {
                Serial.println("  决策: 收到 REQUEST_ALL_DRAWINGS，但本机有效运行时间并非较长。忽略。");
                if (peerRawUptime != 0)
                    lastKnownPeerUptime = peerRawUptime;
                if (peerReceivedOffset != 0 || lastKnownPeerOffset != 0)
                    lastKnownPeerOffset = peerReceivedOffset;
                initialSyncLogicProcessed = true;
            }
            break;
        }
        case MSG_TYPE_ALL_DRAWINGS_COMPLETE:
        {
            Serial.println("收到 MSG_TYPE_ALL_DRAWINGS_COMPLETE.");
            if (iamRequestingAllData && !iamEffectivelyMoreUptimeDevice)
            {
                Serial.println("  同步完成 (本机为较新设备)。");
                // iamRequestingAllData = false; // 在计算后重置，以确保 timeRequestSentForAllDrawings 的有效性
                if (timeRequestSentForAllDrawings != 0) {
                    relativeBootTimeOffset = (long)peerRawUptime + (long)peerReceivedOffset - (long)timeRequestSentForAllDrawings;
                    Serial.println("  使用 timeRequestSentForAllDrawings 计算 offset");
                } else {
                    // Fallback or error, should not happen if logic is correct
                    relativeBootTimeOffset = (long)peerRawUptime + (long)peerReceivedOffset - (long)millis();
                    Serial.println("  警告: timeRequestSentForAllDrawings 为 0, 使用 millis() 计算 offset");
                }
                iamRequestingAllData = false; // 现在重置
                timeRequestSentForAllDrawings = 0; // 重置时间戳，避免下次误用
                uptimeOfLastPeerSyncedFrom = peerRawUptime;
                Serial.print("  relativeBootTimeOffset 计算并设置为: ");
                Serial.println(relativeBootTimeOffset);
                Serial.print("  uptimeOfLastPeerSyncedFrom 设置为: ");
                Serial.println(uptimeOfLastPeerSyncedFrom);
                lastKnownPeerUptime = peerRawUptime;
                lastKnownPeerOffset = peerReceivedOffset;

                Serial.println("  处理 UPTIME_INFO 进行同步决策。");
                Serial.print("  本地有效运行时间: ");
                Serial.print(millis() + relativeBootTimeOffset);
                Serial.print(" (原始: ");
                Serial.print(millis());
                Serial.print(", 偏移: ");
                Serial.print(relativeBootTimeOffset);
                Serial.println(")");
                Serial.print("  对端有效运行时间: ");
                Serial.print(peerEffectiveUptime);
                Serial.print(" (原始: ");
                Serial.print(peerRawUptime);
                Serial.print(", 偏移: ");
                Serial.print(peerReceivedOffset);
                Serial.println(")");
                initialSyncLogicProcessed = true;
            }
            else
            {
                Serial.println("  收到 ALL_DRAWINGS_COMPLETE，但本机状态不符。忽略。");
            }
            break;
        }
        case MSG_TYPE_CLEAR_AND_REQUEST_UPDATE:
        {
            Serial.println("收到 MSG_TYPE_CLEAR_AND_REQUEST_UPDATE.");
            unsigned long effectiveUptimeDifference = (localEffectiveUptime > peerEffectiveUptime) ? (localEffectiveUptime - peerEffectiveUptime) : (peerEffectiveUptime - localEffectiveUptime);

            if (localEffectiveUptime < peerEffectiveUptime && effectiveUptimeDifference > EFFECTIVE_UPTIME_SYNC_THRESHOLD)
            {
                Serial.println("  决策: 本机有效运行时间较短 (超出阈值)。执行清空并请求。");
                iamEffectivelyMoreUptimeDevice = false;
                iamRequestingAllData = true;
                allDrawingHistory.clear();
                tft.fillScreen(TFT_BLACK);
                drawMainInterface();
                SyncMessage_t requestMsg;
                requestMsg.type = MSG_TYPE_REQUEST_ALL_DRAWINGS;
                requestMsg.senderUptime = localCurrentRawUptime;
                requestMsg.senderOffset = localCurrentOffset;
                memset(&requestMsg.touch_data, 0, sizeof(TouchData_t));
                sendSyncMessage(&requestMsg);
                timeRequestSentForAllDrawings = millis(); // 记录发送请求的时间
                lastKnownPeerUptime = peerRawUptime;
                lastKnownPeerOffset = peerReceivedOffset;
                initialSyncLogicProcessed = true;
            }
            else
            {
                Serial.println("  决策: 收到 CLEAR_AND_REQUEST_UPDATE，但本机有效运行时间并非较短 (或在阈值内)。忽略。");
                if (peerRawUptime != 0)
                    lastKnownPeerUptime = peerRawUptime;
                if (peerReceivedOffset != 0 || lastKnownPeerOffset != 0)
                    lastKnownPeerOffset = peerReceivedOffset;
                initialSyncLogicProcessed = true;
            }
            break;
        }
        case MSG_TYPE_RESET_CANVAS:
        {
            Serial.println("收到 MSG_TYPE_RESET_CANVAS.");
            // allDrawingHistory.clear(); tft.fillScreen(TFT_BLACK); drawMainInterface(); // 旧的，现在用 clearScreenAndCache
            allDrawingHistory.clear();
            clearScreenAndCache(); // 使用 Project-ESPNow.ino 中的函数
            relativeBootTimeOffset = 0;
            iamEffectivelyMoreUptimeDevice = false;
            iamRequestingAllData = false;
            initialSyncLogicProcessed = false;
            lastKnownPeerUptime = 0;
            lastKnownPeerOffset = 0;
            uptimeOfLastPeerSyncedFrom = 0;
            SyncMessage_t uptimeInfoMsg;
            uptimeInfoMsg.type = MSG_TYPE_UPTIME_INFO;
            uptimeInfoMsg.senderUptime = localCurrentRawUptime;
            uptimeInfoMsg.senderOffset = relativeBootTimeOffset;
            memset(&uptimeInfoMsg.touch_data, 0, sizeof(TouchData_t));
            sendSyncMessage(&uptimeInfoMsg);
            Serial.println("  画布已重置。发送了新的 UPTIME_INFO。");
            if (!isScreenOn)
                hasNewUpdateWhileScreenOff = true;
            break;
        }
        default:
        {
            Serial.print("收到未知消息类型: ");
            Serial.println(msg.type);
            break;
        }
        }
    }
}

// 重播所有绘图历史 (在屏幕上重新绘制所有点和线)
void replayAllDrawings()
{
    lastRemotePoint = {0, 0, 0}; // 重置远程最后一点，以便从头开始绘制
    lastRemoteDrawTime = 0;      // 重置远程最后绘制时间

    for (const auto &data : allDrawingHistory)
    {
        if (data.isReset)
        {                                        // 如果是重置操作
            tft.fillScreen(TFT_BLACK);           // 清屏
            drawMainInterface();                 // 重绘主界面
            lastRemotePoint = {0, 0, 0};         // 重置状态
            lastRemoteDrawTime = data.timestamp; // 更新时间戳
            continue;                            // 继续处理下一个历史记录
        }
        int mapX = data.x;
        int mapY = data.y;
        // 根据时间戳判断是画点还是画线
        if (data.timestamp - lastRemoteDrawTime > TOUCH_STROKE_INTERVAL || lastRemotePoint.z == 0) // 使用配置中的宏
        {
            tft.drawPixel(mapX, mapY, data.color); // 画点
        }
        else
        {
            tft.drawLine(lastRemotePoint.x, lastRemotePoint.y, mapX, mapY, data.color); // 画线
        }
        lastRemotePoint = {mapX, mapY, 1};   // 更新远程最后一点
        lastRemoteDrawTime = data.timestamp; // 更新远程最后绘制时间
    }
}
