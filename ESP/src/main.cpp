/*
 * Copyright (C) 2016 Peter Withers
 */

/*
 * E36Timer.c
 *
 * Created: 13/02/2016 18:00:32
 * Author : Peter Withers <peter@gthb-bambooradical.com>
 */

//#include <Arduino.h>
//#include <eeprom.h>
#include <EEPROM.h>
#include <Servo.h>
// todo: this include and related defines are temporary
//#include <NewPing.h>
#include <user_interface.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <Sodaq_BMP085.h>
#include <Wire.h>
#include <Ticker.h>

Ticker pwmTweenTimer;
ADC_MODE(ADC_VCC);
WiFiUDP Udp;
unsigned int localUdpPort = 2222;

const byte DNS_PORT = 53;
IPAddress timerIP(192, 168, 1, 1);
IPAddress remoteIP(192, 168, 1, 2);
DNSServer dnsServer;
ESP8266WebServer webServer(80);
ESP8266HTTPUpdateServer httpUpdater;

Sodaq_BMP085 pressureSensor;
double baselinePressure;
bool isTimer = false;
bool hasPressureSensor = true;

int historyLength = 1024;
int historyIndex = 0;
int udpHistoryIndex = 0;
int flightStartIndex = 0;
float temperatureHistory[1024];
float altitudeHistory[1024];
uint8_t escHistory[1024];
uint8_t dtHistory[1024];
int32_t remoteRssiHistory[1024];
float remoteVoltageHistory[1024];
int maxSvgValue = 0;

//#define TRIGGER_PIN  3
//#define ECHO_PIN     2
//#define MAX_DISTANCE 400

//NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);
// end temporary defines

#define IndicatorLed     2
#define ServoPWM         4
#define EscPWM           5
#define ButtonPin        0
#define SdaPin           12
#define SclPin           14

Servo dtServo;
Servo escServo;

#define MaxPwm 141
#define MinPwm 44

//#define ButtonIsDown (sonar.ping_cm() < 30)
#define ButtonIsDown (digitalRead(ButtonPin) == 0)
#define RcDtIsActive false
//#define RcDtIsActive (digitalRead(RcDt1Pin) == 0)

#define TurnOnLed digitalWrite(IndicatorLed, 1)
#define TurnOffLed digitalWrite(IndicatorLed, 0)

typedef union {

    struct {
        uint8_t dtStatus;
        int32_t rssi;
        uint16_t voltage;
    } data;
    uint8_t asBytes[sizeof (data)];
} TelemetryData;

enum MachineState {
    setupSystem,
    throttleMax,
    waitingButtonRelease1,
    throttleMin,
    waitingButtonRelease2,
    startWipe1, // when the device resets we wipe the servo arm to release the DT lever so that a reset in midair does not prevent DT
    endWipe1,
    editMotorTime,
    startWipe2, // between the settings wipe to indicate the change
    endWipe2,
    editDtTime,
    saveEepromSettings,
    waitingButtonStart,
    clearGraphArrays,
    waitingButtonRelease,
    motorRun,
    freeFlight,
    triggerDT,
    waitingForRestart,
    waitingFirmwareUpdate,
    dtRemoteConfig,
    dtRemote
};

volatile enum MachineState machineState = setupSystem;

volatile unsigned long buttonLastChangeMs = 0;
volatile unsigned long buttonDebounceMs = 100;

volatile unsigned long lastMotorSpinUpMs = 0;
volatile unsigned long flightIdRandom1 = 0;
volatile unsigned long flightIdRandom2 = 0;
volatile unsigned long currentFlightMs = 0;
volatile unsigned long lastStateChangeMs = 0;
volatile const unsigned long editingTimeoutMs = 3000;
volatile const unsigned long escSpinDownMs = 1000;
volatile const unsigned long escSpinUpMs = 1000;
volatile const unsigned long servoWipeMs = 1000;
volatile const unsigned long displayStepMs = 500;
volatile unsigned long editingStartMs = 0;
volatile const unsigned long powerDownServoMs = 30l * 1000l; // 30 seconds before the servo is powered down
volatile const unsigned long powerDownEscSeconds = 300l * 1000l; // 5 minutes before the ESC is powered down

const unsigned long motorSeconds[] = {2, 4, 5, 7, 10, 13, 15};
const int motorSecondsSize = 6;
volatile int motorSecondsIndex = 0;
const unsigned long dethermalSeconds[] = {0, 5, 30, 60, 90, 120, 180, 240, 300};

// start measured actual times
// tested with the timer calibrated to produce 50.38hz
// motor 2: {2.3}
// motor 7: {7.2}
// motor 15: {14.9,14.6,14.9,14.7};
// any DT below motor time of 15: {16.0,15.7}
// DT 0 with motor 2: {3.4}
// DT 30: {30.6};
// DT 60: {59.8};
// DT 300: {293.0};
// end measured actual times

const int dethermalSecondsSize = 9;
volatile int dethermalSecondsIndex = 0;
const int waitingEscValue = ((MaxPwm - MinPwm) / 3) + MinPwm;
volatile int buttonHasBeenUp = 0;

char ssid[32] = "";
char password[32] = "";

void slowFlash() {
    if (millis() / 1000 % 2 == 0) {
        TurnOnLed;
    } else {
        TurnOffLed;
    }
}

void fastFlash() {
    if ((millis() / (1000 / 5)) % 2 == 0) {
        TurnOnLed;
    } else {
        TurnOffLed;
    }
}

void doubleFlash() {
    int pulseIndex = (millis() / (1000 / 5)) % 10;
    if (pulseIndex == 0 || pulseIndex == 3) {
        TurnOnLed;
    } else {
        TurnOffLed;
    }
}

void trippleFlash() {
    int pulseIndex = (millis() / (1000 / 5)) % 10;
    if (pulseIndex == 0 || pulseIndex == 2 || pulseIndex == 4) {
        TurnOnLed;
    } else {
        TurnOffLed;
    }
}

void setCredentials() {
    if (webServer.hasArg("ssid") && webServer.hasArg("pass")) {
        webServer.arg("ssid").toCharArray(ssid, sizeof (ssid) - 1);
        webServer.arg("pass").toCharArray(password, sizeof (password) - 1);
        EEPROM.begin(512);
        EEPROM.put(9, ssid);
        EEPROM.put(9 + sizeof (ssid), password);
        char ok[2 + 1] = "OK";
        EEPROM.put(9 + sizeof (ssid) + sizeof (password), ok);
        EEPROM.commit();
        EEPROM.end();
    }
}

