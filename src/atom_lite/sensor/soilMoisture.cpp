#include "soilMoisture.h"

SoilMoisture SM_sensor;
SemaphoreHandle_t sensorSemaphore;

static const struct 
{
    uint16_t regAddr;
    uint8_t regCount;
} modbusRequest[] = 
{
    {0x0006,1},
    {0x0012,2},
    {0x0015,1},
    {0x001E,3}
};

static uint8_t requestIndex = 0;

bool SMisConnected(ModbusRTUClientClass &modbus)
{
    return false; 
}

void SMPrintData(void)
{
    if (SM_sensor.data_updated == false) return;
    printf("PH Values: %.2f \n", SM_sensor.PH_values);
    printf("Soil Moisture: %.2f \n", SM_sensor.soil_moisture);
    printf("Temperature: %.2f \n", SM_sensor.temperature);
    printf("Soil Conductivity: %d \n", SM_sensor.soil_conductivity);
    printf("Soil Nitrogen: %d \n", SM_sensor.soil_nitrogen);
    printf("Soil Phosphorus: %d \n", SM_sensor.soil_phosphorus);
    printf("Soil Potassium: %d \n", SM_sensor.soil_potassium);
}

float format2f(float value)
{
    return roundf(value * 100) / 100; //
}

void SMReadData(ModbusRTUClientClass &modbus)
{
    //Serial.println("Data is collecting");
    if(xSemaphoreTake(sensorSemaphore, pdMS_TO_TICKS(100)))
    {
        if (modbus.requestFrom(SM_ADDRESS_ID, HOLDING_REGISTERS, modbusRequest[requestIndex].regAddr, modbusRequest[requestIndex].regCount))
        {
            switch (requestIndex)
            {
                case 0:
                if (modbus.available())
                {
                    //SM_sensor.data_updated = false;
                    SM_sensor.PH_values = modbus.read() / 100.0;
                    //Serial.println("Read case 0");
                }
                break;
                case 1:
                if (modbus.available() >= 2)
                {
                    SM_sensor.soil_moisture = modbus.read() / 10.0;
                    SM_sensor.temperature = modbus.read() / 10.0;
                    //Serial.println("Read case 1");
                }
                break;
                case 2:
                if (modbus.available())
                {
                    SM_sensor.soil_conductivity = modbus.read();
                    //Serial.print("Read case 2");
                }
                break;
                case 3:
                if (modbus.available() >= 3)
                {
                    SM_sensor.soil_nitrogen = modbus.read();
                    SM_sensor.soil_phosphorus = modbus.read();
                    SM_sensor.soil_potassium = modbus.read();
                    SM_sensor.data_updated = true;
                    //Serial.print("Read case 3");
                    Serial.println("Collecting Data is done");
                }
                //SMPrintData();
                break;
            }
        }
        else
        {
            Serial.println("Modbus is error");
            Serial.print("Modbus request failed: ");
            Serial.println(modbus.lastError()); // In chuỗi mô tả lỗi
        }
        requestIndex = (requestIndex + 1) % (sizeof(modbusRequest) / sizeof(modbusRequest[0]));
        xSemaphoreGive(sensorSemaphore);
    }
    else
    {
        Serial.println("Can not read data");

    }
}

void SMReadDataRandom()
{
    if(xSemaphoreTake(sensorSemaphore, pdMS_TO_TICKS(100)))
    {
        SM_sensor.PH_values = format2f(random(300,700) / 100.0);
        SM_sensor.soil_moisture = format2f(random(200,800) / 10.0);
        SM_sensor.temperature = format2f(random(150,350) / 10.0);
        SM_sensor.soil_conductivity = random(100,2000);
        SM_sensor.soil_nitrogen = random(1,100);
        SM_sensor.soil_phosphorus = random(1,100);
        SM_sensor.soil_potassium = random(1,100);
        SM_sensor.data_updated = true;
        xSemaphoreGive(sensorSemaphore);
    }
    else
    {
        Serial.println("Can not read data");
    }
}