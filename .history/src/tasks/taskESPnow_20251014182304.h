#ifndef INIT_ESPNOW
#define INIT_ESPNOW

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "global.h"

struct SensorData;

void taskInitESPNow();
SensorData getDataSensor();
bool isESPNowConnected();
void resetESPNowConnection();
void scanAndSetChannel();
int getCurrentChannel();
bool reinitializeESPNow();

// Thêm các function tiện ích
bool isESPNowInitialized();
unsigned long getLastReceiveTime();
void forceChannelScan();

#endif