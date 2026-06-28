#ifndef SOIL_MOISTURE
#define SOIL_MOISTURE

//#include "./global.h"
#include <Arduino.h>
#include <ArduinoModbus.h>
#include <ArduinoRS485.h>

#define SM_ADDRESS_ID 0x11
#define SM_FUNCTION_CODE 0x03
#define SM_ADDRESS_CHECK 0x0100

struct SoilMoisture
{
    float temperature = 0;
    float PH_values = 0;
    float soil_moisture = 0;
    int soil_conductivity = 0;
    int soil_nitrogen = 0;
    int soil_phosphorus = 0;
    int soil_potassium = 0;

    bool data_updated = false;
    bool valid = false;
};

extern SoilMoisture SM_sensor;
extern SemaphoreHandle_t sensorSemaphore;

bool SMisConnected();
void SMReadData(ModbusRTUClientClass &modbus);
void SMPrintData(void);
void SMReadDataRandom(void);

#endif