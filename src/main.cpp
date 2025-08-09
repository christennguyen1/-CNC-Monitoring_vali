#include "./tasks/taskSetUpUI.h"
#include "./tasks/taskESPnow.h"

void setup()
{
  M5.begin();
  Serial.begin(115200);
  Wire.begin(GPIO_NUM_38, GPIO_NUM_39, 1);

  taskInitESPNow();

  initSetUpUI();
}

void loop()
{
}