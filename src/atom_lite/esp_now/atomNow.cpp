// #include "atomNow.h"
// #include <esp_now.h>
// #include <WiFi.h>

// // ================== CONFIG ==================
// uint8_t peerAddress[] = {0xDC, 0x54, 0x75, 0xCE, 0xAE, 0x50}; // MAC đích ESP-NOW
// SemaphoreHandle_t xEspNowDoneSemaphore = NULL;

// extern bool wifiConnected;
// extern bool apMode;
// static uint8_t currentEspNowChannel = 6; // fallback khi chưa có STA
// static bool peerAdded = false;

// static bool addOrUpdatePeer(uint8_t channel);

// // ================== SEND CALLBACK (cho retry) ==================
// // esp_now_send() trả ESP_OK chỉ nghĩa là đã đẩy vào hàng đợi truyền,
// // KHÔNG đảm bảo gói thực sự đến nơi. Callback này báo kết quả truyền
// // thật ở tầng MAC (có ACK từ phía nhận hay không), dùng để quyết định
// // có cần gửi lại (retry) hay không.
// static volatile bool sendCbReceived = false;
// static volatile bool sendCbSuccess = false;

// static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
// {
//     sendCbSuccess = (status == ESP_NOW_SEND_SUCCESS);
//     sendCbReceived = true;
// }

// // ================== INIT ESP-NOW ==================
// void initEspNow()
// {
//     // Luôn bật AP_STA để có thể phát đồng thời AP và gửi ESP-NOW
//     WiFi.mode(WIFI_AP_STA);
//     WiFi.softAP("ValiNode_AP", "12345678", currentEspNowChannel);
//     delay(100);

//     if (esp_now_init() != ESP_OK)
//     {
//         Serial.println("❌ ESP-NOW Init Failed");
//         return;
//     }

//     Serial.println("✅ ESP-NOW Init Successful");
//     Serial.print("[ESP-NOW] MAC: ");
//     Serial.println(WiFi.macAddress());

//     // QUAN TRỌNG: đọc channel THẬT từ radio sau khi gọi softAP(), không
//     // giả định nó đúng bằng currentEspNowChannel. Nếu có AP khác (ví dụ
//     // ValiCNC_Config từ webserver.cpp) cũng gọi softAP() trên cùng radio,
//     // driver có thể chọn channel khác với giá trị mình yêu cầu - nếu add
//     // peer với channel sai (không khớp radio thật), gói gửi đi sẽ liên tục
//     // fail/lệch channel mà bên nhận đang nghe.
//     uint8_t actualChannel = WiFi.channel();
//     Serial.printf("[ESP-NOW] Channel thực tế của radio: %d\n", actualChannel);

//     if (actualChannel != currentEspNowChannel)
//     {
//         Serial.printf("⚠️  Channel thực tế (%d) khác với channel yêu cầu (%d) - dùng channel thật.\n",
//                       actualChannel, currentEspNowChannel);
//     }

//     addOrUpdatePeer(actualChannel);

//     esp_now_register_send_cb(onDataSent);

//     xEspNowDoneSemaphore = xSemaphoreCreateBinary();
// }

// // ================== FORMAT SENSOR DATA ==================
// // Thêm sequence number ở đầu (tách bằng dấu '|') để bên nhận có thể
// // phát hiện gói bị mất thật (số nhảy cóc) khác với lỗi logic timeout
// // phía nhận (số liên tục không thiếu nhưng vẫn báo mất kết nối).
// // Đây là công cụ debug tạm thời - format: "<seq>|<ph>_<ec>_<temp>_..."
// static uint32_t sendSeq = 0;

// void formatSMData(char *buffer, size_t bufferSize)
// {
//     sendSeq++;
//     char dataPart[100];
//     snprintf(dataPart, sizeof(dataPart),
//              "%.2f_%d_%.2f_%.2f_%d_%d_%d_%d",
//              SM_sensor.PH_values,
//              SM_sensor.soil_conductivity,
//              SM_sensor.temperature,
//              SM_sensor.soil_moisture,
//              SM_sensor.soil_nitrogen,
//              SM_sensor.soil_phosphorus,
//              SM_sensor.soil_potassium,
//              wifiConnected ? 1 : 0);

