#include "taskSetUpUI.h"
#include "BeVietnamPro_Medium20pt7b.h"
#define COLOR_BACKGROUND 0xF7FF
#define COLOR_BACKGROUND_REC 0xD6BD
#define COLOR_TEXT 0x2135

M5AtomDisplay display(1280, 720); // Khởi tạo với độ phân giải 720P

// // Giả lập dữ liệu cảm biến
// float pH = 6.5;
// float EC = 1.2;
// float soilTemp = 25.0;
// float soilHumid = 60.0;
// float N = 100.0;
// float P = 80.0;
// float K = 120.0;

TaskHandle_t TaskUI = NULL;

// Hàm vẽ ô thông số
void drawParameterBox(int x, int y, String label, String value, String unit)
{
    // Vẽ khung bo tròn
    display.fillRoundRect(x, y, 250, 200, 15, COLOR_BACKGROUND_REC); // Nền xanh lá đậm, kích thước 250x200
    // display.drawRoundRect(x, y, 250, 200, 15, TFT_WHITE);     // Viền trắng

    // Vẽ nhãn
    display.setTextColor(COLOR_TEXT);
    display.setTextSize(0.7); // Font nhỏ hơn cho nhãn
    display.setCursor(x + 15, y + 10);
    display.print(label);

    // Vẽ giá trị
    display.setTextColor(COLOR_TEXT);
    if (value.length() == 3)
    {
        display.setCursor(x + 80, y + 60);
    }
    else if (value.length() == 4)
    {
        display.setCursor(x + 50, y + 60);
    }
    else
    {
        display.setCursor(x + 40, y + 60);
    }
    display.setTextSize(1.6); // Font nhỏ hơn cho giá trị
    display.print(value);

    // Vẽ đơn vị
    display.setTextSize(0.6); // Font nhỏ hơn cho đơn vị
    display.setCursor(x + 150, y + 160);
    display.print(unit);
}

void taskUI(void *pvParameter)
{
    int checkScreen = 0;
    while (true)
    {
        // THÊM KIỂM TRA:
        if (isESPNowConnected())
        {
            SensorData data = getDataSensor();
            checkScreen++;

            display.startWrite();

            if (checkScreen == 1)
            {
                display.fillScreen(COLOR_BACKGROUND); // Xóa màn hình với màu đen

                // Thiết lập font chữ
                display.setFont(&BeVietnamPro_Medium20pt8b);
                display.setTextColor(COLOR_TEXT);

                // Vẽ tiêu đề
                display.setTextSize(1.5);
                display.setCursor((display.width() - display.textWidth("Thông số theo dõi")) / 2, 30);
                display.setTextColor(COLOR_TEXT);
                display.print("Thông sô theo dõi");
                display.setCursor(650, 25);
                display.setCursor(650, 22);
                display.print("´");
            }

            // Vẽ các ô thông số
            drawParameterBox(90, 150, "PH", String(data.pH, 1), "       pH");
            drawParameterBox(380, 150, "EC", String(data.EC, 1), "uS/cm");
            drawParameterBox(670, 150, "NHIÊT ÐÔ", String(data.soilTemp, 1), "         °C");
            display.setCursor(736, 170);
            display.print(".");
            display.setCursor(801, 170);
            display.print(".");
            drawParameterBox(950, 150, "ÐÔ ÂM ÐÂT", String(data.soilHumid, 1), "    %RH");
            display.setCursor(995, 170);
            display.print(".");
            display.setCursor(1025, 135);
            display.print(",");
            display.setCursor(1095, 150);
            display.print("´");
            drawParameterBox(240, 400, "N", String(data.N, 1), "mg/Kg");
            drawParameterBox(530, 400, "P", String(data.P, 1), "mg/Kg");
            drawParameterBox(820, 400, "K", String(data.K, 1), "mg/Kg");

            display.endWrite();
        }
        else
        {
            // Hiển thị trạng thái "Waiting for connection"
            display.startWrite();
            display.fillScreen(TFT_BLACK);

            // Thiết lập font và màu
            display.setFont(&BeVietnamPro_Medium20pt8b);
            display.setTextColor(TFT_YELLOW);
            display.setTextSize(1); // Chỉ dùng số nguyên (1, 2, 3...)

            // Tính toán vị trí giữa màn hình
            const char* text = "Cho kêt nôi ESP-NOW..."; // Bỏ dấu nếu font không hỗ trợ
            int16_t textWidth = display.textWidth(text);
            int16_t x = (display.width() - textWidth) / 2;
            int16_t y = 300;

            // Vẽ text
            display.setCursor(x, y);
            display.print(text);
            display.setCursor(608, 295);
            display.print("´");
            display.setCursor(533, 295);
            display.print("´");
            display.setCursor(467, 295);
            display.print("`");
            display.setCursor(476, 278);
            display.print(",");

            display.endWrite();
            checkScreen = 0;
        }

        M5.update();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void initSetUpUI()
{
    if (!display.init())
    {
        Serial.println("Display initialization failed!");
        while (1)
            ; // Dừng chương trình nếu khởi tạo thất bại
    }

    display.setRotation(1);        // Xoay ngang (90° theo chiều kim đồng hồ)
    display.setColorDepth(24);     // Độ sâu màu 24-bit
    display.fillScreen(TFT_WHITE); // Xóa màn hình với màu đen

    // Thiết lập font chữ
    display.setFont(&fonts::Orbitron_Light_32);
    display.setTextColor(TFT_WHITE);

    // Vẽ tiêu đề
    display.setTextSize(1.5);
    display.setCursor((display.width() - display.textWidth("Soil Monitoring")) / 2, 50);
    display.setTextColor(TFT_YELLOW);
    display.print("Soil Monitoring");
    display.setTextSize(1.0);

    xTaskCreate(taskUI, "UITask", 8192, NULL, 1, &TaskUI);
}
