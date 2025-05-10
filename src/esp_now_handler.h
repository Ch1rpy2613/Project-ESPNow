#ifndef ESP_NOW_HANDLER_H
#define ESP_NOW_HANDLER_H

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h> // 用于 esp_wifi_get_mac()
#include <vector>
#include <queue>
#include <set>
#include <string>     // For std::string if used by macSet, though it's std::set<String>
#include "config.h"   // 项目配置文件
#include <TFT_eSPI.h> // 需要 TFT_eSPI::color565 等，以及 tft 对象
#include "touch_handler.h" // For TS_Point type

// ESP-NOW 相关数据结构定义
// 这些结构体定义已从主 .ino 文件移至此处，这里是其权威定义位置。

typedef struct TouchData_s // 使用 _s 后缀表示 struct，_t 用于 typedef 后的类型名
{
    int x;                   // 映射到屏幕的X坐标 (用于绘图)
    int y;                   // 映射到屏幕的Y坐标 (用于绘图)
    unsigned long timestamp; // 绘图动作的时间戳 (本地绘制时的 millis())
    bool isReset;            // 如果此操作是清屏重置，则为 true
    uint32_t color;          // 绘图颜色
} TouchData_t;

enum MessageType_e // 使用 _e 后缀表示 enum
{
    MSG_TYPE_UPTIME_INFO,
    MSG_TYPE_DRAW_POINT,
    MSG_TYPE_REQUEST_ALL_DRAWINGS,
    MSG_TYPE_ALL_DRAWINGS_COMPLETE,
    MSG_TYPE_CLEAR_AND_REQUEST_UPDATE,
    MSG_TYPE_RESET_CANVAS,
    MSG_TYPE_SYNC_START // 新增：同步开始信号
};
typedef enum MessageType_e MessageType_t; // Typedef for the enum

typedef struct SyncMessage_s
{
    MessageType_t type;
    unsigned long senderUptime;
    long senderOffset;
    TouchData_t touch_data;
    uint16_t totalPointsForSync; // 新增：用于同步开始时告知总点数
} SyncMessage_t;


// ESP-NOW 相关全局变量 (声明为 extern)
extern esp_now_peer_info_t broadcastPeerInfo;
extern uint8_t broadcastAddress[];
extern std::queue<SyncMessage_t> incomingMessageQueue;
extern std::vector<TouchData_t> allDrawingHistory;
extern std::set<String> macSet; // 用于设备计数，由 ESP-NOW 填充

extern unsigned long lastKnownPeerUptime;
extern long lastKnownPeerOffset;
extern uint8_t lastPeerMac[6];
extern bool initialSyncLogicProcessed;
extern bool iamEffectivelyMoreUptimeDevice;
extern bool iamRequestingAllData;
extern bool isAwaitingSyncStartResponse; 
extern bool isReceivingDrawingData;    
extern bool isSendingDrawingData;        
extern size_t currentHistorySendIndex;   // 新增：用于分批发送历史记录的当前索引
extern long relativeBootTimeOffset;
extern unsigned long uptimeOfLastPeerSyncedFrom;

// 触摸点处理相关 (用于远程点绘制)
extern TS_Point lastRemotePoint;      // 远程最后一点 (用于以正确的连续性重播历史记录)
extern unsigned long lastRemoteDrawTime; // 远程最后绘制时间 (用于以正确的时间/连续性重播历史记录)
// touchInterval 定义已移至 config.h 作为 TOUCH_STROKE_INTERVAL


// 函数声明
void espNowInit(); // ESP-NOW 初始化
void OnSyncDataSent(const uint8_t *mac_addr, esp_now_send_status_t status); // 发送回调
void OnSyncDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len); // 接收回调
void sendSyncMessage(const SyncMessage_t *msg); // 发送同步消息的辅助函数
void processIncomingMessages(); // 处理接收到的消息队列
void replayAllDrawings();       // 重播所有绘图历史 (需要 tft 对象)

// 注意: replayAllDrawings 函数依赖于在 esp_now_handler.cpp 中可访问的全局 tft 对象和 drawMainInterface 函数。

#endif // ESP_NOW_HANDLER_H
