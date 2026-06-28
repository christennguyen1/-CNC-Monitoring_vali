#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>

extern void webServerInit();
extern String wifi_ssid, wifi_pass;

// Expose network synchronization primitives and AP state to other modules
extern SemaphoreHandle_t xWiFiUpdateSemaphore;
extern SemaphoreHandle_t xNetworkSemaphore;
extern bool apMode;

// Allow other modules to request AP start/stop
extern void startAPMode();
extern void stopAPMode();