void saveSettings() {
    boolean changeMade = false;
    EEPROM.begin(512);
    Serial.print(" motorSecondsIndex: ");
    Serial.print(motorSecondsIndex);
    Serial.print(" EEPROM.read(1): ");
    Serial.print(EEPROM.read(1));
    // using update not write to preserve eeprom life
    // update does not seem to exist in the ESP library so we test before setting to preserve the eeprom
    if (EEPROM.read(1) != motorSecondsIndex) {
        Serial.print(" motorSecondsIndex storing changes");
        EEPROM.write(1, motorSecondsIndex);
        changeMade = true;
    } else {
        Serial.print(" motorSecondsIndex unchanged");
    }
    if (EEPROM.read(2) != dethermalSecondsIndex) {
        Serial.print(" dethermalSecondsIndex storing changes");
        EEPROM.write(2, dethermalSecondsIndex);
        changeMade = true;
    } else {
        Serial.print(" dethermalSecondsIndex unchanged");
    }
    byte settingsByte = EEPROM.read(3);
    if (bitRead(settingsByte, 0) != isTimer || bitRead(settingsByte, 1) != hasPressureSensor) {
        Serial.print(" toggleTimerOrRemote, hasPressureSensor storing changes");
        bitWrite(settingsByte, 0, isTimer);
        bitWrite(settingsByte, 1, hasPressureSensor);
        EEPROM.write(3, settingsByte);
        changeMade = true;
    } else {
        Serial.print(" toggleTimerOrRemote, hasPressureSensor unchanged");
    }
    if (changeMade) {
        Serial.print(" writing changes");
        EEPROM.commit();
    } else {
        Serial.print(" unchanged no commit");
    }
    EEPROM.end();
}

void loadSavedSettings() {
    EEPROM.begin(512);
    motorSecondsIndex = EEPROM.read(1);
    dethermalSecondsIndex = EEPROM.read(2);
    byte settingsByte = EEPROM.read(3);
    isTimer = bitRead(settingsByte, 0);
    hasPressureSensor = bitRead(settingsByte, 1);
    motorSecondsIndex = (motorSecondsIndex < motorSecondsSize) ? motorSecondsIndex : motorSecondsSize - 1;
    dethermalSecondsIndex = (dethermalSecondsIndex < dethermalSecondsSize) ? dethermalSecondsIndex : dethermalSecondsSize - 1;
    EEPROM.get(9, ssid);
    EEPROM.get(9 + sizeof (ssid), password);
    char ok[2 + 1];
    EEPROM.get(9 + sizeof (ssid) + sizeof (password), ok);
    if (String(ok) != String("OK")) {
        ssid[0] = 0;
        password[0] = 0;
    }
    EEPROM.end();
}

void pinChangeInterrupt() {
    buttonLastChangeMs = millis();
}

String getTelemetryJson() {
    String returnString = "machineState: '";
    switch (machineState) {
        case setupSystem:
            returnString += "setupSystem";
            break;
        case throttleMax:
            returnString += "throttleMax";
            break;
        case waitingButtonRelease1:
            returnString += "waitingButtonRelease1";
            break;
        case throttleMin:
            returnString += "throttleMin";
            break;
        case waitingButtonRelease2:
            returnString += "waitingButtonRelease2";
            break;
        case startWipe1:
            returnString += "startWipe1";
            break;
        case endWipe1:
            returnString += "endWipe1";
            break;
        case editMotorTime:
            returnString += "editMotorTime";
            break;
        case startWipe2:
            returnString += "startWipe2";
            break;
        case endWipe2:
            returnString += "endWipe2";
            break;
        case editDtTime:
            returnString += "editDtTime";
            break;
        case saveEepromSettings:
            returnString += "saveEepromSettings";
            break;
        case waitingButtonStart:
            returnString += "waitingButtonStart";
            break;
        case clearGraphArrays:
            returnString += "clearGraphArrays";
            break;
        case waitingButtonRelease:
            returnString += "waitingButtonRelease";
            break;
        case motorRun:
            returnString += "motorRun";
            break;
        case freeFlight:
            returnString += "freeFlight";
            break;
        case triggerDT:
            returnString += "triggerDT";
            break;
        case waitingForRestart:
            returnString += "waitingForRestart";
            break;
        case waitingFirmwareUpdate:
            returnString += "waitingFirmwareUpdate";
            break;
        case dtRemoteConfig:
            returnString += "dtRemoteConfig";
            break;
        case dtRemote:
            returnString += "dtRemote";
            break;
    }
    int servoPosition = dtServo.read();
    int escPosition = escServo.read();
    returnString += "'; servoPosition: ";
    returnString += servoPosition;
    returnString += "; escPosition: ";
    returnString += escPosition;
    returnString += "; dethermalSeconds: ";
    returnString += dethermalSeconds[dethermalSecondsIndex];
    returnString += "; motorSeconds: ";
    returnString += motorSeconds[motorSecondsIndex];
    returnString += "; rssi: ";
    returnString += WiFi.RSSI();
    returnString += "; lastStateChangeMs: ";
    returnString += (millis() - lastStateChangeMs);
    return returnString;
}

bool getPressure(double &temperature, double &pressure, double &altitude) {
    temperature = pressureSensor.readTemperature();
    pressure = pressureSensor.readPressure();
    altitude = pressureSensor.readAltitude(baselinePressure);

    /*Serial.print("altitudeHistory: ");
    for (int currentIndex = 0; currentIndex < historyLength; currentIndex++) {
        Serial.print(altitudeHistory[currentIndex]);
        Serial.print(", ");
    }
    Serial.println(";");
    Serial.print("temperatureHistory: ");
    for (int currentIndex = 0; currentIndex < historyLength; currentIndex++) {
        Serial.print(temperatureHistory[currentIndex]);
        Serial.print(", ");
    }
    Serial.println(";");*/
    return true;
}

