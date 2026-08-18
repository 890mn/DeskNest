#ifndef IOBOX_Motor_H
#define IOBOX_Motor_H

#include <Arduino.h>
#include <Wire.h>

class IOBOX_Motor {
public:
    uint8_t M1 = 0;
    uint8_t M2 = 1;
    uint8_t ALL = 2;
    uint8_t CW = 0;
    uint8_t CCW = 1;

    IOBOX_Motor();
    ~IOBOX_Motor();

    void motorRun(int index, int direction, int speed);
    void motorStop(int index);
    void servo(int index, int angle);

    // Expose the transfer result without issuing a second I2C probe.  The
    // Mind+ API keeps motorRun/motorStop void; this accessor preserves that
    // API while allowing the component test to report the real write status.
    uint8_t lastI2cError() const { return _last_i2c_error; }

private:
    void i2cWriteBuf(int addr, unsigned char* p, int len);
    uint8_t _last_i2c_error = 4;
};

#endif
