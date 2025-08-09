#ifndef GLOBAL_H
#define GLOBAL_H

#include "Arduino.h"
#include "time.h"
#include "WiFi.h"

#define NUMBER_BUTTONS 1
#define MY_LED 35
#define NUM_PIXELS 1
#define BUTTON_R 26
#define BUTTON_V 32
#define NORMAL_STATE 0

struct SensorData
{
    float pH = 0.0;
    float EC = 0.0;
    float soilTemp = 0.0;
    float soilHumid = 0.0;
    float N = 0.0;
    float P = 0.0;
    float K = 0.0;
};

#endif