#include "IOBOX_Motor.h"

#include "Arduino.h"
#include "Wire.h"

IOBOX_Motor::IOBOX_Motor() {
    Wire.begin();
}

IOBOX_Motor::~IOBOX_Motor() {}

void IOBOX_Motor::motorRun(int index, int direction, int speed) {
    _last_i2c_error = 4;
    int Speed = abs(speed);
    if (Speed >= 255) {
        Speed = 255;
    }

    byte buf[3] = {0, (byte)direction, (byte)Speed};

    if (index > 3 || index < 0) return;
    if (index == M1) {
        buf[0] = 0x00;
        i2cWriteBuf(0x10, buf, 3);
    } else if (index == M2) {
        buf[0] = 0x02;
        i2cWriteBuf(0x10, buf, 3);
    } else if (index == ALL) {
        buf[0] = 0x00;
        i2cWriteBuf(0x10, buf, 3);
        buf[0] = 0x02;
        i2cWriteBuf(0x10, buf, 3);
    }
}

void IOBOX_Motor::motorStop(int index) {
    motorRun(index, CW, 0);
}

void IOBOX_Motor::i2cWriteBuf(int addr, unsigned char* p, int len) {
#if defined(NRF52833) || defined(ESP_PLATFORM) || defined(NRF5)
    Wire.setClock(100000);
#endif
    Wire.beginTransmission(addr);
    for (int i = 0; i < len; ++i) {
        Wire.write((uint8_t)p[i]);
    }
    _last_i2c_error = Wire.endTransmission();
}
