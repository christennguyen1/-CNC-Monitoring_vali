#include "mqtt_client.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// ================== EXTERNAL FROM WEBSERVER ==================
extern SemaphoreHandle_t xWiFiUpdateSemaphore;
extern SemaphoreHandle_t xNetworkSemaphore;
extern String wifi_ssid;
extern String wifi_pass;
extern bool apMode;
extern void startAPMode();
extern void stopAPMode();
extern bool wifiConnected;

// ================== MQTT CONFIG ==================
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

TaskHandle_t taskMQTTHandle = NULL;
TaskHandle_t taskWifiManagerHandle = NULL;
SemaphoreHandle_t xMQTTReadySemaphore = NULL;

// ================== FLAGS ==================
volatile bool mqttNeedReconnect = false;
char payload[256];

// ================== JSON BUILD ==================
void prepareSoilMoistureJson()
{
    StaticJsonDocument<256> doc;
    char buffer[16];

    snprintf(buffer, sizeof(buffer), "%.2f", SM_sensor.temperature);
    doc["temperature"] = buffer;

    snprintf(buffer, sizeof(buffer), "%.2f", SM_sensor.PH_values);
    doc["ph"] = buffer;

    snprintf(buffer, sizeof(buffer), "%.2f", SM_sensor.soil_moisture);
    doc["moisture"] = buffer;

    doc["conductivity"] = SM_sensor.soil_conductivity;
    doc["nitrogen"] = SM_sensor.soil_nitrogen;
    doc["phosphorus"] = SM_sensor.soil_phosphorus;
    doc["potassium"] = SM_sensor.soil_potassium;

    memset(payload, 0, sizeof(payload));
    serializeJson(doc, payload, sizeof(payload));
    Serial.printf("[MQTT] JSON ready: %u bytes\n", strlen(payload));
}

// ================== MQTT CALLBACK ==================
void mqttCallback(char *topic, byte *payload, unsigned int len)
{
    char msg[len + 1];
    memcpy(msg, payload, len);
    msg[len] = '\0';
    Serial.printf("[MQTT] Message [%s]: %s\n", topic, msg);
}

// ================== MQTT CONNECT ==================
bool mqttConnect()
{
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);

    String clientId = "ESP32_" + String(random(0xffff), HEX);
    if (!mqttClient.connect(clientId.c_str(), ACCESS_TOKEN, ""))
    {
        Serial.println("[MQTT] Connect failed");
        return false;
    }

    mqttClient.subscribe(MQTT_SUB_TOPIC);
    Serial.println("[MQTT] Connected");
    return true;
}

// ================== WIFI MANAGER ==================
// Giới hạn số lần thử lại để tránh radio bị "phá" channel liên tục
// (mỗi lần WiFi.begin() retry sẽ làm gián đoạn channel ESP-NOW đang lock,
// vì quá trình scan/associate chiếm dụng radio chung).
// Sau MAX_FAILED_CYCLES chu kỳ liên tiếp fail với CÙNG 1 SSID, dừng hẳn
// việc thử lại cho tới khi người dùng nhập SSID/password mới qua web config.
static const int MAX_FAILED_CYCLES = 3; // 3 chu kỳ x 10 lần thử = 30 lần thử tổng
static int failedCycles = 0;
static String lastTriedSsid = "";

// Expose ra ngoài (extern, không static) để các module khác (ví dụ
// atomNow.cpp) có thể đọc và in trong log debug, xác nhận chính xác
// hệ thống đã "give up" hay chưa tại 1 thời điểm cụ thể.
bool giveUpOnCurrentSsid = false;

