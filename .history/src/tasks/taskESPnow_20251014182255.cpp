#include "taskESPnow.h"

TaskHandle_t TaskESPNow = NULL;
TaskHandle_t TaskConnectionCheck = NULL;
TaskHandle_t TaskChannelMonitor = NULL;

SemaphoreHandle_t getDataMutex = NULL;

volatile unsigned long lastReceiveTime = 0;
const unsigned long CONNECTION_TIMEOUT = 15000; // Tăng timeout lên 15s
const unsigned long CHANNEL_CHECK_INTERVAL = 30000; // Kiểm tra channel mỗi 30s
const unsigned long REINIT_INTERVAL = 60000; // Reinit ESP-NOW mỗi 60s nếu cần
volatile bool isConnected = false;
volatile bool espnowInitialized = false;
volatile int currentChannel = 1;
volatile unsigned long lastChannelCheck = 0;
volatile unsigned long lastReinitTime = 0;
char dataStr[64];

// Danh sách các channel phổ biến để quét
const uint8_t channelList[] = {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13};
const int channelListSize = sizeof(channelList) / sizeof(channelList[0]);

// Hàm lấy channel hiện tại
int getCurrentChannel()
{
    uint8_t channel;
    wifi_second_chan_t secondCh;
    esp_wifi_get_channel(&channel, &secondCh);
    return channel;
}

// Hàm quét và thiết lập channel phù hợp
void scanAndSetChannel()
{
    Serial.println("Scanning for optimal channel...");
    
    // Lưu channel hiện tại
    int originalChannel = getCurrentChannel();
    
    // Quét các channel phổ biến
    for (int i = 0; i < channelListSize; i++)
    {
        uint8_t testChannel = channelList[i];
        esp_wifi_set_channel(testChannel, WIFI_SECOND_CHAN_NONE);
        currentChannel = testChannel;
        
        Serial.printf("Testing channel: %d\n", testChannel);
        vTaskDelay(pdMS_TO_TICKS(500)); // Đợi 500ms để test
        
        // Nếu đã có dữ liệu trong thời gian ngắn, channel này có thể tốt
        if (millis() - lastReceiveTime < 2000)
        {
            Serial.printf("Found activity on channel: %d\n", testChannel);
            return;
        }
    }
    
    // Nếu không tìm thấy channel tốt, quay về channel 1
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    currentChannel = 1;
    Serial.println("Set to default channel: 1");
}

// Hàm khởi tạo lại ESP-NOW
bool reinitializeESPNow()
{
    Serial.println("Reinitializing ESP-NOW...");
    
    // Deinit ESP-NOW
    if (espnowInitialized)
    {
        esp_now_deinit();
        espnowInitialized = false;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // Khởi tạo lại ESP-NOW
    esp_err_t initResult = esp_now_init();
    if (initResult != ESP_OK)
    {
        Serial.printf("ESP-NOW reinit failed: %s\n", esp_err_to_name(initResult));
        return false;
    }
    
    // Đăng ký callback
    esp_err_t regResult = esp_now_register_recv_cb(onReceive);
    if (regResult != ESP_OK)
    {
        Serial.printf("Register callback failed: %s\n", esp_err_to_name(regResult));
        esp_now_deinit();
        return false;
    }
    
    espnowInitialized = true;
    Serial.println("ESP-NOW reinitialized successfully!");
    return true;
}

void onReceive(const esp_now_recv_info *info, const uint8_t *incomingData, int len)
{
    uint8_t *mac = info->src_addr;
    if (mac == NULL || incomingData == NULL || len <= 0)
    {
        Serial.println("Invalid data received");
        return;
    }

    Serial.print("Data received from: ");
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    Serial.print(macStr);
    Serial.printf(" on channel: %d\n", getCurrentChannel());

    Serial.print("Data: ");
    if (xSemaphoreTake(getDataMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        for (int i = 0; i < len && i < sizeof(dataStr) - 1; i++)
        {
            Serial.print((char)incomingData[i]);
            dataStr[i] = (char)incomingData[i];
        }
        if (len < sizeof(dataStr))
        {
            dataStr[len] = '\0';
        }
        else
        {
            dataStr[sizeof(dataStr) - 1] = '\0';
        }
        Serial.println();
        xSemaphoreGive(getDataMutex);
    }

    lastReceiveTime = millis();
    if (!isConnected)
    {
        isConnected = true;
        Serial.printf("ESP-NOW connection established on channel: %d!\n", getCurrentChannel());
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

// Task giám sát và tự động thay đổi channel
void taskChannelMonitor(void *pvParameter)
{
    const TickType_t xDelay = pdMS_TO_TICKS(10000); // Kiểm tra mỗi 10s

    while (1)
    {
        unsigned long currentTime = millis();
        
        // Kiểm tra nếu mất kết nối quá lâu và cần quét channel mới
        if (!isConnected && (currentTime - lastChannelCheck) > CHANNEL_CHECK_INTERVAL)
        {
            Serial.println("Connection lost for extended period. Scanning channels...");
            scanAndSetChannel();
            lastChannelCheck = currentTime;
        }
        
        // Reinit ESP-NOW nếu cần thiết
        if (!isConnected && (currentTime - lastReinitTime) > REINIT_INTERVAL)
        {
            Serial.println("Attempting ESP-NOW reinitialization...");
            if (reinitializeESPNow())
            {
                lastReinitTime = currentTime;
                // Sau khi reinit, quét channel mới
                scanAndSetChannel();
            }
        }

        vTaskDelay(xDelay);
    }
}

void taskConnectionCheck(void *pvParameter)
{
    const TickType_t xDelay = pdMS_TO_TICKS(5000);
    int reconnectAttempts = 0;
    const int MAX_RECONNECT_ATTEMPTS = 3;

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
                    reconnectAttempts = 0;
                    Serial.println("⚠ ESP-NOW connection lost!");
                }
                
                // Thử kết nối lại
                reconnectAttempts++;
                if (reconnectAttempts <= MAX_RECONNECT_ATTEMPTS)
                {
                    Serial.printf("Reconnection attempt %d/%d\n", reconnectAttempts, MAX_RECONNECT_ATTEMPTS);
                    
                    // Thử quét channel mới sau mỗi lần thất bại
                    if (reconnectAttempts > 1)
                    {
                        scanAndSetChannel();
                    }
                }
                else if (reconnectAttempts > MAX_RECONNECT_ATTEMPTS * 2)
                {
                    // Reset counter để thử lại sau
                    reconnectAttempts = 0;
                    Serial.println("Resetting reconnection attempts...");
                }
            }
            else if (!isConnected && timeDiff < CONNECTION_TIMEOUT)
            {
                // Kết nối đã được khôi phục
                reconnectAttempts = 0;
            }
        }

        Serial.print("ESP-NOW Status: ");
        if (isConnected)
        {
            Serial.printf("CONNECTED (Channel: %d)\n", getCurrentChannel());
        }
        else if (lastReceiveTime > 0)
        {
            Serial.printf("WAITING FOR DATA (Channel: %d, Attempts: %d)\n", getCurrentChannel(), reconnectAttempts);
        }
        else
        {
            Serial.printf("NO DATA RECEIVED YET (Channel: %d)\n", getCurrentChannel());
        }

        vTaskDelay(xDelay);
    }
}

