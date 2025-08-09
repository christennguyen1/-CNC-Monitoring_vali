#include "taskSetUpUI.h"
#include "Airbnb_Cereal_App_Medium20pt7b.h"
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
    display.setTextSize(0.8); // Font nhỏ hơn cho nhãn
    display.setCursor(x + 15, y + 20);
    display.print(label);

    // Vẽ giá trị
    display.setTextColor(COLOR_TEXT);
    if (value.length() == 3)
    {
        display.setCursor(x + 90, y + 80);
    }
    else if (value.length() == 4)
    {
        display.setCursor(x + 80, y + 80);
    }
    else
    {
        display.setCursor(x + 50, y + 80);
    }
    display.setTextSize(1.6); // Font nhỏ hơn cho giá trị
    display.print(value);

    // Vẽ đơn vị
    display.setTextSize(0.8); // Font nhỏ hơn cho đơn vị
    display.setCursor(x + 140, y + 160);
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
                display.setFont(&Airbnb_Cereal_App_Medium20pt7b);
                display.setTextColor(COLOR_TEXT);

                // Vẽ tiêu đề
                display.setTextSize(1.5);
                display.setCursor((display.width() - display.textWidth("Soil Monitoring")) / 2, 30);
                display.setTextColor(COLOR_TEXT);
                display.print("Soil Monitoring");
            }

            // Vẽ các ô thông số
            drawParameterBox(90, 150, "pH", String(data.pH, 1), "      pH");
            drawParameterBox(380, 150, "EC", String(data.EC, 1), "uS/cm");
            drawParameterBox(670, 150, "Soil Temp", String(data.soilTemp, 1), "         C");
            drawParameterBox(950, 150, "Soil Humid", String(data.soilHumid, 1), "   %RH");
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
            display.setTextColor(TFT_YELLOW);
            display.setCursor(display.width() / 2 - 100, display.height() / 2);
            display.print("Waiting for ESP-NOW connection...");
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