//     snprintf(buffer, bufferSize, "%lu|%s", (unsigned long)sendSeq, dataPart);
// }

// static bool addOrUpdatePeer(uint8_t channel)
// {
//     if (peerAdded) {
//         esp_now_del_peer(peerAddress);
//         peerAdded = false;
//     }
//     esp_now_peer_info_t peerInfo = {};
//     memcpy(peerInfo.peer_addr, peerAddress, 6);
//     peerInfo.channel = channel;
//     peerInfo.encrypt = false;
//     peerInfo.ifidx = WIFI_IF_STA;

//     if (esp_now_add_peer(&peerInfo) != ESP_OK) return false;
//     peerAdded = true;
//     currentEspNowChannel = channel;
//     return true;
// }

// void espNowSyncChannel()
// {
//     // QUAN TRỌNG: dùng WiFi.channel() - đây luôn trả về channel THẬT của
//     // radio, áp dụng cho cả AP và STA (vì chung 1 radio, luôn cùng channel).
//     // Không chỉ check khi có STA connected - nếu đang ở AP mode thuần
//     // (chưa có WiFi, hoặc WiFi fail), channel vẫn có thể bị driver tự
//     // chọn khác với giá trị mình yêu cầu (ví dụ do nhiều lần gọi softAP()
//     // từ các module khác nhau trên cùng radio), nên vẫn cần đồng bộ lại.
//     uint8_t actualChannel = WiFi.channel();
//     if (actualChannel == 0) return; // giá trị không hợp lệ, bỏ qua

//     if (actualChannel != currentEspNowChannel || !peerAdded) {
//         Serial.printf("[ESP-NOW] Channel thực tế thay đổi: %d -> %d, đồng bộ lại peer...\n",
//                       currentEspNowChannel, actualChannel);
//         addOrUpdatePeer(actualChannel);
//     }
// }

// // ================== GỬI KÈM RETRY ==================
// // Gửi 1 gói, chờ callback xác nhận kết quả truyền thật (không chỉ
// // "đã enqueue"). Nếu thất bại, gửi lại tối đa MAX_RETRIES lần,
// // cách nhau RETRY_DELAY_MS để tránh đụng độ ngay lập tức.
// static const int MAX_RETRIES = 2;          // tổng cộng thử tối đa 3 lần (1 lần đầu + 2 retry)
// static const uint32_t RETRY_DELAY_MS = 50; // chờ ngắn giữa các lần thử, không làm trễ chu kỳ gửi 10s
// static const uint32_t SEND_CB_TIMEOUT_MS = 200; // chờ tối đa callback trả về

// static bool sendWithRetry(const char *data, size_t len)
// {
//     for (int attempt = 0; attempt <= MAX_RETRIES; attempt++)
//     {
//         sendCbReceived = false;
//         sendCbSuccess = false;

//         esp_err_t result = esp_now_send(peerAddress, (uint8_t *)data, len);
//         if (result != ESP_OK)
//         {
//             Serial.printf("[ESP-NOW] esp_now_send() failed (err=%d), attempt %d/%d\n",
//                           result, attempt + 1, MAX_RETRIES + 1);

//             if (result == ESP_ERR_ESPNOW_NOT_INIT)
//             {
//                 Serial.println("[ESP-NOW] Reinitializing...");
//                 esp_now_deinit();
//                 initEspNow();
//             }
//             vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
//             continue;
//         }

//         // Chờ callback báo kết quả truyền thật (có ACK ở tầng MAC hay không)
//         uint32_t waited = 0;
//         while (!sendCbReceived && waited < SEND_CB_TIMEOUT_MS)
//         {
//             vTaskDelay(pdMS_TO_TICKS(10));
//             waited += 10;
//         }

