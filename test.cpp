#include <WiFi.h>
#include <esp_now.h>

// Địa chỉ MAC của ESP32 nhận
uint8_t receiverMAC[] = {0xDC, 0x54, 0x75, 0xCE, 0xAC, 0xD8};

// Biến theo dõi kết nối
bool lastSendSuccess = false;
unsigned long lastSendTime = 0;
int failedSendCount = 0;
int successSendCount = 0;
bool isConnected = false;

// Callback khi gửi xong
void onSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    lastSendTime = millis();
    
    if (status == ESP_NOW_SEND_SUCCESS) {
        lastSendSuccess = true;
        successSendCount++;
        failedSendCount = 0; // Reset failed count khi thành công
        
        if (!isConnected) {
            isConnected = true;
            Serial.println("✓ ESP-NOW connection established!");
        }
        Serial.println("✓ Gửi thành công");
    } else {
        lastSendSuccess = false;
        failedSendCount++;
        Serial.printf("✗ Gửi thất bại (lần %d)\n", failedSendCount);
        
        // Nếu thất bại liên tiếp > 3 lần, coi như mất kết nối
        if (failedSendCount >= 3) {
            isConnected = false;
            Serial.println("⚠ ESP-NOW connection lost! Multiple send failures.");
        }
    }
}

// Hàm kiểm tra trạng thái kết nối
void checkConnectionStatus() {
    Serial.println("\n=== ESP-NOW Connection Status ===");
    
    // Kiểm tra peer có tồn tại không
    if (esp_now_is_peer_exist(receiverMAC)) {
        Serial.println("✓ Peer registered");
        
        if (isConnected) {
            Serial.println("✓ Connection: ACTIVE");
        } else {
            Serial.println("⚠ Connection: UNSTABLE");
        }
        
        Serial.printf("Success: %d, Failed: %d\n", successSendCount, failedSendCount);
        
        // Tính tỷ lệ thành công
        int totalSends = successSendCount + failedSendCount;
        if (totalSends > 0) {
            float successRate = (float)successSendCount / totalSends * 100;
            Serial.printf("Success rate: %.1f%%\n", successRate);
        }
    } else {
        Serial.println("✗ No peer registered");
        isConnected = false;
    }
    
    Serial.println("================================\n");
}

// Hàm thử kết nối lại
bool reconnectESPNow() {
    Serial.println("Attempting to reconnect ESP-NOW...");
    
    // Xóa peer cũ
    if (esp_now_is_peer_exist(receiverMAC)) {
        esp_now_del_peer(receiverMAC);
    }
    
    // Thêm lại peer
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMAC, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
        Serial.println("✓ Peer re-added successfully");
        failedSendCount = 0;
        return true;
    } else {
        Serial.println("✗ Failed to re-add peer");
        return false;
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("=== ESP-NOW Sender Starting ===");
    Serial.println("MAC Address: " + WiFi.macAddress());

    WiFi.mode(WIFI_STA); // Bắt buộc cho ESP-NOW
    WiFi.disconnect(); // Ngắt kết nối WiFi nếu có

    // Khởi tạo ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("✗ Lỗi khởi tạo ESP-NOW");
        return;
    }

    // Đăng ký callback khi gửi
    esp_now_register_send_cb(onSent);

    // Thêm thiết bị nhận (peer)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMAC, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("✗ Thêm peer thất bại");
        return;
    }

    Serial.println("✓ ESP-NOW sẵn sàng gửi dữ liệu!");
}

void loop() {
    const char *msg = "Hello ESP Brother!";
    
    // Kiểm tra nếu quá nhiều lỗi liên tiếp, thử kết nối lại
    if (failedSendCount >= 5) {
        Serial.println("Too many failures, attempting reconnection...");
        reconnectESPNow();
    }
    
    // Gửi dữ liệu
    esp_err_t result = esp_now_send(receiverMAC, (uint8_t *)msg, strlen(msg));

    if (result == ESP_OK) {
        Serial.println("📤 Sending message...");
    } else {
        Serial.println("✗ Send Error!");
        failedSendCount++;
    }

    delay(2000); // Gửi mỗi 2 giây
    
    // Hiển thị trạng thái kết nối mỗi 10 lần gửi
    static int sendCounter = 0;
    sendCounter++;
    if (sendCounter >= 5) {
        checkConnectionStatus();
        sendCounter = 0;
    }
}