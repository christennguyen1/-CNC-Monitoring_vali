#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H


//#include <Arduino.h>
#include <global.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <TinyGsmClient.h>
#include "../sensor/soilMoisture.h"
#include "../esp_now/atomNow.h"
#include <WiFi.h>

// Thông tin kết nối MQTT
#define MQTT_BROKER "app.coreiot.io"
#define MQTT_PORT 1883
#define ACCESS_TOKEN "ValiCNC"
#define MQTT_PUB_TOPIC "v1/devices/me/telemetry"
#define MQTT_SUB_TOPIC "v1/devices/me/attributes"

#define UPLOAD_INTERVAL 10000

extern PubSubClient mqttClient;
extern TinyGsm modem;
extern TinyGsmClient gsmClient;

// Task handle
extern TaskHandle_t taskWifiManagerHandle;
extern TaskHandle_t taskModemHandle;
extern TaskHandle_t taskMQTTHandle;

// Semaphore để đồng bộ giữa các task
extern SemaphoreHandle_t xMQTTReadySemaphore;

void mqttCallback(char *topic, byte *payload, unsigned int len);
bool mqttConnect();
void TaskModemInit(void *pvParameters);
void TaskMQTTLoop(void *pvParameters);
void TaskWifiManager(void *pvParameters);
void mqttInit();

#endif