String getTelemetryString() {
    String telemetryString = "machineState: ";
    switch (machineState) {
        case setupSystem:
            telemetryString += "setupSystem";
            break;
        case throttleMax:
            telemetryString += "throttleMax";
            break;
        case waitingButtonRelease1:
            telemetryString += "waitingButtonRelease1";
            break;
        case throttleMin:
            telemetryString += "throttleMin";
            break;
        case waitingButtonRelease2:
            telemetryString += "waitingButtonRelease2";
            break;
        case startWipe1:
            telemetryString += "startWipe1";
            break;
        case endWipe1:
            telemetryString += "endWipe1";
            break;
        case editMotorTime:
            telemetryString += "editMotorTime";
            break;
        case startWipe2:
            telemetryString += "startWipe2";
            break;
        case endWipe2:
            telemetryString += "endWipe2";
            break;
        case editDtTime:
            telemetryString += "editDtTime";
            break;
        case saveEepromSettings:
            telemetryString += "saveEepromSettings";
            break;
        case waitingButtonStart:
            telemetryString += "waitingButtonStart";
            break;
        case clearGraphArrays:
            telemetryString += "clearGraphArrays";
            break;
        case waitingButtonRelease:
            telemetryString += "waitingButtonRelease";
            break;
        case motorRun:
            telemetryString += "motorRun";
            break;
        case freeFlight:
            telemetryString += "freeFlight";
            break;
        case triggerDT:
            telemetryString += "triggerDT";
            break;
        case waitingForRestart:
            telemetryString += "waitingForRestart";
            break;
        case waitingFirmwareUpdate:
            telemetryString += "waitingFirmwareUpdate";
            break;
        case dtRemoteConfig:
            telemetryString += "dtRemoteConfig";
            break;
        case dtRemote:
            telemetryString += "dtRemote";
            break;
    }
    telemetryString += "<br/>";
    int servoPosition = dtServo.read();
    int escPosition = escServo.read();
    telemetryString += "servoPosition: ";
    telemetryString += servoPosition;
    telemetryString += "<br/>";
    telemetryString += "escPosition: ";
    telemetryString += escPosition;
    telemetryString += "<br/>";
    telemetryString += "dethermalSeconds: ";
    telemetryString += dethermalSeconds[dethermalSecondsIndex];
    telemetryString += "<br/>";
    telemetryString += "motorSeconds: ";
    telemetryString += motorSeconds[motorSecondsIndex];
    telemetryString += "<br/>";
    telemetryString += "hasPressureSensor: ";
    telemetryString += hasPressureSensor;
    telemetryString += "<br/>";
    telemetryString += "isTimer: ";
    telemetryString += isTimer;
    telemetryString += "<br/>";
    telemetryString += "ADC: ";
    telemetryString += analogRead(A0);
    telemetryString += "<br/>";
    telemetryString += "voltage: ";
    //telemetryString += (analogRead(A0) / 69.0);
    telemetryString += (ESP.getVcc() / 1000.0);
    telemetryString += "v";
    // 339 @ 4.946v
    // 286 @ 4.142v
    // voltage divider: 39k+3k
    // 339÷4.496=68.5402
    // 286÷4.142=69,048768711
    telemetryString += "<br/>";
    telemetryString += "lastStateChangeMs: ";
    telemetryString += (millis() - lastStateChangeMs);
    telemetryString += "<br/>";
    telemetryString += "RSSI: ";
    telemetryString += WiFi.RSSI();
    telemetryString += "dBm<br/>";
    unsigned long udpSendCycleCount = ESP.getCycleCount();
    Udp.beginPacket(remoteIP, localUdpPort);
    Udp.write("PingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPingPing");
    Udp.endPacket();
    telemetryString += ("UDP send time: ");
    telemetryString += (ESP.getCycleCount() - udpSendCycleCount);
    telemetryString += (" cycles");
    telemetryString += "<br/>";

    if (hasPressureSensor) {
        double altitude, temperature, pressure;
        if (getPressure(temperature, pressure, altitude)) {
            telemetryString += temperature;
            telemetryString += " temperature<br/>";
            telemetryString += pressure;
            telemetryString += " mb<br/>";
            telemetryString += altitude;
            telemetryString += " meters<br/>";
            telemetryString += altitude * 3.28084;
            telemetryString += " feet<br/>";
        }
    }
    return telemetryString;
}

void spinUpMotor(int targetSpeed) {
    int escValue = escServo.read();
    if (targetSpeed > escValue) {
        int escPosition = escValue + (int) ((targetSpeed - escValue)*((millis() - lastStateChangeMs) / (float) escSpinUpMs));
        escPosition = (escPosition < targetSpeed) ? escPosition : targetSpeed;
        escPosition = (escPosition > escValue) ? escPosition : escValue;
        escServo.write(escPosition);
    } else {
        escServo.write(targetSpeed);
    }
}

void spinDownMotor() {
    int remainingTime = (motorSeconds[motorSecondsIndex] * 1000) - (millis() - lastStateChangeMs);
    int spinDownValue = MinPwm + (int) ((MaxPwm - MinPwm) * (remainingTime / (float) escSpinDownMs));
    int escPosition = (remainingTime > 0) ? spinDownValue : MinPwm;
    escPosition = (escPosition > MinPwm) ? escPosition : MinPwm;
    escPosition = (escPosition < MaxPwm) ? escPosition : MaxPwm;
    escServo.write(escPosition);
}

void updateStartWipe(enum MachineState completionState) {
    int wipeValue = MinPwm + (int) ((MaxPwm - MinPwm)*((millis() - lastStateChangeMs) / (float) servoWipeMs));
    wipeValue = (wipeValue < MaxPwm) ? wipeValue : MaxPwm;
    wipeValue = (wipeValue > MinPwm) ? wipeValue : MinPwm;
    dtServo.write(wipeValue);
    if (wipeValue >= MaxPwm) {
        machineState = completionState;
        lastStateChangeMs = millis();
        editingStartMs = millis();
    }
}

int updateEndWipe() {
    int servoValue = dtServo.read();
    int wipeValue = MaxPwm - (int) ((MaxPwm - MinPwm)*((millis() - lastStateChangeMs) / (float) servoWipeMs));
    wipeValue = (wipeValue < servoValue) ? wipeValue : servoValue;
    wipeValue = (wipeValue > MinPwm) ? wipeValue : MinPwm;
    dtServo.write(wipeValue);
    return wipeValue;
}

