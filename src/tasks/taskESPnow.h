#ifndef INIT_ESPNOW
#define INIT_ESPNOW

#include <WiFi.h>
#include <esp_now.h>
#include "global.h"

struct SensorData;

void taskInitESPNow();
SensorData getDataSensor();
bool isESPNowConnected();

#endif