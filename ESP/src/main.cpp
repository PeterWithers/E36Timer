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
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <Sodaq_BMP085.h>
#include <Wire.h>
WiFiUDP Udp;
unsigned int localUdpPort = 2222;

const byte DNS_PORT = 53;
IPAddress timerIP(192, 168, 1, 1);
IPAddress remoteIP(192, 168, 1, 2);
DNSServer dnsServer;
ESP8266WebServer webServer(80);

Sodaq_BMP085 pressureSensor;
double baselinePressure;
bool hasPressureSensor = true;

int historyLength = 1024;
int historyIndex = 0;
float temperatureHistory[1024];
float altitudeHistory[1024];
int escHistory[1024];
int dtHistory[1024];
int maxSvgValue = 0;

//#define TRIGGER_PIN  3
//#define ECHO_PIN     2
//#define MAX_DISTANCE 400

//NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);
// end temporary defines

#define IndicatorLed     2
#define ServoPWM         1 // shared with TX
#define EscPWM           3 // shared with RX
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
    waitingButtonStart,
    waitingButtonRelease,
    motorRun,
    freeFlight,
    triggerDT,
    waitingForRestart,
    dtRemote
};

volatile enum MachineState machineState = setupSystem;

volatile unsigned long buttonLastChangeMs = 0;
volatile unsigned long buttonDebounceMs = 100;

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

void saveSettings() {
    boolean changeMade = false;
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
    if (changeMade) {
        Serial.print(" writing changes");
        EEPROM.commit();
    } else {
        Serial.print(" unchanged no commit");
    }
}

void loadSavedSettings() {
    motorSecondsIndex = EEPROM.read(1);
    dethermalSecondsIndex = EEPROM.read(2);
    motorSecondsIndex = (motorSecondsIndex < motorSecondsSize) ? motorSecondsIndex : motorSecondsSize - 1;
    dethermalSecondsIndex = (dethermalSecondsIndex < dethermalSecondsSize) ? dethermalSecondsIndex : dethermalSecondsSize - 1;
}

void pinChangeInterrupt() {
    buttonLastChangeMs = millis();
}

void sendTelemetry() {
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
        case waitingButtonStart:
            returnString += "waitingButtonStart";
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
        case waitingButtonStart:
            telemetryString += "waitingButtonStart";
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
    telemetryString += "ADC: ";
    telemetryString += analogRead(A0);
    telemetryString += "<br/>";
    telemetryString += "voltage: ";
    telemetryString += (analogRead(A0) / 69.0);
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
        sendTelemetry();
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
        sendTelemetry();
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
    Serial.println("servo attach");
}

void powerUpEsc() {
    escServo.attach(EscPWM, 1000, 2000); // enable the ESC output
    //escServo.write(0);
    Serial.println("esc attach");
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
            Serial.println("servo detach");
        }
    }
    if (millis() - lastStateChangeMs > powerDownEscSeconds) {
        if (escServo.attached()) {
            // power down the ESC after the given delay
            escServo.detach(); // disable the ESC output
            Serial.println("esc detach");
        }
    }
}

bool checkDtPacket() {
    int packetSize = Udp.parsePacket();
    if (packetSize) {
        Udp.flush();
        return true;
    } else {
        return false;
    }
}

void updateHistory() {
    int currentIndex = (millis() / 1000) % historyLength;
    if (currentIndex != historyIndex) {
        float temperature = (hasPressureSensor) ? pressureSensor.readTemperature() : 0;
        float altitude = (hasPressureSensor) ? pressureSensor.readAltitude(baselinePressure) : 0;
        temperatureHistory[currentIndex] = temperature;
        altitudeHistory[currentIndex] = altitude;
        escHistory[currentIndex] = escServo.read();
        dtHistory[currentIndex] = dtServo.read();

        maxSvgValue = (maxSvgValue < altitudeHistory[currentIndex]) ? altitudeHistory[currentIndex] : maxSvgValue;
        maxSvgValue = (maxSvgValue < temperatureHistory[currentIndex]) ? temperatureHistory[currentIndex] : maxSvgValue;
        maxSvgValue = (maxSvgValue < escHistory[currentIndex]) ? escHistory[currentIndex] : maxSvgValue;
        maxSvgValue = (maxSvgValue < dtHistory[currentIndex]) ? dtHistory[currentIndex] : maxSvgValue;

        historyIndex = currentIndex;
    }
}