void TaskWifiManager(void *pvParameters)
{
    for (;;)
    {
        String local_ssid, local_pass;

        // lấy thông tin wifi an toàn qua mutex
        if (xSemaphoreTake(xNetworkSemaphore, pdMS_TO_TICKS(2000)) == pdTRUE)
        {
            local_ssid = wifi_ssid;
            local_pass = wifi_pass;
            xSemaphoreGive(xNetworkSemaphore);
        }

        // Nếu SSID thay đổi so với lần trước (người dùng vừa nhập WiFi mới
        // qua web config) -> reset trạng thái "đã từ bỏ", thử lại từ đầu.
        if (local_ssid != lastTriedSsid)
        {
            lastTriedSsid = local_ssid;
            failedCycles = 0;
            giveUpOnCurrentSsid = false;

            // Bật lại auto-reconnect (đã tắt khi give-up SSID cũ trước đó),
            // để WiFi.begin() với SSID mới hoạt động bình thường.
            WiFi.setAutoReconnect(true);
        }

        // nếu có SSID, chưa connected, và chưa "từ bỏ" SSID này thì thử kết nối
        if (!local_ssid.isEmpty() && WiFi.status() != WL_CONNECTED && !giveUpOnCurrentSsid)
        {
            Serial.printf("[WiFi] Trying SSID: %s\n", local_ssid.c_str());

            WiFi.mode(WIFI_AP_STA); // để vẫn giữ AP
            WiFi.begin(local_ssid.c_str(), local_pass.c_str());

            int attempts = 0;
            while (WiFi.status() != WL_CONNECTED && attempts < 10)
            {
                attempts++;
                Serial.printf("[WiFi] Attempt %d/10\n", attempts);
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            if (WiFi.status() == WL_CONNECTED)
            {
                wifiConnected = true;
                mqttNeedReconnect = true;
                failedCycles = 0; // connect thành công -> reset đếm fail
                Serial.println("✅ Wi-Fi connected: " + WiFi.localIP().toString());

                espNowSyncChannel();

                if (apMode)
                    stopAPMode();
            }
            else
            {
                wifiConnected = false;
                failedCycles++;
                Serial.printf("❌ Wi-Fi connect failed. (cycle %d/%d)\n", failedCycles, MAX_FAILED_CYCLES);

                if (failedCycles >= MAX_FAILED_CYCLES)
                {
                    giveUpOnCurrentSsid = true;
                    Serial.println("⛔ Đã thử quá nhiều lần với SSID này, dừng lại.");
                    Serial.println("   Vào AP mode và nhập WiFi mới qua web config để thử lại.");

                    // QUAN TRỌNG: chỉ dừng việc CODE gọi WiFi.begin() là chưa đủ.
                    // ESP32 WiFi driver có auto-reconnect mặc định (bật sẵn) -
                    // dù code không gọi begin() nữa, driver vẫn tự âm thầm
                    // scan/reconnect tới SSID đã cấu hình lần cuối, khiến
                    // channel radio tiếp tục nhảy liên tục (ảnh hưởng ESP-NOW)
                    // dù đã "give up" ở tầng code. Phải chủ động tắt hẳn:
                    WiFi.setAutoReconnect(false);
                    WiFi.disconnect(false); // false = không xóa config AP, chỉ ngắt STA

                    // Đảm bảo về lại AP_STA ổn định (không còn STA cố scan nữa)
                    WiFi.mode(WIFI_AP_STA);

                    Serial.printf("[DEBUG] >>> setAutoReconnect(false) + disconnect() đã chạy. "
                                  "WiFi.status() ngay sau đó = %d\n", (int)WiFi.status());
                }
            }
        }

        // nếu mất Wi-Fi thì bật lại AP (luôn bật, bất kể có giveUp hay không,
        // để người dùng luôn có đường vào sửa lại WiFi)
        if (WiFi.status() != WL_CONNECTED && !apMode)
        {
            wifiConnected = false;
            Serial.println("⚠️ Wi-Fi lost → start AP mode");
            startAPMode();
        }

        vTaskDelay(pdMS_TO_TICKS(8000));
    }
}

// ================== MQTT LOOP ==================
void TaskMQTTLoop(void *pvParameters)
{
    uint32_t lastReconnectAttempt = 0;
    uint32_t lastPublish = 0;

    if (xSemaphoreTake(xMQTTReadySemaphore, portMAX_DELAY) == pdTRUE)
        Serial.println("[MQTT] Task started");

    for (;;)
    {
        bool networkAvailable = wifiConnected && WiFi.status() == WL_CONNECTED;
        if (!networkAvailable)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (!mqttClient.connected() || mqttNeedReconnect)
        {
            mqttNeedReconnect = false;
            uint32_t now = millis();
            if (now - lastReconnectAttempt > 3000)
            {
                lastReconnectAttempt = now;
                if (mqttConnect())
                    lastReconnectAttempt = 0;
            }
        }
        else
        {
            mqttClient.loop();

            uint32_t now = millis();
            if (now - lastPublish >= UPLOAD_INTERVAL)
            {
                lastPublish = now;
                prepareSoilMoistureJson();
                if (mqttClient.publish(MQTT_PUB_TOPIC, payload))
                    Serial.println("[MQTT] Published OK");
                else
                    Serial.println("[MQTT] Publish failed");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
        taskYIELD();
    }
}

// ================== INIT MQTT SYSTEM ==================
void mqttInit()
{
    if (!xMQTTReadySemaphore)
        xMQTTReadySemaphore = xSemaphoreCreateBinary();
    if (!xNetworkSemaphore)
        xNetworkSemaphore = xSemaphoreCreateMutex();

    Serial.println("[MQTT] Starting tasks...");
    xSemaphoreGive(xMQTTReadySemaphore);

    xTaskCreatePinnedToCore(TaskWifiManager, "TaskWifiManager", 4096, NULL, 1, &taskWifiManagerHandle, 1);
    xTaskCreatePinnedToCore(TaskMQTTLoop, "TaskMQTTLoop", 4096, NULL, 1, &taskMQTTHandle, 1);
}