void taskESPNow(void *pvParameter)
{
    vTaskDelay(pdMS_TO_TICKS(1000));

    Serial.println("=== ESP-NOW Receiver Starting ===");

    // Cấu hình WiFi với các tham số tối ưu cho ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    // Tắt power saving để đảm bảo ESP-NOW hoạt động ổn định
    esp_wifi_set_ps(WIFI_PS_NONE);
    
    // Đặt protocol WiFi để hỗ trợ tốt hơn cho ESP-NOW
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);

    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
    
    // Thiết lập channel mặc định
    currentChannel = 1;
    esp_wifi_set_channel(currentChannel, WIFI_SECOND_CHAN_NONE);
    Serial.printf("Initial channel set to: %d\n", currentChannel);

    // Khởi tạo ESP-NOW với retry mechanism
    int initAttempts = 0;
    const int MAX_INIT_ATTEMPTS = 5;
    
    while (initAttempts < MAX_INIT_ATTEMPTS)
    {
        esp_err_t initResult = esp_now_init();
        if (initResult == ESP_OK)
        {
            Serial.println("ESP-NOW initialized successfully!");
            espnowInitialized = true;
            break;
        }
        else
        {
            initAttempts++;
            Serial.printf("ESP-NOW init attempt %d failed: %s\n", initAttempts, esp_err_to_name(initResult));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    
    if (!espnowInitialized)
    {
        Serial.println("Failed to initialize ESP-NOW after multiple attempts!");
        vTaskDelete(NULL);
        return;
    }

    esp_err_t regResult = esp_now_register_recv_cb(onReceive);
    if (regResult != ESP_OK)
    {
        Serial.printf("Register callback failed: %s\n", esp_err_to_name(regResult));
        esp_now_deinit();
        espnowInitialized = false;
        vTaskDelete(NULL);
        return;
    }

    Serial.println("ESP32 receiver ready!");
    
    // Quét channel ban đầu để tìm channel tối ưu
    scanAndSetChannel();

    // Tạo task kiểm tra kết nối
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
    
    // Tạo task giám sát channel
    BaseType_t channelTaskResult = xTaskCreate(
        taskChannelMonitor,
        "ChannelMonitor",
        8192,
        NULL,
        1,
        &TaskChannelMonitor);

    if (channelTaskResult != pdPASS)
    {
        Serial.println("Failed to create channel monitor task");
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
    
    // Khởi tạo các biến timing
    lastReceiveTime = 0;
    lastChannelCheck = 0;
    lastReinitTime = millis();
    isConnected = false;
    espnowInitialized = false;
    
    BaseType_t result = xTaskCreate(
        taskESPNow,
        "ESP-NOW",
        12288, // Tăng stack size để xử lý các function mới
        NULL,
        2,
        &TaskESPNow);

    if (result != pdPASS)
    {
        Serial.println("Failed to create ESP-NOW task");
    }
    else
    {
        Serial.println("ESP-NOW task created successfully");
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
    lastChannelCheck = 0;
    lastReinitTime = 0;
    Serial.println("ESP-NOW connection status reset");
    
    // Thực hiện quét channel và reinit nếu cần
    scanAndSetChannel();
    
    if (!espnowInitialized)
    {
        reinitializeESPNow();
    }
}