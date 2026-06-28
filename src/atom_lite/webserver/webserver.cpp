#include "webserver.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Preferences.h>

// ========== GLOBALS ==========
Preferences prefs;
WebServer server(80);

SemaphoreHandle_t xWiFiUpdateSemaphore = NULL;
SemaphoreHandle_t xNetworkSemaphore = NULL;

bool apMode = false;
bool wifiConnected = false;
String wifi_ssid, wifi_pass;

// Đánh dấu LittleFS có mount thành công không, để handleRoot() báo lỗi
// rõ ràng thay vì lỗi mơ hồ nếu filesystem chưa sẵn sàng.
static bool littleFSReady = false;

const char *AP_SSID = "ValiCNC_Config";
const char *AP_PASS = "12345678";
const int AP_CHANNEL = 6;

// ========== HANDLERS ==========
void handleRoot()
{
    if (!littleFSReady)
    {
        server.send(500, "text/plain",
                    "Filesystem not mounted. Please upload filesystem image again.");
        return;
    }

    if (!LittleFS.exists("/index.html"))
    {
        server.send(404, "text/plain", "index.html not found. Please upload filesystem image.");
        return;
    }
    File file = LittleFS.open("/index.html", "r");
    server.streamFile(file, "text/html");
    file.close();
}

void handleConnect()
{
    String ssid = server.arg("ssid");
    String pass = server.arg("password");
    if (ssid == "")
    {
        server.send(400, "text/plain", "SSID cannot be empty!");
        return;
    }

    prefs.begin("wifi", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();

    if (xNetworkSemaphore && xSemaphoreTake(xNetworkSemaphore, pdMS_TO_TICKS(200)) == pdTRUE)
    {
        wifi_ssid = ssid;
        wifi_pass = pass;
        xSemaphoreGive(xNetworkSemaphore);
    }
    else
    {
        wifi_ssid = ssid;
        wifi_pass = pass;
    }

    // Không in password ra Serial - tránh lộ thông tin WiFi của người dùng
    // qua UART/console nếu thiết bị bị truy cập vật lý.
    Serial.printf("[WEB] Saved SSID: %s\n", ssid.c_str());
    server.send(200, "text/plain", "✅ Saved! Device will connect...");

    if (xWiFiUpdateSemaphore != NULL)
        xSemaphoreGive(xWiFiUpdateSemaphore);
}

// ========== ACCESS POINT ==========
void startAPMode()
{
    if (apMode)
        return;

    WiFi.mode(WIFI_AP_STA);
    delay(100);

    if (WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL))
    {
        apMode = true;
        Serial.println("\n=== AP Mode Started ===");
        Serial.println("SSID: " + String(AP_SSID));
        Serial.println("URL:  http://" + WiFi.softAPIP().toString());
    }
    else
    {
        Serial.println("❌ Failed to start AP Mode");
    }

    server.on("/", handleRoot);
    server.on("/connect", HTTP_POST, handleConnect);
    server.begin();
}

void stopAPMode()
{
    if (!apMode)
        return;
    Serial.println("[AP] Stopping Access Point...");
    server.close();
    WiFi.softAPdisconnect(true);
    apMode = false;
}

// ========== WIFI CONNECTION ==========
bool tryConnectSavedWiFi()
{
    prefs.begin("wifi", true);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();

    if (ssid == "")
        return false;

    // QUAN TRỌNG: đồng bộ ngay vào biến global wifi_ssid/wifi_pass.
    // Nếu không làm bước này, TaskWifiManager (mqtt_client.cpp) sẽ luôn
    // thấy wifi_ssid rỗng ở các lần boot sau (vì wifi_ssid chỉ được set
    // trong handleConnect() khi người dùng submit form web - không phải
    // mỗi lần boot). Hậu quả: cơ chế give-up/retry-limit trong
    // TaskWifiManager không bao giờ kích hoạt, dù WiFi.begin() ở đây vẫn
    // chạy và có thể fail vô thời hạn (driver auto-reconnect ngầm không
    // bao giờ bị tắt) - gây nhiễu channel ESP-NOW liên tục không dừng.
    if (xNetworkSemaphore && xSemaphoreTake(xNetworkSemaphore, pdMS_TO_TICKS(200)) == pdTRUE)
    {
        wifi_ssid = ssid;
        wifi_pass = pass;
        xSemaphoreGive(xNetworkSemaphore);
    }
    else
    {
        wifi_ssid = ssid;
        wifi_pass = pass;
    }

    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());
    Serial.printf("[WiFi] Trying saved network: %s\n", ssid.c_str());

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\n✅ WiFi connected! IP: " + WiFi.localIP().toString());
        wifiConnected = true;
        stopAPMode();
        return true;
    }
    else
    {
        Serial.println("\n❌ WiFi failed.");
        wifiConnected = false;
        // KHÔNG return ở đây để code chạy tiếp - giữ nguyên hành vi cũ,
        // nhưng giờ wifi_ssid đã được đồng bộ nên TaskWifiManager sẽ
        // tiếp quản việc retry/give-up đúng cách từ giờ trở đi.
        return false;
    }
}

