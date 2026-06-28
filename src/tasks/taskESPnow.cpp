#include "taskESPnow.h"
#include <esp_wifi.h>

TaskHandle_t TaskESPNow = NULL;
TaskHandle_t TaskConnectionCheck = NULL;
TaskHandle_t TaskChannelHop = NULL;

SemaphoreHandle_t getDataMutex = NULL;

volatile unsigned long lastReceiveTime = 0;
// Mạch chính gửi mỗi 10s. Thực tế quan sát được: đôi khi có 1 gói lẻ tẻ
// bị mất do nhiễu RF tự nhiên (không phải do code), nhưng channel/kết nối
// vẫn đang đúng - gói kế tiếp vẫn tới bình thường. Đặt timeout = 4x interval
// (40s) để chịu được tới 3 gói liên tiếp bị mất mới thật sự coi là "lost",
// tránh việc dao động lost/reconnect liên tục vì những lần mất gói đơn lẻ
// vô hại.
const unsigned long CONNECTION_TIMEOUT = 40000;
volatile bool isConnected = false;
char dataStr[64];

// ================== CHANNEL HOPPING ==================
// Mạch chính (sender) bám theo channel của router WiFi nhà nó, hoặc channel
// AP mode - đây có thể là BẤT KỲ giá trị nào trong 1-13, không chỉ 1/6/11
// (đã quan sát thực tế mạch chính chạy ở channel 12 khi ở AP mode, do driver
// tự chọn khi có nhiều module cùng gọi softAP() trên 1 radio).
// Dò hết 1-13 để đảm bảo luôn bắt được, đổi lại thời gian dò lần đầu lâu hơn.
static const uint8_t CHANNEL_LIST[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
static const uint8_t CHANNEL_LIST_LEN = sizeof(CHANNEL_LIST) / sizeof(CHANNEL_LIST[0]);
static const uint32_t HOP_INTERVAL_MS = 300; // thời gian dừng ở mỗi channel khi đang dò
static uint8_t hopIndex = 0;
volatile bool channelLocked = false;

// ================== SEQUENCE TRACKING (DEBUG) ==================
// Theo dõi số thứ tự gói nhận được để phân biệt:
// - Gói bị mất THẬT trên đường truyền (seq nhảy cóc, ví dụ 5 -> 7, thiếu 6)
// - Lỗi logic timeout phía nhận (seq liên tục không thiếu nhưng vẫn báo lost)
static long lastSeq = -1; // -1 = chưa nhận gói nào

// ================== DEBUG: ĐẾM SỐ LẦN NHẬN + KHOẢNG CÁCH ==================
static uint32_t recvCount = 0;
static uint32_t lastRecvTimeDebug = 0; // riêng cho mục đích đo gap, khác lastReceiveTime (dùng cho timeout)

static void setRadioChannel(uint8_t channel)
{
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

void onReceive(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    if (mac == NULL || incomingData == NULL || len <= 0)
    {
        Serial.println("Invalid data received");
        return;
    }

    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (xSemaphoreTake(getDataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        // Giới hạn độ dài để không vượt buffer, clear buffer trước khi copy
        // để không bị dính ký tự cũ còn sót lại từ lần nhận trước (nếu lần
        // trước dài hơn lần này).
        int copyLen = (len < (int)sizeof(dataStr) - 1) ? len : (int)sizeof(dataStr) - 1;
        memset(dataStr, 0, sizeof(dataStr));
        memcpy(dataStr, incomingData, copyLen);
        dataStr[copyLen] = '\0';

        // In gộp 1 lệnh duy nhất để tránh log bị xen lẫn với task khác
        // (taskConnectionCheck cũng in Serial song song)
        // ===== DEBUG: đếm số lần nhận + khoảng cách giữa 2 lần nhận =====
        recvCount++;
        uint32_t nowDebug = millis();
        uint32_t gapMs = (lastRecvTimeDebug > 0) ? (nowDebug - lastRecvTimeDebug) : 0;
        lastRecvTimeDebug = nowDebug;

        Serial.printf("Data received from: %s | Data: %s | (#%lu, cách lần trước %lu ms)\n",
                      macStr, dataStr, (unsigned long)recvCount, (unsigned long)gapMs);

        // ===== DEBUG: parse sequence number, phát hiện gói bị mất thật =====
        // Format mới: "<seq>|<ph>_<ec>_<temp>_..." - tách phần seq trước dấu '|'
        char *sepPos = strchr(dataStr, '|');
        if (sepPos != nullptr)
        {
            long seq = atol(dataStr); // atol đọc tới khi gặp ký tự không phải số, dừng đúng ở '|'
            if (lastSeq >= 0)
            {
                long expected = lastSeq + 1;
                if (seq != expected)
                {
                    long missedCount = seq - expected;
                    if (missedCount > 0)
                        Serial.printf("⚠️  [SEQ] Mất %ld gói thật! (mong seq=%ld, nhận seq=%ld)\n",
                                      missedCount, expected, seq);
                    else
                        Serial.printf("⚠️  [SEQ] Bất thường: seq giảm hoặc lặp lại (trước=%ld, hiện tại=%ld)\n",
                                      lastSeq, seq);
                }
                else
                {
                    Serial.printf("✅ [SEQ] Liên tục đúng, seq=%ld\n", seq);
                }
            }
            lastSeq = seq;
        }

        xSemaphoreGive(getDataMutex);
    }

    lastReceiveTime = millis();
    if (!isConnected)
    {
        isConnected = true;

        // Lần đầu nhận được gói hợp lệ -> đọc channel hiện tại của radio và lock lại,
        // dừng hopping vì đã tìm đúng channel của mạch chính.
        uint8_t ch;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&ch, &second);
        channelLocked = true;
        Serial.printf("ESP-NOW connection established! Locked on channel %d\n", ch);
    }
}

SensorData getDataSensor()
{
    SensorData data;
    if (xSemaphoreTake(getDataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        char temStr[100];
        strncpy(temStr, dataStr, sizeof(temStr) - 1);
        temStr[sizeof(temStr) - 1] = '\0';
        xSemaphoreGive(getDataMutex);

        // Bỏ qua phần "<seq>|" ở đầu nếu có (định dạng debug mới),
        // chỉ parse phần data thật sau dấu '|'.
        char *dataPart = strchr(temStr, '|');
        dataPart = (dataPart != nullptr) ? (dataPart + 1) : temStr;

        char *token = strtok(dataPart, "_");
        int index = 0;
        while (token != NULL)
        {
            float val = atof(token);
            switch (index)
            {
            case 0:
                data.pH = val;
                break;
            case 1:
                data.EC = val;
                break;
            case 2:
                data.soilTemp = val;
                break;
            case 3:
                data.soilHumid = val;
                break;
            case 4:
                data.N = val;
                break;
            case 5:
                data.P = val;
                break;
            case 6:
                data.K = val;
                break;
            default:
                break;
            }

            token = strtok(NULL, "_");
            index++;
        }
    }

    return data;
}

void taskConnectionCheck(void *pvParameter)
{
    const TickType_t xDelay = pdMS_TO_TICKS(5000);

    while (1)
    {
        unsigned long currentTime = millis();

        if (lastReceiveTime > 0)
        {
            unsigned long timeDiff = (currentTime >= lastReceiveTime) ? (currentTime - lastReceiveTime) : (0xFFFFFFFF - lastReceiveTime + currentTime);

            if (timeDiff > CONNECTION_TIMEOUT)
            {
                if (isConnected)
                {
                    isConnected = false;
                    channelLocked = false; // quay lại trạng thái dò channel
                    Serial.println("⚠ ESP-NOW connection lost! Resuming channel scan...");
                }
            }
        }

        uint8_t currentCh = 0;
        wifi_second_chan_t secondCh;
        esp_wifi_get_channel(&currentCh, &secondCh);

        const char *statusStr = isConnected ? "CONNECTED" : (lastReceiveTime > 0 ? "WAITING FOR DATA" : "NO DATA RECEIVED YET");
        const char *chState = channelLocked ? "locked" : "scanning";
        Serial.printf("ESP-NOW Status: %s | Channel: %d (%s)\n", statusStr, currentCh, chState);

        vTaskDelay(xDelay);
    }
}

// ================== TASK QUẢN LÝ HOPPING ==================
// Khi chưa lock channel (chưa kết nối được mạch chính), nhảy lần lượt
// qua các channel trong CHANNEL_LIST. Khi đã lock, task này tự đứng yên.
void taskChannelHop(void *pvParameter)
{
    for (;;)
    {
        if (!channelLocked)
        {
            hopIndex = (hopIndex + 1) % CHANNEL_LIST_LEN;
            setRadioChannel(CHANNEL_LIST[hopIndex]);
            Serial.printf("[ESP-NOW] Scanning channel %d...\n", CHANNEL_LIST[hopIndex]);
        }

        vTaskDelay(pdMS_TO_TICKS(HOP_INTERVAL_MS));
    }
}

void taskESPNow(void *pvParameter)
{
    vTaskDelay(pdMS_TO_TICKS(1000));

    Serial.println("=== ESP-NOW Receiver Starting ===");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Set channel ban đầu trước khi init ESP-NOW
    setRadioChannel(CHANNEL_LIST[0]);

    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());

    esp_err_t initResult = esp_now_init();
    if (initResult != ESP_OK)
    {
        Serial.printf("ESP-NOW init failed: %s\n", esp_err_to_name(initResult));
        vTaskDelete(NULL);
        return;
    }

    esp_err_t regResult = esp_now_register_recv_cb(onReceive);
    if (regResult != ESP_OK)
    {
        Serial.printf("Register callback failed: %s\n", esp_err_to_name(regResult));
        esp_now_deinit();
        vTaskDelete(NULL);
        return;
    }

    Serial.println("ESP32 receiver ready! Scanning for sender...");

    BaseType_t taskResult = xTaskCreate(
        taskConnectionCheck,
        "ConnCheck",
        8192,
        NULL,
        1,
        &TaskConnectionCheck);

    if (taskResult != pdPASS)
    {
        Serial.println("Failed to create connection check task");
    }

    BaseType_t hopResult = xTaskCreate(
        taskChannelHop,
        "ChannelHop",
        4096,
        NULL,
        1,
        &TaskChannelHop);

    if (hopResult != pdPASS)
    {
        Serial.println("Failed to create channel hop task");
    }

    vTaskDelete(NULL);
}

void taskInitESPNow()
{
    getDataMutex = xSemaphoreCreateMutex();
    if (getDataMutex == NULL)
    {
        Serial.println("Failed to create mutex");
        return;
    }
    BaseType_t result = xTaskCreate(
        taskESPNow,
        "ESP-NOW",
        10240,
        NULL,
        2,
        &TaskESPNow);

    if (result != pdPASS)
    {
        Serial.println("Failed to create ESP-NOW task");
    }
}

bool isESPNowConnected()
{
    return isConnected;
}

void resetESPNowConnection()
{
    lastReceiveTime = 0;
    isConnected = false;
    Serial.println("ESP-NOW connection status reset");
}