void updateEndWipe(enum MachineState completionState) {
    int wipeValue = updateEndWipe();
    if (wipeValue <= MinPwm) {
        machineState = completionState;
        lastStateChangeMs = millis();
        editingStartMs = millis();
    }
}

void displayMotorTime() {
    // show the steps / divisions before resting at the selected value
    int indexToDisplay = (millis() - lastStateChangeMs) / displayStepMs;
    int servoPosition;
    if (indexToDisplay <= motorSecondsIndex) {
        int stepValue = MinPwm + ((MaxPwm - MinPwm) * (indexToDisplay) / (motorSecondsSize - 1));
        editingStartMs = millis();
        servoPosition = stepValue;
    } else {
        int displayValue = MinPwm + ((MaxPwm - MinPwm) * motorSecondsIndex / (motorSecondsSize - 1));
        servoPosition = displayValue;
    }
    dtServo.write(servoPosition);
}

void displayDethermalTime() {
    // show the steps / divisions before resting at the selected value
    int indexToDisplay = (millis() - lastStateChangeMs) / displayStepMs;
    int servoPosition;
    if (indexToDisplay <= dethermalSecondsIndex) {
        int stepValue = MinPwm + ((MaxPwm - MinPwm) * (indexToDisplay) / (dethermalSecondsSize - 1));
        editingStartMs = millis();
        servoPosition = stepValue;
    } else {
        int displayValue = MinPwm + ((MaxPwm - MinPwm) * dethermalSecondsIndex / (dethermalSecondsSize - 1));
        servoPosition = displayValue;
    }
    dtServo.write(servoPosition);
}

void powerUpDt() {
    dtServo.attach(ServoPWM, 1000, 2000); // enable the servo output
    //dtServo.write(0);
    //    Serial.println("servo attach");
}

void powerUpEsc() {
    escServo.attach(EscPWM, 1000, 2000); // enable the ESC output
    //escServo.write(0);
    //    Serial.println("esc attach");
}

void powerUp() {
    powerUpDt();
    powerUpEsc();
}

void checkPowerDown() {
    if (millis() - lastStateChangeMs > powerDownServoMs) {
        if (dtServo.attached()) {
            // power down the servo after the given delay
            dtServo.detach(); // disable the servo output
            //            Serial.println("servo detach");
        }
    }
    if (millis() - lastStateChangeMs > powerDownEscSeconds) {
        if (escServo.attached()) {
            // power down the ESC after the given delay
            escServo.detach(); // disable the ESC output
            //            Serial.println("esc detach");
        }
    }
}

bool checkDtPacket() {
    int packetSize = Udp.parsePacket();
    if (packetSize) {
        if (Udp.peek() == 'd') {
            // discard any remaining data that would become stale
            Udp.flush();
            return true;
        }
        int currentIndex = ((millis() - lastMotorSpinUpMs) / 1000);
        int currentBufferIndex = currentIndex % historyLength;
        TelemetryData telemetryData;
        Udp.read(telemetryData.asBytes, sizeof (telemetryData.asBytes));
        Serial.print("dtStatus: ");
        Serial.println(telemetryData.data.dtStatus);
        Serial.print("voltage: ");
        Serial.println(telemetryData.data.voltage);
        Serial.print("rssi: ");
        Serial.println(telemetryData.data.rssi);
        remoteVoltageHistory[currentBufferIndex] = telemetryData.data.voltage / 1000.0;
        remoteRssiHistory[currentBufferIndex] = telemetryData.data.rssi;
        // discard any remaining data that would become stale
        Udp.flush();
    }
    return false;
}

void updateHistory() {
    int currentIndex = ((millis() - lastMotorSpinUpMs) / 1000);
    if (currentIndex != historyIndex) {
        int currentBufferIndex = currentIndex % historyLength;
        float temperature = (hasPressureSensor) ? pressureSensor.readTemperature() : 0;
        float altitude = (hasPressureSensor) ? pressureSensor.readAltitude(baselinePressure) : 0;
        temperatureHistory[currentBufferIndex] = temperature;
        altitudeHistory[currentBufferIndex] = altitude;
        escHistory[currentBufferIndex] = (escServo.readMicroseconds() - 1000) / 10;
        dtHistory[currentBufferIndex] = (dtServo.readMicroseconds() - 1000) / 10;
        maxSvgValue = (maxSvgValue < altitudeHistory[currentBufferIndex]) ? altitudeHistory[currentBufferIndex] : maxSvgValue;
        maxSvgValue = (maxSvgValue < temperatureHistory[currentBufferIndex]) ? temperatureHistory[currentBufferIndex] : maxSvgValue;
        maxSvgValue = (maxSvgValue < escHistory[currentBufferIndex]) ? escHistory[currentBufferIndex] : maxSvgValue;
        maxSvgValue = (maxSvgValue < dtHistory[currentBufferIndex]) ? dtHistory[currentBufferIndex] : maxSvgValue;

        historyIndex = currentIndex;
    }
}

