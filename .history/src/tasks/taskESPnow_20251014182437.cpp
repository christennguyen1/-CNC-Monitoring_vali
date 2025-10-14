#include "taskESPnow.h"
#include <esp_wifi.h>
TaskHandle_t TaskESPNow = NULL;
TaskHandle_t TaskConnectionCheck = NULL;

SemaphoreHandle_t getDataMutex = NULL;

volatile unsigned long lastReceiveTime = 0;
const unsigned long CONNECTION_TIMEOUT = 10000;
volatile bool isConnected = false;
char dataStr[64];

void onReceive(const esp_now_recv_info *info, const uint8_t *incomingData, int len)
{
    const uint8_t *mac = info->src_addr;

    if (mac == NULL || incomingData == NULL || len <= 0)
    {
        Serial.println("Invalid data received");
        return;
    }

    Serial.print("Data received from: ");
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.println(macStr);

    Serial.print("Data: ");
    if (xSemaphoreTake(getDataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        for (int i = 0; i < len; i++)
        {
            Serial.print((char)incomingData[i]);
            dataStr[i] = (char)incomingData[i];
            if (len < sizeof(dataStr))
            {
                dataStr[len] = '\0';
            }
        }
        Serial.println();
        xSemaphoreGive(getDataMutex);
    }

    lastReceiveTime = millis();
    if (!isConnected)
    {
        isConnected = true;
        Serial.println("ESP-NOW connection established!");
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

        char *token = strtok(temStr, "_");
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
                    Serial.println("⚠ ESP-NOW connection lost!");
                }
            }
        }

        Serial.print("ESP-NOW Status: ");
        if (isConnected)
        {
            Serial.println("CONNECTED");
        }
        else if (lastReceiveTime > 0)
        {
            Serial.println("WAITING FOR DATA");
        }
        else
        {
            Serial.println("NO DATA RECEIVED YET");
        }

        vTaskDelay(xDelay);
    }
}

void taskESPNow(void *pvParameter)
{
    vTaskDelay(pdMS_TO_TICKS(1000));

    Serial.println("=== ESP-NOW Receiver Starting ===");

    WiFi.mode(WIFI_STA);
    // WiFi.disconnect();
    WiFi.begin("ValiCNC", "12345678"); // theo AP → cùng channel

    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    uint8_t primaryChan;
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&primaryChan, &secondChan);
    esp_wifi_set_channel(primaryChan, WIFI_SECOND_CHAN_NONE);

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

    Serial.println("ESP32 receiver ready!");

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

    vTaskDelete(NULL);
}

TaskHandle_t taskWifiManagerHandle = NULL;

void WiFiEvent(WiFiEvent_t event)
{
    switch (event)
    {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.printf("✅ Got IP: %s\n", WiFi.localIP().toString().c_str());
        {
            // Lấy lại channel của AP
            uint8_t ch;
            wifi_second_chan_t sc;
            esp_wifi_get_channel(&ch, &sc);
            esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
            Serial.printf("📡 Synced ESP-NOW to channel %d\n", ch);
        }
        break;

    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
        Serial.println("⚠️ WiFi lost IP — trying renew...");
        WiFi.reconnect();
        break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        Serial.println("❌ WiFi disconnected — reconnecting...");
        WiFi.reconnect();
        break;

    default:
        break;
    }
}

void TaskWifiManager(void *pvParameters)
{
    for (;;)
    {
        String local_ssid = "ValiCNC";
        String local_pass = "12345678";
        if (!local_ssid.isEmpty() && WiFi.status() != WL_CONNECTED)
        {
            Serial.printf("[WiFi] Trying SSID: %s\n", local_ssid.c_str());

            // 🔥 Giữ nguyên AP_STA để không tắt AP/ESP-NOW
            WiFi.begin(local_ssid.c_str(), local_pass.c_str());
            // WiFi.reconnect();
            int attempt = 0;
            while (WiFi.status() != WL_CONNECTED && attempt < 10)
            {
                attempt++;
                vTaskDelay(pdMS_TO_TICKS(500));
            }

            if (WiFi.status() == WL_CONNECTED)
            {
                Serial.println("Wi-Fi connected: " + WiFi.localIP().toString());
            }
            else
            {
                Serial.println("Wi-Fi connect failed.");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void taskInitESPNow()
{
    getDataMutex = xSemaphoreCreateMutex();
    if (getDataMutex == NULL)
    {
        Serial.println("Failed to create mutex");
        return;
    }
    WiFi.onEvent(WiFiEvent);

    BaseType_t result = xTaskCreate(
        taskESPNow,
        "ESP-NOW",
        10240,
        NULL,
        2,
        &TaskESPNow);

    xTaskCreatePinnedToCore(TaskWifiManager, "TaskWifiManager", 4096, NULL, 1, &taskWifiManagerHandle, 1);

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