//         if (sendCbReceived && sendCbSuccess)
//         {
//             if (attempt > 0)
//                 Serial.printf("[ESP-NOW] Gửi thành công sau %d lần thử lại\n", attempt);
//             return true;
//         }

//         Serial.printf("[ESP-NOW] Gói không tới (callback fail/timeout), thử lại %d/%d\n",
//                       attempt + 1, MAX_RETRIES + 1);
//         vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
//     }

//     Serial.println("[ESP-NOW] Đã thử hết số lần retry, gói này coi như mất.");
//     return false;
// }

// // ================== ESP-NOW SEND TASK ==================
// void sendSensorDataTask(void *pvParameters)
// {
//     Serial.println("[ESP-NOW] Task started");

//     for (;;)
//     {

//         espNowSyncChannel();

//         if (xSemaphoreTake(sensorSemaphore, pdMS_TO_TICKS(100)))
//         {
//             if (SM_sensor.data_updated)
//             {
//                 char dataBuffer[128];
//                 formatSMData(dataBuffer, sizeof(dataBuffer));

//                 // Gửi qua ESP-NOW, có retry nếu gói không tới được
//                 bool sent = sendWithRetry(dataBuffer, strlen(dataBuffer));

//                 if (sent)
//                 {
//                     Serial.printf("[ESP-NOW] Sent OK: %s\n", dataBuffer);
//                 }
//                 else
//                 {
//                     Serial.printf("[ESP-NOW] Send failed sau khi retry: %s\n", dataBuffer);
//                 }

//                 SM_sensor.data_updated = false;
//             }
//             xSemaphoreGive(sensorSemaphore);
//         }

//         // Lưu ý: KHÔNG tự gọi lại WiFi.mode()/WiFi.softAP() ở đây.
//         // AP mode đã được quản lý đầy đủ bởi webserver.cpp (startAPMode/
//         // stopAPMode qua TaskWiFiMonitor) - gọi lại softAP() định kỳ ở đây
//         // là dư thừa và có thể làm gián đoạn radio trong tích tắc, khiến
//         // peer đang nhận ESP-NOW (màn hình) bị mất gói dù channel không đổi.
//         // WiFi.mode(WIFI_AP_STA) đã được set sẵn từ lúc initEspNow(),
//         // không cần set lại mỗi vòng lặp.

//         vTaskDelay(pdMS_TO_TICKS(10000)); // gửi mỗi 10 giây
//     }
// }




#include "atomNow.h"
#include <esp_now.h>
#include <WiFi.h>

// ================== SEND CALLBACK (DEBUG) ==================
// esp_now_send() trả ESP_OK chỉ nghĩa là đã đẩy vào hàng đợi truyền,
// KHÔNG đảm bảo gói thực sự đến nơi/đã truyền xong qua sóng. Callback
// này cho biết kết quả THẬT ở tầng MAC, giúp phân biệt: gói bị driver
// "ăn mất" trước khi truyền (sẽ KHÔNG thấy callback, hoặc callback FAIL)
// hay gói có truyền nhưng bên nhận không bắt được (callback SUCCESS
// nhưng bên nhận vẫn không nhận - lúc đó lỗi nằm bên nhận/RF, không
// nằm ở bên gửi).
static volatile bool sendCbReceived = false;
static volatile bool sendCbSuccess = false;
static volatile uint32_t sendCbSeqDebug = 0; // seq tương ứng với callback gần nhất

static void onDataSentDebug(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    sendCbSuccess = (status == ESP_NOW_SEND_SUCCESS);
    sendCbReceived = true;
    Serial.printf("[ESP-NOW][DEBUG] >>> Callback nhận được cho seq=%lu: %s\n",
                  (unsigned long)sendCbSeqDebug,
                  sendCbSuccess ? "SUCCESS (đã rời anten, có ACK)" : "FAIL (không có ACK)");
}

