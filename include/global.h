#ifndef GLOBAL_H
#define GLOBAL_H

#include "Arduino.h"
#include <M5Atom.h>



#define SerialMon Serial
#define MONITOR_BAUDRATE 115200

#define SerialAT Serial1
#define SIM7680_BAUDRATE 115200

#define ATOM_DTU_SIM7680_RESET -1
#define ATOM_DTU_SIM7680_EN 12
#define ATOM_DTU_SIM7680_TX 22
#define ATOM_DTU_SIM7680_RX 19

#define ATOM_DTU_RS485_TX 23
#define ATOM_DTU_RS485_RX 33

#endif