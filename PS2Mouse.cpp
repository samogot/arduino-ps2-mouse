#include "PS2Mouse.h"
#include "Arduino.h"

#define INTELLI_MOUSE 3
#define SCALING_1_TO_1 0xE6
#define RESOLUTION_8_COUNTS_PER_MM 3

// 1ms timeout for PS/2 clock signals to prevent freezing on missed interrupts
#define TIMEOUT_MICROS 1000

enum Commands {
    SET_RESOLUTION = 0xE8,
    REQUEST_DATA = 0xEB,
    SET_REMOTE_MODE = 0xF0,
    GET_DEVICE_ID = 0xF2,
    SET_SAMPLE_RATE = 0xF3,
    RESET = 0xFF,
};

PS2Mouse::PS2Mouse(int clockPin, int dataPin) {
    _clockPin = clockPin;
    _dataPin = dataPin;
    _supportsIntelliMouseExtensions = false;
    _hasError = false;
}

void PS2Mouse::high(int pin) {
    pinMode(pin, INPUT);
    digitalWrite(pin, HIGH);
}

void PS2Mouse::low(int pin) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
}

void PS2Mouse::initialize() {
    high(_clockPin);
    high(_dataPin);
    reset();
    checkIntelliMouseExtensions();
    setResolution(RESOLUTION_8_COUNTS_PER_MM);
    setScaling(SCALING_1_TO_1);
    setSampleRate(40);
    setRemoteMode();
    delayMicroseconds(100);
}

void PS2Mouse::writeByte(char data) {
    if (_hasError) return;
    int parityBit = 1;

    high(_dataPin);
    high(_clockPin);
    delayMicroseconds(300);
    low(_clockPin);
    delayMicroseconds(300);
    low(_dataPin);
    delayMicroseconds(10);

    // start bit
    high(_clockPin);

    waitForClockState(LOW);
    if (_hasError) return;

    // data
    for (int i = 0; i < 8; i++) {
        int dataBit = bitRead(data, i);
        writeBit(dataBit);
        if (_hasError) return;
        parityBit = parityBit ^ dataBit;
    }

    // parity bit
    writeBit(parityBit);
    if (_hasError) return;

    // stop bit
    high(_dataPin);
    delayMicroseconds(50);
    waitForClockState(LOW);
    if (_hasError) return;

    // wait for mouse to switch modes
    unsigned long start = micros();
    while ((digitalRead(_clockPin) == LOW) || (digitalRead(_dataPin) == LOW)) {
        if (micros() - start > TIMEOUT_MICROS) {
            _hasError = true;
            return;
        }
    }

    // put a hold on the incoming data
    low(_clockPin);
}

void PS2Mouse::writeBit(int bit) {
    if (_hasError) return;
    if (bit == HIGH) {
        high(_dataPin);
    } else {
        low(_dataPin);
    }

    waitForClockState(HIGH);
    if (_hasError) return;
    waitForClockState(LOW);
}

char PS2Mouse::readByte() {
    if (_hasError) return 0;
    char data = 0;
    int onesCount = 0;

    high(_clockPin);
    high(_dataPin);
    delayMicroseconds(50);
    waitForClockState(LOW);
    if (_hasError) return 0;
    delayMicroseconds(5);

    // consume the start bit
    waitForClockState(HIGH);
    if (_hasError) return 0;

    // consume 8 bits of data
    for (int i = 0; i < 8; i++) {
        int bit = readBit();
        if (_hasError) return 0;
        bitWrite(data, i, bit);
        if (bit == HIGH) onesCount++;
    }

    // consume parity bit
    int parityBit = readBit();
    if (_hasError) return 0;

    // consume stop bit
    int stopBit = readBit();
    if (_hasError) return 0;

    // Check stop bit and odd parity to discard corrupted data
    if (stopBit != HIGH || (onesCount + parityBit) % 2 == 0) {
        _hasError = true;
        return 0;
    }

    // put a hold on the incoming data
    low(_clockPin);

    return data;
}

int PS2Mouse::readBit() {
    if (_hasError) return 0;
    waitForClockState(LOW);
    if (_hasError) return 0;
    int bit = digitalRead(_dataPin);
    waitForClockState(HIGH);
    return bit;
}

void PS2Mouse::setSampleRate(int rate) {
    writeAndReadAck(SET_SAMPLE_RATE);
    writeAndReadAck(rate);
}

void PS2Mouse::writeAndReadAck(int data) {
    writeByte((char) data);
    readByte();
}

void PS2Mouse::reset() {
    writeAndReadAck(RESET);
    readByte();  // self-test status
    readByte();  // mouse ID
}

void PS2Mouse::checkIntelliMouseExtensions() {
    // IntelliMouse detection sequence
    setSampleRate(200);
    setSampleRate(100);
    setSampleRate(80);

    char deviceId = getDeviceId();
    _supportsIntelliMouseExtensions = (deviceId == INTELLI_MOUSE);
}

char PS2Mouse::getDeviceId() {
    writeAndReadAck(GET_DEVICE_ID);
    return readByte();
}

void PS2Mouse::setScaling(int scaling) {
    writeAndReadAck(scaling);
}

void PS2Mouse::setRemoteMode() {
    writeAndReadAck(SET_REMOTE_MODE);
}

void PS2Mouse::setResolution(int resolution) {
    writeAndReadAck(SET_RESOLUTION);
    writeAndReadAck(resolution);
}

void PS2Mouse::waitForClockState(int expectedState) {
    unsigned long start = micros();
    while (digitalRead(_clockPin) != expectedState) {
        if (micros() - start > TIMEOUT_MICROS) {
            _hasError = true;
            return;
        }
    }
}

MouseData PS2Mouse::readData() {
    _hasError = false;
    MouseData data;

    requestData();
    data.status = readByte();
    data.position.x = readByte();
    data.position.y = readByte();

    if (_supportsIntelliMouseExtensions) {
        data.wheel = readByte();
    }

    if (_hasError) {
        data.status = 0;
        data.position.x = 0;
        data.position.y = 0;
        data.wheel = 0;
    }

    return data;
}

void PS2Mouse::requestData() {
    writeAndReadAck(REQUEST_DATA);
}