// ================== CONFIG ==================
uint8_t peerAddress[] = {0xDC, 0x54, 0x75, 0xCE, 0xAE, 0x50}; // MAC đích ESP-NOW
SemaphoreHandle_t xEspNowDoneSemaphore = NULL;

extern bool wifiConnected;
extern bool apMode;
extern bool giveUpOnCurrentSsid; // từ mqtt_client.cpp, để debug xác nhận đã give-up hay chưa
static uint8_t currentEspNowChannel = 6; // fallback khi chưa có STA
static bool peerAdded = false;

static bool addOrUpdatePeer(uint8_t channel);

// ================== INIT ESP-NOW ==================
void initEspNow()
{
    // Luôn bật AP_STA để có thể phát đồng thời AP và gửi ESP-NOW
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ValiNode_AP", "12345678", currentEspNowChannel);  // Phát AP ở channel 6
    delay(100);

    if (esp_now_init() != ESP_OK)
    {
        Serial.println("❌ ESP-NOW Init Failed");
        return;
    }

    Serial.println("✅ ESP-NOW Init Successful");
    Serial.print("[ESP-NOW] MAC: ");
    Serial.println(WiFi.macAddress());
    Serial.printf("[ESP-NOW] Channel: %d\n", WiFi.channel());

    addOrUpdatePeer(currentEspNowChannel);

    esp_now_register_send_cb(onDataSentDebug);

    xEspNowDoneSemaphore = xSemaphoreCreateBinary();
}

// ================== FORMAT SENSOR DATA ==================
// Thêm sequence number ở đầu (tách bằng dấu '|') để bên nhận có thể
// đối chiếu, phát hiện chính xác có gói nào bị mất hay không (số nhảy cóc).
static uint32_t sendSeq = 0;

void formatSMData(char *buffer, size_t bufferSize)
{
    sendSeq++;

    // ĐANG TEST: hardcode data cố định để test (không đọc SM_sensor thật).
    // Khi cần quay lại data thật, đảo ngược comment 2 dòng dưới đây:
    char dataPart[100];
    // snprintf(dataPart, sizeof(dataPart), "7.00_0_29.90_100.00_0_0_0_0");

    snprintf(dataPart, sizeof(dataPart),
             "%.2f_%d_%.2f_%.2f_%d_%d_%d_%d",
             SM_sensor.PH_values,
             SM_sensor.soil_conductivity,
             SM_sensor.temperature,
             SM_sensor.soil_moisture,
             SM_sensor.soil_nitrogen,
             SM_sensor.soil_phosphorus,
             SM_sensor.soil_potassium,
             wifiConnected ? 1 : 0);

    snprintf(buffer, bufferSize, "%lu|%s", (unsigned long)sendSeq, dataPart);
}

static bool addOrUpdatePeer(uint8_t channel)
{
    if (peerAdded) {
        esp_now_del_peer(peerAddress);
        peerAdded = false;
    }
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, peerAddress, 6);
    peerInfo.channel = channel;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) return false;
    peerAdded = true;
    currentEspNowChannel = channel;
    return true;
}

void espNowSyncChannel()
{
    if (WiFi.status() != WL_CONNECTED) return;
    uint8_t staChannel = WiFi.channel();
    if (staChannel == 0) return;

    if (staChannel != currentEspNowChannel || !peerAdded) {
        addOrUpdatePeer(staChannel);
    }
}

// ================== ESP-NOW SEND TASK ==================
// Debug: đo thời gian giữa 2 lần gửi liên tiếp (sendSeq dùng chung với
// formatSMData() - đây cũng là số đã gắn vào payload gửi đi).
static uint32_t lastSendTime = 0;