void loop() {
    int servoPosition = dtServo.read();
    int escPosition = escServo.read();
    bool foundDtPacket = checkDtPacket();
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
            sendTelemetry();
            break;
        case waitingButtonRelease1:
            if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                if (ButtonIsDown) {
                    if (buttonHasBeenUp == 1) {
                        machineState = throttleMin;
                        buttonHasBeenUp = 0;
                        sendTelemetry();
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
            sendTelemetry();
            break;
        case waitingButtonRelease2:
            if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                if (ButtonIsDown) {
                    if (buttonHasBeenUp == 1) {
                        machineState = throttleMax;
                        buttonHasBeenUp = 0;
                        sendTelemetry();
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
                        sendTelemetry();
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonLastChangeMs = millis();
            }
            if (millis() > editingStartMs + editingTimeoutMs) {
                machineState = startWipe2;
                lastStateChangeMs = millis();
                sendTelemetry();
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
                        sendTelemetry();
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonLastChangeMs = millis();
            }
            if (millis() > editingStartMs + editingTimeoutMs) {
                // we leave the ESC powered down until this point because some ESCs have timing issues that the bootloader delay seems to affect
                powerUpEsc();
                machineState = waitingButtonStart;
                lastStateChangeMs = millis();
                saveSettings();
                sendTelemetry();
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
                            machineState = waitingButtonRelease;
                            lastStateChangeMs = millis();
                            buttonHasBeenUp = 0;
                            powerUp();
                            sendTelemetry();
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
                        sendTelemetry();
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
                    sendTelemetry();
                } else {
                    TurnOnLed;
                }
                //                sendTelemetry();
            } else {
                if (escPosition >= MaxPwm) {
                    TurnOffLed;
                } else {
                    TurnOnLed;
                    spinUpMotor(MaxPwm);
                }
                if (RcDtIsActive || foundDtPacket) { // respond to an RC DT trigger
                    machineState = triggerDT;
                    lastStateChangeMs = millis();
                    sendTelemetry();
                } else if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                    // allow restarts starts
                    if (ButtonIsDown) {
                        if (buttonHasBeenUp == 1) {
                            machineState = waitingButtonStart;
                            lastStateChangeMs = millis();
                            buttonHasBeenUp = 0;
                            // power down the motor in the case of restarts
                            escPosition = MinPwm;
                            escServo.write(escPosition);
                            sendTelemetry();
                        }
                    } else {
                        buttonHasBeenUp = 1;
                    }
                    buttonLastChangeMs = millis();
                }
            }
            break;
        case freeFlight:
            //            sendTelemetry();
            if (RcDtIsActive || foundDtPacket) { // respond to an RC DT trigger
                Serial.print("RcDtIsActive ");
                machineState = triggerDT;
                lastStateChangeMs = millis();
                sendTelemetry();
            } else if (millis() - lastStateChangeMs > dethermalSeconds[dethermalSecondsIndex]*1000) {
                Serial.print("dethermalSeconds ");
                machineState = triggerDT;
                lastStateChangeMs = millis();
                sendTelemetry();
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
                        sendTelemetry();
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonLastChangeMs = millis();
            }
            fastFlash();
            break;
        case dtRemote:
            if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                if (ButtonIsDown) {
                    Udp.beginPacket(timerIP, localUdpPort);
                    Udp.write("ButtonDown");
                    Udp.endPacket();
                }
                buttonLastChangeMs = millis();
            }
            int packetSize = Udp.parsePacket();
            if (packetSize) {
                Serial.print("Found packet");
                Udp.flush();
            }
            break;
    }
    if (machineState != dtRemote) {
        updateHistory();
        dnsServer.processNextRequest();
        webServer.handleClient();
    }
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

