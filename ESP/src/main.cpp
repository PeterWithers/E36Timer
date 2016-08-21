/*
 * Copyright (C) 2016 Peter Withers
 */

/*
 * E36Timer.c
 *
 * Created: 13/02/2016 18:00:32
 * Author : Peter Withers <peter@gthb-bambooradical.com>
 */

#include <Arduino.h>
#include <eeprom.h>
#include <Servo.h>
// todo: this include and related defines are temporary 
#include <NewPing.h> 

#define TRIGGER_PIN  3
#define ECHO_PIN     2
#define MAX_DISTANCE 400

NewPing sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE);
// end temporary defines

#define IndicatorLed     13
#define ServoPWM         15
#define EscPWM           14
#define ButtonPin        12
#define RcDt1Pin         11
#define RcDt2Pin         10

Servo dtServo;
Servo escServo;

#define MaxPwm 180
#define MinPwm 0

#define ButtonIsDown (sonar.ping_cm() < 30)
//#define ButtonIsDown (digitalRead(ButtonPin) == 0)
#define RcDtIsActive (digitalRead(RcDt1Pin) == 0)

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
volatile const unsigned long escSpinDownMs = 3000;
volatile const unsigned long escSpinUpMs = 3000;
volatile const unsigned long servoWipeMs = 3000;
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
    // using update not write to preserve eeprom life
    EEPROM.write(1, motorSecondsIndex);
    EEPROM.write(2, dethermalSecondsIndex);
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
    Serial.print(", lastStateChangeMs: ");
    Serial.println(millis() - lastStateChangeMs);
}

void updateStartWipe(enum MachineState completionState) {
    int wipeValue = MinPwm + (int) ((MaxPwm - MinPwm)*((millis() - lastStateChangeMs) / (float) servoWipeMs));
    dtServo.write(wipeValue);
    if (wipeValue >= MaxPwm) {
        machineState = completionState;
        lastStateChangeMs = millis();
        editingStartMs = millis();
        sendTelemetry();
    }
}

void updateEndWipe(enum MachineState completionState) {
    int wipeValue = MaxPwm - (int) ((MaxPwm - MinPwm)*((millis() - lastStateChangeMs) / (float) servoWipeMs));
    dtServo.write(wipeValue);
    if (wipeValue <= MinPwm) {
        machineState = completionState;
        lastStateChangeMs = millis();
        editingStartMs = millis();
        sendTelemetry();
    }
}

void displayMotorTime() {
    // show the steps / divisions before resting at the selected value
    int indexToDisplay = (millis() - lastStateChangeMs) / 1000;
    int servoPosition;
    if (indexToDisplay <= motorSecondsIndex) {
        int stepValue = MinPwm + ((MaxPwm - MinPwm) * (indexToDisplay) / (motorSecondsSize - 1));
        lastStateChangeMs = millis();
        servoPosition = stepValue;
    } else {
        int displayValue = MinPwm + ((MaxPwm - MinPwm) * motorSecondsIndex / (motorSecondsSize - 1));
        servoPosition = displayValue;
    }
    dtServo.write(servoPosition);
}

void displayDethermalTime() {
    // show the steps / divisions before resting at the selected value
    int indexToDisplay = (millis() - lastStateChangeMs) / 1000;
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
    dtServo.attach(ServoPWM); // enable the servo output
    Serial.println("servo attach");
}

void powerUpEsc() {
    escServo.attach(EscPWM); // enable the ESC output
    Serial.println("esc attach");
}

void powerUp() {
    powerUpDt();
    powerUpEsc();
}

void checkPowerDown() {
    if (millis() > lastStateChangeMs + powerDownServoMs) {
        if (dtServo.attached()) {
            // power down the servo after the given delay
            dtServo.detach(); // disable the servo output
            Serial.println("servo detach");
        }
    }
    if (millis() > lastStateChangeMs + powerDownEscSeconds) {
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
            servoPosition = (servoPosition - 2 > MinPwm) ? servoPosition - 2 : MinPwm;
            dtServo.write(servoPosition);
            if (servoPosition <= MinPwm) {
                if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                    if (ButtonIsDown) {
                        if (buttonHasBeenUp == 1) {
                            machineState = waitingButtonRelease;
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
            escPosition = (escPosition < waitingEscValue) ? escPosition + 1 : waitingEscValue; // spin up the motor to the waiting speed
            escServo.write(escPosition);
            if (millis() - buttonLastChangeMs > buttonDebounceMs) {
                if (!ButtonIsDown) {
                    if (buttonHasBeenUp == 0) {
                        machineState = motorRun;
                        lastStateChangeMs = millis();
                        buttonHasBeenUp = 1;
                    }
                } else {
                    buttonHasBeenUp = 0;
                }
                buttonLastChangeMs = millis();
            }
            fastFlash();
            sendTelemetry();
            break;
        case motorRun:
            if (millis() - lastStateChangeMs + escSpinDownMs > motorSeconds[motorSecondsIndex] * 1000) {
                // power down and switch state
                int remainingTime = (motorSeconds[motorSecondsIndex] * 1000) - (millis() - lastStateChangeMs);
                int spinDownValue = MinPwm + (MaxPwm - MinPwm)*(remainingTime / escSpinDownMs);
                escPosition = (remainingTime > 0) ? spinDownValue : MinPwm;
                escPosition = (escPosition > MinPwm) ? escPosition - 1 : MinPwm;
                escServo.write(escPosition);
                if (escPosition <= MinPwm) {
                    TurnOffLed;
                    machineState = freeFlight;
                    // do not reset the pwmCycleCount here because the DT time should overlap the motor run time
                    sendTelemetry();
                } else {
                    TurnOnLed;
                }
                sendTelemetry();
            } else {
                if (escPosition >= MaxPwm) {
                    TurnOffLed;
                } else {
                    TurnOnLed;
                    int spinUpValue = MaxPwm - (MaxPwm - MinPwm)*(lastStateChangeMs / escSpinDownMs);
                    escPosition = (escPosition < MaxPwm) ? spinUpValue : MaxPwm;
                    escServo.write(escPosition);
                    sendTelemetry();
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
            updateEndWipe(waitingForRestart);
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
    Serial.begin(57600);
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
}
