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


const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 1);
DNSServer dnsServer;
ESP8266WebServer webServer(80);

//#define TRIGGER_PIN  3
//#define ECHO_PIN     2
//#define MAX_DISTANCE 400

//NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);
// end temporary defines

#define IndicatorLed     2
#define ServoPWM         4
#define EscPWM           5
#define ButtonPin        12
#define RcDt1Pin         13
#define RcDt2Pin         14

Servo dtServo;
Servo escServo;

#define MaxPwm 180
#define MinPwm 0

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
    waitingForRestart
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
volatile int editingStartMs = 0;
volatile const unsigned long powerDownServoMs = 30 * 1000; // 30 seconds before the servo is powered down
volatile const unsigned long powerDownEscSeconds = 300 * 1000; // 5 minutes before the ESC is powered down

const int motorSeconds[] = {2, 4, 5, 7, 10, 13, 15};
const int motorSecondsSize = 6;
volatile int motorSecondsIndex = 0;
const int dethermalSeconds[] = {0, 5, 30, 60, 90, 120, 180, 240, 300};

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
    Serial.print("machineState: ");
    switch (machineState) {
        case setupSystem:
            Serial.print("setupSystem");
            break;
        case throttleMax:
            Serial.print("throttleMax");
            break;
        case waitingButtonRelease1:
            Serial.print("waitingButtonRelease1");
            break;
        case throttleMin:
            Serial.print("throttleMin");
            break;
        case waitingButtonRelease2:
            Serial.print("waitingButtonRelease2");
            break;
        case startWipe1:
            Serial.print("startWipe1");
            break;
        case endWipe1:
            Serial.print("endWipe1");
            break;
        case editMotorTime:
            Serial.print("editMotorTime");
            break;
        case startWipe2:
            Serial.print("startWipe2");
            break;
        case endWipe2:
            Serial.print("endWipe2");
            break;
        case editDtTime:
            Serial.print("editDtTime");
            break;
        case waitingButtonStart:
            Serial.print("waitingButtonStart");
            break;
        case waitingButtonRelease:
            Serial.print("waitingButtonRelease");
            break;
        case motorRun:
            Serial.print("motorRun");
            break;
        case freeFlight:
            Serial.print("freeFlight");
            break;
        case triggerDT:
            Serial.print("triggerDT");
            break;
        case waitingForRestart:
            Serial.print("waitingForRestart");
            break;
    }
    int servoPosition = dtServo.read();
    int escPosition = escServo.read();
    Serial.print(", servoPosition: ");
    Serial.print(servoPosition);
    Serial.print(", escPosition: ");
    Serial.print(escPosition);
    Serial.print(", dethermalSeconds: ");
    Serial.print(dethermalSeconds[dethermalSecondsIndex]);
    Serial.print(", motorSeconds: ");
    Serial.print(motorSeconds[motorSecondsIndex]);
    Serial.print(", lastStateChangeMs: ");
    Serial.println(millis() - lastStateChangeMs);
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
    telemetryString += "lastStateChangeMs: ";
    telemetryString += (millis() - lastStateChangeMs);
    telemetryString += "<br/>";
    return telemetryString;
}

void getTelemetry() {
    Serial.print("getTelemetry");
    webServer.send(200, "text/html", getTelemetryString());
}

void defaultPage() {
    Serial.print("defaultPage");
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
            "<br/>"
            //            "<button id='buttonUpdate' onclick='location.reload();'>update</button>"
            "<div id='telemetryResult'>"
            + getTelemetryString() +
            "</div>"
            "<a href='motorDecrease'>motorDecrease</a>&nbsp;"
            "<a href='motorIncrease'>motorIncrease</a><br/><br/>"
            "<a href='dtDecrease'>dtDecrease</a>&nbsp;"
            "<a href='dtIncrease'>dtIncrease</a><br/><br/>"
            "<a href='saveChanges'>saveChanges</a><br/><br/>"
            "</body></html>");
}

void motorDecrease() {
    motorSecondsIndex--;
    motorSecondsIndex = (motorSecondsIndex < 0) ? 0 : motorSecondsIndex;
    defaultPage();
}