void getGraphData() {
    int maxRecords = (webServer.hasArg("start")) ? 100 : historyLength / 2;
    int startIndex = (webServer.hasArg("start")) ? webServer.arg("start").toInt() : 0;
    int endIndex = (startIndex + maxRecords < historyLength) ? startIndex + maxRecords : historyLength;
    String graphData = "{";
    graphData += getTelemetryJson();
    graphData += "; historyIndex: ";
    graphData += historyIndex;
    graphData += "; startIndex: ";
    graphData += startIndex;
    graphData += "; altitudeHistory: [";
    for (int currentIndex = startIndex; currentIndex < endIndex; currentIndex++) {
        graphData += altitudeHistory[currentIndex];
        graphData += ", ";
    }
    graphData += "];";
    graphData += "temperatureHistory: [";
    for (int currentIndex = startIndex; currentIndex < endIndex; currentIndex++) {
        graphData += temperatureHistory[currentIndex];
        graphData += ", ";
    }
    graphData += "];";
    graphData += "dtHistory: [";
    for (int currentIndex = startIndex; currentIndex < endIndex; currentIndex++) {
        graphData += (dtHistory[currentIndex] / 10);
        graphData += ", ";
    }
    graphData += "];";
    graphData += "escHistory: [";
    for (int currentIndex = startIndex; currentIndex < endIndex; currentIndex++) {
        graphData += (escHistory[currentIndex] / 10);
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
            "<a href='motorRun'>motorRun</a><br/><br/>"
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
            "<a href='saveChanges'>saveChanges</a><br/><br/>"
            "<a href='graph.json'>Graph Data</a><br/><br/>"
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

void getMotorRun() {
    lastStateChangeMs = millis();
    machineState = motorRun;
    defaultPage();
}

void toggleSensors() {
    hasPressureSensor = !hasPressureSensor;
    webServer.sendContent((hasPressureSensor) ? "using sensors" : "ignoring sensors");
}

void setup() {
    Serial.begin(115200);
    delay(10);
    Serial.print("E36");
    EEPROM.begin(4);
    loadSavedSettings();
    setupRegisters();
    if (true) {
        attachInterrupt(1, pinChangeInterrupt, CHANGE);
        if (ButtonIsDown) {
            machineState = throttleMax;
            sendTelemetry();
        } else {
            machineState = startWipe1;
            powerUpDt();
            sendTelemetry();
        }
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(timerIP, timerIP, IPAddress(255, 255, 255, 0));
        WiFi.softAP("E36 Timer");

        // if DNSServer is started with "*" for domain name, it will reply with
        // provided IP to all DNS request
        dnsServer.start(DNS_PORT, "*", timerIP);

        webServer.on("/telemetry", getTelemetry);
        webServer.on("/graph.json", getGraphData);
        webServer.on("/graph.svg", getGraphSvg);
        webServer.on("/triggerDT", getTriggerDT);
        webServer.on("/motorRun", getMotorRun);
        webServer.on("/restart", getWaitingButtonStart);
        webServer.on("/motorDecrease", motorDecrease);
        webServer.on("/motorIncrease", motorIncrease);
        webServer.on("/dtDecrease", dtDecrease);
        webServer.on("/dtIncrease", dtIncrease);
        webServer.on("/saveChanges", saveChanges);
        webServer.on("/toggleSensors", toggleSensors);
        webServer.onNotFound(defaultPage);
        webServer.begin();
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.config(remoteIP, timerIP, IPAddress(255, 255, 255, 0));
        WiFi.begin("E36 Timer");
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
        }
        machineState = dtRemote;
    }
    Udp.begin(localUdpPort);

    Wire.pins(SdaPin, SclPin);
    pressureSensor.begin();
    //    double temperature, pressure, altitude;
    //    getPressure(temperature, pressure, altitude);
    baselinePressure = pressureSensor.readPressure();

    Serial.print("baseline pressure: ");
    Serial.print(baselinePressure);
    Serial.println(" mb");
}