void sendSensorDataTask(void *pvParameters)
{
    Serial.println("[ESP-NOW] Task started");

    for (;;)
    {

        espNowSyncChannel();

        if (xSemaphoreTake(sensorSemaphore, pdMS_TO_TICKS(100)))
        {
            // ĐANG TEST: luôn gửi (bỏ điều kiện SM_sensor.data_updated)
            // vì dùng data hardcode, không cần chờ cảm biến cập nhật.
            {
                char dataBuffer[128];
                formatSMData(dataBuffer, sizeof(dataBuffer)); // sendSeq++ xảy ra bên trong

                // ===== DEBUG: đo khoảng cách giữa 2 lần gửi =====
                uint32_t now = millis();
                uint32_t gapMs = (lastSendTime > 0) ? (now - lastSendTime) : 0;
                lastSendTime = now;

                // ===== DEBUG: in context lúc gửi - để biết WiFi/MQTT/CPU
                // đang làm gì đúng lúc này, xem có đúng lúc bận không =====
                Serial.printf("[ESP-NOW][DEBUG] >>> Trước khi gửi seq=%lu | WiFi.status=%d | "
                              "giveUp=%d | freeHeap=%u | core=%d | tick=%lu\n",
                              (unsigned long)sendSeq, (int)WiFi.status(), (int)giveUpOnCurrentSsid,
                              (unsigned)ESP.getFreeHeap(), (int)xPortGetCoreID(),
                              (unsigned long)xTaskGetTickCount());

                sendCbReceived = false;
                sendCbSuccess = false;
                sendCbSeqDebug = sendSeq;

                // Gửi qua ESP-NOW
                esp_err_t result = esp_now_send(peerAddress, (uint8_t *)dataBuffer, strlen(dataBuffer));

                Serial.printf("[ESP-NOW][DEBUG] >>> esp_now_send() trả về ngay: %s (err=%d)\n",
                              (result == ESP_OK) ? "ESP_OK (đã enqueue)" : "LỖI", result);

                // Chờ callback xác nhận (tối đa 300ms) để biết kết quả truyền THẬT
                uint32_t waitedCb = 0;
                while (!sendCbReceived && waitedCb < 300)
                {
                    vTaskDelay(pdMS_TO_TICKS(10));
                    waitedCb += 10;
                }
                if (!sendCbReceived)
                {
                    Serial.printf("[ESP-NOW][DEBUG] >>> ⚠️ KHÔNG nhận được callback sau %lu ms cho seq=%lu! "
                                  "(driver có thể đã enqueue nhưng không bao giờ thực gửi)\n",
                                  (unsigned long)waitedCb, (unsigned long)sendSeq);
                }

                if (result == ESP_OK)
                {
                    Serial.printf("[ESP-NOW] Sent OK (cách lần trước %lu ms): %s\n",
                                  (unsigned long)gapMs, dataBuffer);
                }
                else
                {
                    Serial.printf("[ESP-NOW] Send failed (cách lần trước %lu ms, err=%d): %s\n",
                                  (unsigned long)gapMs, result, dataBuffer);

                    // Nếu lỗi do channel mismatch, re-init ở channel 6
                    if (result == ESP_ERR_ESPNOW_NOT_INIT)
                    {
                        Serial.println("[ESP-NOW] Reinitializing...");
                        esp_now_deinit();
                        initEspNow();
                    }
                }

                SM_sensor.data_updated = false;
            }
            xSemaphoreGive(sensorSemaphore);
        }

        // Lưu ý: KHÔNG tự gọi lại WiFi.mode()/WiFi.softAP() ở đây.
        // AP mode đã được quản lý đầy đủ bởi webserver.cpp (startAPMode/
        // stopAPMode qua TaskWiFiMonitor) - gọi lại softAP() định kỳ ở đây
        // là dư thừa và có thể làm gián đoạn radio trong tích tắc, khiến
        // peer đang nhận ESP-NOW (màn hình) bị mất gói dù channel không đổi.
        // WiFi.mode(WIFI_AP_STA) đã được set sẵn từ lúc initEspNow(),
        // không cần set lại mỗi vòng lặp.

        vTaskDelay(pdMS_TO_TICKS(10000)); // gửi mỗi 10 giây
    }
}