#include <M5Atom.h>
#include <ArduinoModbus.h>
#include <ArduinoRS485.h>
#include "ATOM_DTU_CAT1.h"

#include "./atom_lite/sensor/soilMoisture.h"
#include "./atom_lite/esp_now/atomNow.h"
#include "./atom_lite/mqtt/mqtt_client.h"
#include "./atom_lite/webserver/webserver.h"

RS485Class RS485(Serial2, ATOM_DTU_RS485_RX, ATOM_DTU_RS485_TX, -1, -1);

TaskHandle_t readTaskHandle, espnowTaskHandle;

void readSensorTask(void *pvParameters)
{
  while(1)
  {
    SMReadData(ModbusRTUClient);
    //SMReadDataRandom();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup()
{
  M5.begin(true, false, true);
  Serial.begin(115200);

  // 1️⃣ Bắt đầu RS485 / Modbus
  ModbusRTUClient.begin(9600);
  ModbusRTUClient.setTimeout(2000);
  vTaskDelay(pdMS_TO_TICKS(1000));

  // 2️⃣ Semaphore dùng chung giữa các task
  sensorSemaphore = xSemaphoreCreateMutex();

  // 3️⃣ Khởi tạo Wi-Fi + WebServer (chế độ AP_STA)
  webServerInit(); // -> tạo AP config + STA connect
  vTaskDelay(pdMS_TO_TICKS(2000));

  // 4️⃣ Bắt đầu ESP-NOW (sau khi Wi-Fi đã vào AP_STA mode)
  initEspNow();
  vTaskDelay(pdMS_TO_TICKS(1000));

  // 5️⃣ MQTT chạy trên Wi-Fi
  mqttInit();
  vTaskDelay(pdMS_TO_TICKS(1000));

  // // 6️⃣ Task đọc cảm biến
  xTaskCreatePinnedToCore(readSensorTask, "ReadSensor", 4096, NULL, 2, &readTaskHandle, 1);

  // 7️⃣ Task gửi ESP-NOW
  xTaskCreatePinnedToCore(sendSensorDataTask, "SendESPNow", 4096, NULL, 1, &espnowTaskHandle, 1);

  Serial.println("✅ All systems initialized.");
}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1000));
}