void machineStateISR() {
    int servoPosition = dtServo.read();
    int escPosition = escServo.read();
    switch (machineState) {
        case setupSystem:
            break;
        case throttleMax:
            servoPosition = (servoPosition + 2 < MaxPwm) ? servoPosition + 2 : MaxPwm;
            dtServo.write(servoPosition);
            if (servoPosition >= MaxPwm) {
                powerUpEsc();
                escServo.write(servoPosition);
                machineState = waitingButtonRelease1;
                lastStateChangeMs = millis();
            }
            break;
        case waitingButtonRelease1:
            if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                if (ButtonIsDown) {
                    if (buttonHasBeenUp == 1) {
                        machineState = throttleMin;
                        buttonHasBeenUp = 0;
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonLastChangeMs = millis();
            }
            break;
        case throttleMin:
            servoPosition = (servoPosition - 2 > MinPwm) ? servoPosition - 2 : MinPwm;
            dtServo.write(servoPosition);
            escServo.write(servoPosition);
            if (servoPosition <= MinPwm) {
                machineState = waitingButtonRelease2;
                lastStateChangeMs = millis();
            }
            break;
        case waitingButtonRelease2:
            if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                if (ButtonIsDown) {
                    if (buttonHasBeenUp == 1) {
                        machineState = throttleMax;
                        buttonHasBeenUp = 0;
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonLastChangeMs = millis();
            }
            break;
        case startWipe1:
            updateStartWipe(endWipe1);
            break;
        case endWipe1:
            updateEndWipe(editMotorTime);
            break;
        case editMotorTime:
            displayMotorTime();
            if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                if (ButtonIsDown) {
                    if (buttonHasBeenUp == 1) {
                        // adjust motorSeconds
                        motorSecondsIndex = (motorSecondsIndex < motorSecondsSize - 1) ? motorSecondsIndex + 1 : 0;
                        editingStartMs = millis();
                        buttonHasBeenUp = 0;
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonLastChangeMs = millis();
            }
            if (millis() > editingStartMs + editingTimeoutMs) {
                machineState = startWipe2;
                lastStateChangeMs = millis();
            }
            doubleFlash();
            break;
        case startWipe2:
            updateStartWipe(endWipe2);
            break;
        case endWipe2:
            updateEndWipe(editDtTime);
            break;
        case editDtTime:
            displayDethermalTime();
            if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                if (ButtonIsDown) {
                    if (buttonHasBeenUp == 1) {
                        // adjust dethermalSeconds
                        dethermalSecondsIndex = (dethermalSecondsIndex < dethermalSecondsSize - 1) ? dethermalSecondsIndex + 1 : 0;
                        lastStateChangeMs = millis();
                        buttonHasBeenUp = 0;
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonLastChangeMs = millis();
            }
            if (millis() > editingStartMs + editingTimeoutMs) {
                // we leave the ESC powered down until this point because some ESCs have timing issues that the bootloader delay seems to affect
                powerUpEsc();
                machineState = saveEepromSettings;
                lastStateChangeMs = millis();
            }
            trippleFlash();
            break;
        case waitingButtonStart:
            checkPowerDown();
            updateEndWipe();
            if (servoPosition <= MinPwm) {
                if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                    if (ButtonIsDown) {
                        if (buttonHasBeenUp == 1) {
                            machineState = clearGraphArrays;
                            lastStateChangeMs = millis();
                            buttonHasBeenUp = 0;
                            powerUp();
                        }
                    } else {
                        buttonHasBeenUp = 1;
                    }
                    buttonLastChangeMs = millis();
                }
                slowFlash();
            }
            break;
        case waitingButtonRelease:
            spinUpMotor(waitingEscValue); // spin up the motor to the waiting speed
            if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                if (!ButtonIsDown) {
                    if (buttonHasBeenUp == 0) {
                        machineState = motorRun;
                        lastStateChangeMs = millis();
                        buttonHasBeenUp = 1;
                        currentFlightMs = millis();
                        flightStartIndex = historyIndex;
                    }
                } else {
                    buttonHasBeenUp = 0;
                }
                buttonLastChangeMs = millis();
            }
            fastFlash();
            break;
        case motorRun:
            if (millis() - lastStateChangeMs + escSpinDownMs > motorSeconds[motorSecondsIndex] * 1000) {
                spinDownMotor(); // power down and switch state
                if (escPosition <= MinPwm) {
                    TurnOffLed;
                    machineState = freeFlight;
                    // do not reset the pwmCycleCount here because the DT time should overlap the motor run time
                } else {
                    TurnOnLed;
                }
            } else {
                if (escPosition >= MaxPwm) {
                    TurnOffLed;
                } else {
                    TurnOnLed;
                    spinUpMotor(MaxPwm);
                }
                if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                    // allow restarts starts
                    if (ButtonIsDown) {
                        if (buttonHasBeenUp == 1) {
                            machineState = waitingButtonStart;
                            lastStateChangeMs = millis();
                            buttonHasBeenUp = 0;
                            // power down the motor in the case of restarts
                            escPosition = MinPwm;
                            escServo.write(escPosition);
                        }
                    } else {
                        buttonHasBeenUp = 1;
                    }
                    buttonLastChangeMs = millis();
                }
            }
            break;
        case freeFlight:
            if (millis() - lastStateChangeMs > dethermalSeconds[dethermalSecondsIndex]*1000) {
                //                Serial.print("dethermalSeconds ");
                machineState = triggerDT;
                lastStateChangeMs = millis();
            }
            break;
        case triggerDT:
            escPosition = MinPwm; // power down the motor in the case of RC DT
            escServo.write(escPosition);
            // todo: should this skip the smooth servo movement and just jump to dt so that it is the quickest possible
            updateStartWipe(waitingForRestart);
            break;
        case waitingForRestart:
            checkPowerDown();
            if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                if (ButtonIsDown) {
                    if (buttonHasBeenUp == 1) {
                        machineState = waitingButtonStart;
                        buttonHasBeenUp = 0;
                        lastStateChangeMs = millis();
                        powerUp();
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonLastChangeMs = millis();
            }
            fastFlash();
            break;
    }
}

void loop() {
    switch (machineState) {
        case saveEepromSettings:
            saveSettings();
            machineState = waitingButtonStart;
            break;
        case clearGraphArrays:
            machineState = waitingButtonRelease;
            // zero the historyIndex and zero the history buffer
            lastMotorSpinUpMs = millis();
            flightIdRandom1 = random(1000, 9999); // @todo: the random seed has not been set, check that this will not cause issues in this use case.
            flightIdRandom2 = random(1000, 9999);
            memset(temperatureHistory, 0, sizeof (temperatureHistory));
            memset(altitudeHistory, 0, sizeof (altitudeHistory));
            memset(escHistory, 0, sizeof (escHistory));
            memset(dtHistory, 0, sizeof (dtHistory));
            memset(remoteRssiHistory, 0, sizeof (remoteRssiHistory));
            memset(remoteVoltageHistory, 0, sizeof (remoteVoltageHistory));
            break;
        case dtRemoteConfig:
            if (WiFi.status() == WL_CONNECTED) {
                dnsServer.stop();
                webServer.stop();
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);
                machineState = dtRemote;
            }
            break;
        case dtRemote:
            TelemetryData telemetryData;
            if (ButtonIsDown) {
                telemetryData.data.dtStatus = 'd';
                Udp.beginPacket(timerIP, localUdpPort);
                Udp.write(telemetryData.asBytes, sizeof (telemetryData));
                Udp.endPacket();
            } else {
                int currentIndex = ((millis() - lastMotorSpinUpMs) / 1000);
                if (currentIndex != udpHistoryIndex) {
                    telemetryData.data.dtStatus = 'r';
                    telemetryData.data.rssi = WiFi.RSSI();
                    telemetryData.data.voltage = ESP.getVcc();
                    //                    Serial.print("rssi: ");
                    //                    Serial.println(telemetryData.data.rssi);
                    //                    Serial.print("voltage: ");
                    //                    Serial.println(telemetryData.data.voltage);
                    udpHistoryIndex = currentIndex;
                    Udp.beginPacket(timerIP, localUdpPort);
                    Udp.write(telemetryData.asBytes, sizeof (telemetryData));
                    Udp.endPacket();
                }
            }
            int packetSize = Udp.parsePacket();
            if (packetSize) {
                //                Serial.print("Found packet");
                Udp.flush();
            }
            break;
    }
    if (machineState == dtRemoteConfig) {
        dnsServer.processNextRequest();
        webServer.handleClient();
    } else if (machineState != dtRemote) {
        updateHistory();
        bool foundDtPacket = checkDtPacket();
        if (foundDtPacket || RcDtIsActive) { // respond to an RC DT trigger regardless of the machine state
            machineState = triggerDT;
            lastStateChangeMs = millis();
        } else {
            dnsServer.processNextRequest();
            webServer.handleClient();
        }
    }
    yield();
}

void setupRegisters() {
    pinMode(IndicatorLed, OUTPUT); // set the LED to output
    pinMode(ButtonPin, INPUT); // set the button to input
    pinMode(ButtonPin, INPUT_PULLUP); // activate the internal pull up resistor
    attachInterrupt(ButtonPin, pinChangeInterrupt, CHANGE);
    //    powerUp();
    dtServo.write(MinPwm); // set the servo to the minimum for now
    escServo.write(MinPwm); // set the ESC to the minimum for now
    pinMode(A0, INPUT);
}

void getTelemetry() {
    //    Serial.print("getTelemetry");
    webServer.send(200, "text/html", getTelemetryString());
}

void getSettingsJson() {
    String returnSettinsJson = "{";
    returnSettinsJson += "\"settings\": [{\
    \"index\": ";
    returnSettinsJson += dethermalSecondsIndex;
    returnSettinsJson += ",\
            \"value\": ";
    returnSettinsJson += dethermalSeconds[dethermalSecondsIndex];
    returnSettinsJson += ",\
            \"values\": [0, 5, 30, 60, 90, 120, 180, 240, 300],\
            \"description\": \"Dethermal Seconds\",\
            \"name\": \"dethermalSeconds\",\
            \"links\": [{\
                    \"rel\": \"+\",\
                    \"href\": \"dtIncrease\"\
                }, {\
                    \"rel\": \"-\",\
                    \"href\": \"dtDecrease\"\
                }]\
        }, {\
            \"index\": ";
    returnSettinsJson += motorSecondsIndex;
    returnSettinsJson += ",\
            \"value\": ";
    returnSettinsJson += motorSeconds[motorSecondsIndex];
    returnSettinsJson += ",\
            \"description\": \"Motor Seconds\",\
            \"name\": \"motorSeconds\",\
            \"values\": [2, 4, 5, 7, 10, 13, 15],\
            \"links\": [{\
                    \"rel\": \"+\",\
                    \"href\": \"motorIncrease\"\
                }, {\
                    \"rel\": \"-\",\
                    \"href\": \"motorDecrease\"\
                }]\
        }, {\
            \"integer\": 30,\
            \"description\": \"Power Down Servo Seconds\",\
            \"name\": \"powerDownServoMs\",\
            \"links\": [{\
                    \"rel\": \"self\",\
                    \"href\": \"powerDownServoMs\"\
                }]\
        }, {\
            \"integer\": 300000,\
            \"description\": \"Power Down ESC Seconds\",\
            \"name\": \"powerDownEscSeconds\",\
            \"links\": [{\
                    \"rel\": \"self\",\
                    \"href\": \"powerDownEscSeconds\"\
                }]\
        }, {\
            \"boolean\": true,\
            \"description\": \"Pressure Sensor\",\
            \"name\": \"pressureSensor\",\
            \"links\": [{\
                    \"rel\": \"self\",\
                    \"href\": \"toggleSensors\"\
                }]\
        }, {\
            \"boolean\": true,\
            \"description\": \"Timer or Remote\",\
            \"name\": \"isTimer\",\
            \"links\": [{\
                    \"rel\": \"self\",\
                    \"href\": \"toggleTimerOrRemote\"\
                }]\
        }],\
    \"links\": [{\
            \"rel\": \"self\",\
            \"href\": \"settings\"\
        }]\
}   ";
    webServer.send(200, "text/html", returnSettinsJson);
}

//        webServer.on("/dethermalSeconds", HTTP_PUT, putDethermalSeconds);
//        webServer.on("/motorSeconds", HTTP_PUT, putMotorSeconds);
//        webServer.on("/powerDownServoMs", HTTP_PUT, putPowerDownServoMs);
//        webServer.on("/powerDownEscSeconds", HTTP_PUT, putPowerDownEscSeconds);
//        webServer.on("/hasPressureSensor", HTTP_PUT, putHasPressureSensor);

void getGraphData() {
    int maxRecords = (webServer.hasArg("start")) ? 100 : historyLength / 2;
    int startIndex = (webServer.hasArg("start")) ? webServer.arg("start").toInt() : 0;
    startIndex = (startIndex > historyIndex - historyLength) ? startIndex : historyIndex - historyLength;
    int endIndex = (startIndex + maxRecords < historyLength) ? startIndex + maxRecords : historyLength;
    String graphData = "{";
    graphData += getTelemetryJson();
    graphData += "; historyIndex: ";
    graphData += historyIndex;
    graphData += "; flightId: ";
    graphData += lastMotorSpinUpMs;
    graphData += "-";
    graphData += flightIdRandom1;
    graphData += "-";
    graphData += flightIdRandom2;
    graphData += "; currentFlightMs: ";
    graphData += millis() - currentFlightMs;
    graphData += "; flightStartIndex: ";
    graphData += flightStartIndex;
    graphData += "; startIndex: ";
    graphData += startIndex;
    graphData += "; voltage: ";
    // battery 3.83v = "voltage":2.61
    graphData += (ESP.getVcc() / 1000.0);
    graphData += "; altitudeHistory: [";
    for (int currentIndex = startIndex; currentIndex < endIndex; currentIndex++) {
        graphData += altitudeHistory[currentIndex % historyLength];
        graphData += ", ";
    }
    graphData += "];";
    graphData += "temperatureHistory: [";
    for (int currentIndex = startIndex; currentIndex < endIndex; currentIndex++) {
        graphData += temperatureHistory[currentIndex % historyLength];
        graphData += ", ";
    }
    graphData += "];";
    graphData += "dtHistory: [";
    for (int currentIndex = startIndex; currentIndex < endIndex; currentIndex++) {
        graphData += (dtHistory[currentIndex % historyLength] / 10);
        graphData += ", ";
    }
    graphData += "];";
    graphData += "escHistory: [";
    for (int currentIndex = startIndex; currentIndex < endIndex; currentIndex++) {
        graphData += (escHistory[currentIndex % historyLength] / 10);
        graphData += ", ";
    }
    graphData += "];";
    graphData += "remoteRssiHistory: [";
    for (int currentIndex = startIndex; currentIndex < endIndex; currentIndex++) {
        graphData += (remoteRssiHistory[currentIndex % historyLength]);
        graphData += ", ";
    }
    graphData += "];";
    graphData += "remoteVoltageHistory: [";
    for (int currentIndex = startIndex; currentIndex < endIndex; currentIndex++) {
        graphData += (remoteVoltageHistory[currentIndex % historyLength]);
        graphData += ", ";
    }
    graphData += "]}";
    webServer.send(200, "text/html", graphData);
}

void getGraphSvg() {
    String escColour = "#a92c10";
    String dtColour = "#d86b00";
    String temperatureColour = "#495c65";
    String altitudeColour = "#2c353e";

    String dataType = (webServer.hasArg("download")) ? "application/octet-stream" : "image/svg+xml";
    webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    String startSvg = "<svg xmlns=\"http://www.w3.org/2000/svg\" height=\"";
    startSvg += maxSvgValue;
    startSvg += "\" width=\"512\">";

    webServer.send(200, dataType, startSvg);

    webServer.sendContent("<polyline points=\"");
    for (int currentIndex = 0; currentIndex < historyLength; currentIndex++) {
        String altitudePoints = "";
        altitudePoints += currentIndex;
        altitudePoints += ",";
        altitudePoints += maxSvgValue - altitudeHistory[currentIndex];
        altitudePoints += " ";
        webServer.sendContent(altitudePoints);
    }
    webServer.sendContent("\" style=\"fill:none;stroke:" + altitudeColour + ";stroke-width:1\" />");

    webServer.sendContent("<polyline points=\"");
    for (int currentIndex = 0; currentIndex < historyLength; currentIndex++) {
        String temperaturePoints = "";
        temperaturePoints += currentIndex;
        temperaturePoints += ",";
        temperaturePoints += maxSvgValue - temperatureHistory[currentIndex];
        temperaturePoints += " ";
        webServer.sendContent(temperaturePoints);
    }
    webServer.sendContent("\" style=\"fill:none;stroke:" + temperatureColour + ";stroke-width:1\" />");

    webServer.sendContent("<polyline points=\"");
    for (int currentIndex = 0; currentIndex < historyLength; currentIndex++) {
        String escPoints = "";
        escPoints += currentIndex;
        escPoints += ",";
        escPoints += maxSvgValue - escHistory[currentIndex];
        escPoints += " ";
        webServer.sendContent(escPoints);
    }
    webServer.sendContent("\" style=\"fill:none;stroke:" + escColour + ";stroke-width:1\" />");

    webServer.sendContent("<polyline points=\"");
    for (int currentIndex = 0; currentIndex < historyLength; currentIndex++) {
        String dtPoints = "";
        dtPoints += currentIndex;
        dtPoints += ",";
        dtPoints += maxSvgValue - dtHistory[currentIndex];
        dtPoints += " ";
        webServer.sendContent(dtPoints);
    }
    webServer.sendContent("\" style=\"fill:none;stroke:" + dtColour + ";stroke-width:1\" />");

    webServer.sendContent("</svg>");
}

void defaultPage() {
    //Serial.print("defaultPage");
    webServer.send(200, "text/html", "<!DOCTYPE html><html><head><title>E36</title>"
            //            "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.1.1/jquery.min.js'></script>"
            "<script>"
            //            "$('#remoteButton').click(function(){"
            //            "$('#buttonResult').load('button');"
            //            "$('#telemetryResult').load('telemetry');"
            //                        "});"
            "function requestUpdate() {"
            "var xhttp = new XMLHttpRequest();"
            "xhttp.onreadystatechange = function() {"
            "if (this.readyState == 4 && this.status == 200) {"
            "document.getElementById('telemetryResult').innerHTML = this.responseText;"
            "setTimeout(requestUpdate, 1000);"
            "}"
            "};"
            "xhttp.open('GET', 'telemetry', true);"
            "xhttp.send();"
            "}"
            "setTimeout(requestUpdate, 1000)"
            "</script>"
            "</head><body>"
            "<h1>Telemetry Data</h1>"
            //            "<button id='remoteButton'>remoteButton</button>"
            //            "<div id='buttonResult'>buttonResult</div>"
            "<br/>"
            //            "<a href='motorRun'>motorRun</a><br/><br/>"
            "<a href='triggerDT'>triggerDT</a><br/><br/>"
            "<a href='restart'>restart</a><br/><br/><br/>"
            //            "<br/>"
            //            "<button id='buttonUpdate' onclick='location.reload();'>update</button>"
            "<div id='telemetryResult'>"
            + getTelemetryString() +
            "</div>"
            "<a href='motorDecrease'>motorDecrease</a>&nbsp;"
            "<a href='motorIncrease'>motorIncrease</a><br/><br/>"
            "<a href='dtDecrease'>dtDecrease</a>&nbsp;"
            "<a href='dtIncrease'>dtIncrease</a><br/><br/>"
            "<a href='toggleSensors'>toggleSensors</a><br/><br/>"
            "<a href='toggleTimerOrRemote'>toggleTimerOrRemote</a><br/><br/>"
            "<a href='saveChanges'>saveChanges</a><br/><br/>"
            "<a href='settings'>Settings JSON</a><br/><br/>"
            "<a href='requestFirmwareUpdate'>FirmwareUpdate</a><br/><br/>"
            "<a href='graph.json'>Graph JSON</a><br/><br/>"
            "<a href='graph.svg'>Graph SVG</a><br/><br/>"
            "<a href='graph.svg?download'>Download SVG</a><br/><br/>"
            "</body></html>");
}

void motorDecrease() {
    motorSecondsIndex--;
    motorSecondsIndex = (motorSecondsIndex < 0) ? 0 : motorSecondsIndex;
    //displayMotorTime();
    defaultPage();
}

void motorIncrease() {
    motorSecondsIndex++;
    motorSecondsIndex = (motorSecondsIndex < motorSecondsSize) ? motorSecondsIndex : motorSecondsSize - 1;
    //displayMotorTime();
    defaultPage();
}

void dtDecrease() {
    dethermalSecondsIndex--;
    dethermalSecondsIndex = (dethermalSecondsIndex < 0) ? 0 : dethermalSecondsIndex;
    //displayDethermalTime();
    defaultPage();
}

void dtIncrease() {
    dethermalSecondsIndex++;
    dethermalSecondsIndex = (dethermalSecondsIndex < dethermalSecondsSize) ? dethermalSecondsIndex : dethermalSecondsSize - 1;
    //displayDethermalTime();
    defaultPage();
}

void saveChanges() {
    saveSettings();
    machineState = startWipe1;
    defaultPage();
}

void requestFirmwareUpdate() {
    String responseString = "<!DOCTYPE html><html><head><title>E36</title></head><body><h1>Firmware Update</h1><br/><br/>";
    int responseCode;
    if (machineState == waitingFirmwareUpdate && ButtonIsDown) {
        httpUpdater.setup(&webServer);
        responseString += "Update service ready";
        responseString += "<br/><br/><a href='update'>upload form</a>";
        responseCode = 200;
    } else {
        machineState = waitingFirmwareUpdate;
        responseString += "Please hold down the button to begin update";
        responseCode = 403;
    }
    webServer.send(responseCode, "text/html", responseString + "</body></html>");
}

void getTriggerDT() {
    lastStateChangeMs = millis();
    machineState = triggerDT;
    defaultPage();
}

void getWaitingButtonStart() {
    // power down the motor in the case of restarts
    escServo.write(MinPwm);
    lastStateChangeMs = millis();
    powerUp();
    machineState = waitingButtonStart;
    defaultPage();
}

void toggleSensors() {
    hasPressureSensor = !hasPressureSensor;
    webServer.sendContent((hasPressureSensor) ? "using sensors" : "ignoring sensors");
}

void toggleTimerOrRemote() {
    isTimer = !isTimer;
    webServer.sendContent((isTimer) ? "isTimer" : "isRemote");
}

void setup() {
    Serial.begin(115200);
    delay(10);
    Serial.print("E36");
    loadSavedSettings();
    setupRegisters();
    if (isTimer) {
        attachInterrupt(1, pinChangeInterrupt, CHANGE);
        if (ButtonIsDown) {
            machineState = throttleMax;
        } else {
            machineState = startWipe1;
            powerUpDt();
        }
        pwmTweenTimer.attach_ms(100, machineStateISR);
        WiFi.mode(WIFI_AP);
    } else {
        WiFi.mode(WIFI_AP_STA);
        WiFi.config(remoteIP, timerIP, IPAddress(255, 255, 255, 0));
        WiFi.begin((ssid[0] != 0) ? ssid : "E36 Timer"); // not using , password yet
        machineState = dtRemoteConfig;
    }
    WiFi.softAPConfig(timerIP, timerIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP((ssid[0] != 0) ? ssid : "E36 Timer"); // not using , password yet

    // if DNSServer is started with "*" for domain name, it will reply with
    // provided IP to all DNS request
    dnsServer.start(DNS_PORT, "*", timerIP);
    if (isTimer) {
        webServer.on("/graph.json", getGraphData);
        webServer.on("/graph.svg", getGraphSvg);
        webServer.on("/triggerDT", getTriggerDT);
        //        webServer.on("/motorRun", getMotorRun);
        webServer.on("/restart", getWaitingButtonStart);
        webServer.on("/motorDecrease", motorDecrease);
        webServer.on("/motorIncrease", motorIncrease);
        webServer.on("/dtDecrease", dtDecrease);
        webServer.on("/dtIncrease", dtIncrease);
        webServer.on("/toggleSensors", toggleSensors);
    }
    webServer.on("/telemetry", getTelemetry);
    webServer.on("/saveChanges", saveChanges);
    webServer.on("/toggleTimerOrRemote", toggleTimerOrRemote);
    webServer.on("/requestFirmwareUpdate", requestFirmwareUpdate);
    webServer.on("/settings", HTTP_GET, getSettingsJson);
    //webServer.on("/dethermalSeconds", HTTP_PUT, putDethermalSeconds);
    //webServer.on("/motorSeconds", HTTP_PUT, putMotorSeconds);
    //webServer.on("/powerDownServoMs", HTTP_PUT, putPowerDownServoMs);
    //webServer.on("/powerDownEscSeconds", HTTP_PUT, putPowerDownEscSeconds);
    //webServer.on("/hasPressureSensor", HTTP_PUT, putHasPressureSensor);
    webServer.onNotFound(defaultPage);
    webServer.begin();
    Udp.begin(localUdpPort);

    struct rst_info * resetInfo = ESP.getResetInfoPtr();
    Serial.println(ESP.getResetReason());
    if (resetInfo->reason == REASON_EXCEPTION_RST) {
        // if the BMP180 is not present then the read pressure will cause an exception, so we disable it here if an exception caused a restart
        hasPressureSensor = false;
        Serial.println("Exception reset occurred, disabling the pressure sensor");
    }
    if (hasPressureSensor) {
        Wire.pins(SdaPin, SclPin);
        pressureSensor.begin();
        //    double temperature, pressure, altitude;
        //    getPressure(temperature, pressure, altitude);
        baselinePressure = pressureSensor.readPressure();

        Serial.print("baseline pressure: ");
        Serial.print(baselinePressure);
        Serial.println(" mb");
    }
}