// ========== TASKS ==========
void TaskWebServer(void *pv)
{
    for (;;)
    {
        if (apMode)
            server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void TaskWiFiUpdate(void *pv)
{
    for (;;)
    {
        if (xSemaphoreTake(xWiFiUpdateSemaphore, portMAX_DELAY) == pdTRUE)
        {
            Serial.println("[Network] New WiFi credentials detected");
            if (tryConnectSavedWiFi())
            {
                Serial.println("[Network] Connected successfully! Restarting...");
                vTaskDelay(pdMS_TO_TICKS(2000));
                ESP.restart();
            }
            else
            {
                Serial.println("[Network] Failed to connect new WiFi");
            }
        }
    }
}

void TaskWiFiMonitor(void *pv)
{
    for (;;)
    {
        wl_status_t status = WiFi.status();

        // 1️⃣ Nếu mất Wi-Fi → bật AP
        if (status != WL_CONNECTED)
        {
            if (wifiConnected)
            {
                wifiConnected = false;
                Serial.println("⚠️  Wi-Fi lost! Enabling AP mode...");
            }
            if (!apMode)
                startAPMode();
        }

        // 2️⃣ Nếu Wi-Fi trở lại → tắt AP
        else if (status == WL_CONNECTED)
        {
            if (!wifiConnected)
            {
                wifiConnected = true;
                Serial.println("✅ Wi-Fi reconnected! Stopping AP...");
                stopAPMode();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// ========== INIT FUNCTION ==========
void webServerInit()
{
    // QUAN TRỌNG: không tự động format khi mount fail nữa.
    // Format sẽ xóa sạch toàn bộ filesystem (bao gồm index.html bạn đã
    // upload), nên nếu mount thất bại (do thứ tự upload sai, hoặc
    // partition/filesystem type không khớp), ta chỉ log lỗi rõ ràng và
    // để littleFSReady = false, KHÔNG tự xóa data.
    //
    // Nếu thật sự cần format (ví dụ lần đầu dùng thiết bị hoàn toàn mới,
    // chưa từng có filesystem hợp lệ), hãy chủ động gọi LittleFS.format()
    // riêng một lần (qua một lệnh debug/firmware riêng), không để nó tự
    // động chạy ngầm mỗi khi mount fail.
    if (!LittleFS.begin(false))
    {
        Serial.println("❌ LittleFS mount failed!");
        Serial.println("   -> Kiểm tra lại: đã upload Filesystem Image chưa?");
        Serial.println("   -> Kiểm tra platformio.ini có 'board_build.filesystem = littlefs' chưa?");
        Serial.println("   -> Web config UI sẽ không hoạt động cho tới khi sửa xong.");
        littleFSReady = false;
    }
    else
    {
        littleFSReady = true;
        Serial.println("✅ LittleFS mounted successfully");
    }

    if (!xWiFiUpdateSemaphore)
        xWiFiUpdateSemaphore = xSemaphoreCreateBinary();
    if (!xNetworkSemaphore)
        xNetworkSemaphore = xSemaphoreCreateMutex();

    if (!tryConnectSavedWiFi())
    {
        startAPMode();
    }
    else
    {
        Serial.println("[WiFi] Connected from saved credentials!");
    }

    // TASKS
    xTaskCreatePinnedToCore(TaskWebServer, "TaskWebServer", 4096, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(TaskWiFiUpdate, "TaskWiFiUpdate", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(TaskWiFiMonitor, "TaskWiFiMonitor", 4096, NULL, 1, NULL, 0);
}