void motorIncrease() {
    motorSecondsIndex++;
    motorSecondsIndex = (motorSecondsIndex < motorSecondsSize) ? motorSecondsIndex : motorSecondsSize - 1;
    defaultPage();
}

void dtDecrease() {
    dethermalSecondsIndex--;
    dethermalSecondsIndex = (dethermalSecondsIndex < 0) ? 0 : dethermalSecondsIndex;
    defaultPage();
}

void dtIncrease() {
    dethermalSecondsIndex++;
    dethermalSecondsIndex = (dethermalSecondsIndex < dethermalSecondsSize) ? dethermalSecondsIndex : dethermalSecondsSize - 1;
    defaultPage();
}

void saveChanges() {
    saveSettings();
    machineState = startWipe1;
    defaultPage();
}

void getTriggerDT() {
    machineState = triggerDT;
    defaultPage();
}

void getWaitingButtonStart() {
    machineState = waitingButtonStart;
    defaultPage();
}

void getMotorRun() {
    machineState = motorRun;
    defaultPage();
}

void spinUpMotor(int targetSpeed) {
    int escValue = escServo.read();
    if (targetSpeed > escValue) {
        int wipeValue = escValue + (int) ((targetSpeed - escValue)*((millis() - lastStateChangeMs) / (float) escSpinUpMs));
        wipeValue = (wipeValue < targetSpeed) ? wipeValue : targetSpeed;
        wipeValue = (wipeValue > escValue) ? wipeValue : escValue;
        escServo.write(wipeValue);
    } else {
        escServo.write(targetSpeed);
    }
}

void spinDownMotor() {
    int remainingTime = (motorSeconds[motorSecondsIndex] * 1000) - (millis() - lastStateChangeMs);
    int spinDownValue = MinPwm + (MaxPwm - MinPwm)*(remainingTime / escSpinDownMs);
    int escPosition = (remainingTime > 0) ? spinDownValue : MinPwm;
    //    escPosition = (escPosition > MinPwm) ? escPosition - 1 : MinPwm;
    Serial.print(escPosition);
    Serial.print(",");
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
    Serial.println("servo attach");
}

void powerUpEsc() {
    escServo.attach(EscPWM, 1000, 2000); // enable the ESC output
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

void loop() {
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
                if (RcDtIsActive) { // respond to an RC DT trigger
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
            if (RcDtIsActive) { // respond to an RC DT trigger
                machineState = triggerDT;
                lastStateChangeMs = millis();
                sendTelemetry();
            } else if (millis() - lastStateChangeMs > dethermalSeconds[dethermalSecondsIndex]*1000) {
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
    }
    dnsServer.processNextRequest();
    webServer.handleClient();
}

void setupRegisters() {
    pinMode(IndicatorLed, OUTPUT); // set the LED to output
    pinMode(ButtonPin, INPUT); // set the button to input
    pinMode(ButtonPin, INPUT_PULLUP); // activate the internal pull up resistor
    attachInterrupt(ButtonPin, pinChangeInterrupt, CHANGE);
    //    powerUp();
    dtServo.write(MinPwm); // set the servo to the minimum for now
    escServo.write(MinPwm); // set the ESC to the minimum for now
}

void setup() {
    Serial.begin(115200);
    delay(10);
    Serial.print("E36");
    EEPROM.begin(4);
    loadSavedSettings();
    setupRegisters();
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
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    WiFi.softAP("E36 Timer");

    // if DNSServer is started with "*" for domain name, it will reply with
    // provided IP to all DNS request
    dnsServer.start(DNS_PORT, "*", apIP);

    webServer.on("/telemetry", getTelemetry);
    webServer.on("/triggerDT", getTriggerDT);
    webServer.on("/motorRun", getMotorRun);
    webServer.on("/restart", getWaitingButtonStart);
    webServer.on("/motorDecrease", motorDecrease);
    webServer.on("/motorIncrease", motorIncrease);
    webServer.on("/dtDecrease", dtDecrease);
    webServer.on("/dtIncrease", dtIncrease);
    webServer.on("/saveChanges", saveChanges);
    webServer.onNotFound(defaultPage);
    webServer.begin();
}
