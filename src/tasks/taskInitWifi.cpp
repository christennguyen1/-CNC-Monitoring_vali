#include "taskInitWifi.h"

// Wifi
// const char *ssid = "RD-SEAI_2.4G";
// const char *password = "";
const char *sm_ssid = "helloworld";
const char *sm_password = "12345678";

// AP Wifi
const char *ap_ssid = "ESP32-AP";
const char *ap_password = "12345678";

void initSetUpWifiSM()
{
    WiFi.begin(sm_ssid, sm_password);
    Serial.print("Wifi is connected");
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println("");
    Serial.println("Da ket noi WiFi!");
    Serial.println(WiFi.localIP());
    Serial.print("Địa chỉ MAC của ESP32 là: ");
    Serial.println(WiFi.macAddress());
}

void initSetUpWifiAP()
{
    WiFi.softAP(ap_ssid, ap_password);
    Serial.print("IP of ESP32: ");
    Serial.println(WiFi.softAPIP());
}