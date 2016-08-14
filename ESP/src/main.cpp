/*
 * Copyright (C) 2016 Peter Withers
 */

/*
 * E36Timer.c
 *
 * Created: 13/02/2016 18:00:32
 * Author : Peter Withers <peter@gthb-bambooradical.com>
 */

#include <eeprom.h>
#include <Servo.h>

#define IndicatorLed     1
#define ServoPWM         0
#define EscPWM           4
#define ButtonPin        2
#define RcDt1Pin         3
#define RcDt2Pin         5

Servo dtServo;
Servo escServo;

#define MaxPwm 2000
#define MinPwm 1000
//#define MaxOCR0A 140
//#define MinPwm 50

#define ButtonIsDown (digitalRead(ButtonPin) == 0)
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

volatile int buttonCountSinceLastChange = 0;
volatile int buttonDebounceValue = 3;

volatile const int editingTimeoutSeconds = 3;
volatile int editingTimeoutCount;
volatile const int powerDownServoSeconds = 30; // 30 seconds before the servo is powered down
volatile const int powerDownEscSeconds = 300; // 5 minutes before the ESC is powered down
volatile const int cyclesPerSecond = 49;

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

void slowFlash(int pwmCycleCount) {
    if (pwmCycleCount / cyclesPerSecond % 2 == 0) {
        TurnOnLed;
    } else {
        TurnOffLed;
    }
}

void fastFlash(int pwmCycleCount) {
    if ((pwmCycleCount / (cyclesPerSecond / 5)) % 2 == 0) {
        TurnOnLed;
    } else {
        TurnOffLed;
    }
}

void doubleFlash(int pwmCycleCount) {
    int pulseIndex = (pwmCycleCount / (cyclesPerSecond / 5)) % 10;
    if (pulseIndex == 0 || pulseIndex == 3) {
        TurnOnLed;
    } else {
        TurnOffLed;
    }
}

void trippleFlash(int pwmCycleCount) {
    int pulseIndex = (pwmCycleCount / (cyclesPerSecond / 5)) % 10;
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
    buttonCountSinceLastChange = 0;
}

void updateStartWipe(enum MachineState completionState) {
    int servoPosition = dtServo.read();
    servoPosition = (servoPosition + 2 < MaxPwm) ? servoPosition + 2 : MaxPwm;
    dtServo.write(servoPosition);
    if (servoPosition >= MaxPwm) {
        machineState = completionState;
    }
}

void updateEndWipe(enum MachineState completionState) {
    int servoPosition = dtServo.read();
    servoPosition = (servoPosition - 2 > MinPwm) ? servoPosition - 2 : MinPwm;
    dtServo.write(servoPosition);
    if (servoPosition <= MinPwm) {
        machineState = completionState;
        editingTimeoutCount = editingTimeoutSeconds * cyclesPerSecond;
    }
}

void displayMotorTime(int pwmCycleCount) {
    // show the steps / divisions before resting at the selected value
    int servoPosition;
    if ((pwmCycleCount / 20) <= motorSecondsIndex) {
        int stepValue = MinPwm + ((MaxPwm - MinPwm) * (pwmCycleCount / 20) / (motorSecondsSize - 1));
        editingTimeoutCount = pwmCycleCount + (editingTimeoutSeconds * cyclesPerSecond);
        servoPosition = stepValue;
    } else {
        int displayValue = MinPwm + ((MaxPwm - MinPwm) * motorSecondsIndex / (motorSecondsSize - 1));
        servoPosition = displayValue;
    }
    dtServo.write(servoPosition);
}

void displayDethermalTime(int pwmCycleCount) {
    // show the steps / divisions before resting at the selected value
    int servoPosition;
    if ((pwmCycleCount / 20) <= dethermalSecondsIndex) {
        int stepValue = MinPwm + ((MaxPwm - MinPwm) * (pwmCycleCount / 20) / (dethermalSecondsSize - 1));
        editingTimeoutCount = pwmCycleCount + (editingTimeoutSeconds * cyclesPerSecond);
        servoPosition = stepValue;
    } else {
        int displayValue = MinPwm + ((MaxPwm - MinPwm) * dethermalSecondsIndex / (dethermalSecondsSize - 1));
        servoPosition = displayValue;
    }
    dtServo.write(servoPosition);
}

void powerUp() {
    dtServo.attach(ServoPWM); // enable the servo output
    escServo.attach(EscPWM); // enable the ESC output
}

void checkPowerDown(int pwmCycleCount) {
    if (pwmCycleCount > powerDownServoSeconds * cyclesPerSecond) {
        // power down the servo after the given delay
        dtServo.detach(); // disable the servo output
    }
    if (pwmCycleCount > powerDownEscSeconds * cyclesPerSecond) {
        // power down the ESC after the given delay
        escServo.detach(); // disable the ESC output
    }
}

void loop() {
    buttonCountSinceLastChange++;
    int servoPosition = dtServo.read();
    int escPosition = escServo.read();
    int pwmCycleCount = millis() / 20;
    switch (machineState) {
        case setupSystem:
            break;
        case throttleMax:
            servoPosition = (servoPosition + 2 < MaxPwm) ? servoPosition + 2 : MaxPwm;
            dtServo.write(servoPosition);
            if (servoPosition >= MaxPwm) {
                escServo.attach(EscPWM); // enable the ESC output
                escServo.write(servoPosition);
                machineState = waitingButtonRelease1;
            }
            break;
        case waitingButtonRelease1:
            if (buttonCountSinceLastChange > buttonDebounceValue) {
                if (ButtonIsDown) {
                    if (buttonHasBeenUp == 1) {
                        machineState = throttleMin;
                        buttonHasBeenUp = 0;
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonCountSinceLastChange = 0;
            }
            break;
        case throttleMin:
            servoPosition = (servoPosition - 2 > MinPwm) ? servoPosition - 2 : MinPwm;
            dtServo.write(servoPosition);
            escServo.write(servoPosition);
            if (servoPosition <= MinPwm) {
                machineState = waitingButtonRelease2;
            }
            break;
        case waitingButtonRelease2:
            if (buttonCountSinceLastChange > buttonDebounceValue) {
                if (ButtonIsDown) {
                    if (buttonHasBeenUp == 1) {
                        machineState = throttleMax;
                        buttonHasBeenUp = 0;
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonCountSinceLastChange = 0;
            }
            break;
        case startWipe1:
            updateStartWipe(endWipe1);
            break;
        case endWipe1:
            updateEndWipe(editMotorTime);
            break;
        case editMotorTime:
            displayMotorTime(pwmCycleCount);
            if (buttonCountSinceLastChange > buttonDebounceValue) {
                if (ButtonIsDown) {
                    if (buttonHasBeenUp == 1) {
                        // adjust motorSeconds
                        motorSecondsIndex = (motorSecondsIndex < motorSecondsSize - 1) ? motorSecondsIndex + 1 : 0;
                        editingTimeoutCount = pwmCycleCount + (editingTimeoutSeconds * cyclesPerSecond);
                        buttonHasBeenUp = 0;
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonCountSinceLastChange = 0;
            }
            if (pwmCycleCount > editingTimeoutCount) {
                machineState = startWipe2;
                pwmCycleCount = 0;
            }
            doubleFlash(pwmCycleCount);
            break;
        case startWipe2:
            updateStartWipe(endWipe2);
            break;
        case endWipe2:
            updateEndWipe(editDtTime);
            break;
        case editDtTime:
            displayDethermalTime(pwmCycleCount);
            if (buttonCountSinceLastChange > buttonDebounceValue) {
                if (ButtonIsDown) {
                    if (buttonHasBeenUp == 1) {
                        // adjust dethermalSeconds
                        dethermalSecondsIndex = (dethermalSecondsIndex < dethermalSecondsSize - 1) ? dethermalSecondsIndex + 1 : 0;
                        editingTimeoutCount = pwmCycleCount + (editingTimeoutSeconds * cyclesPerSecond);
                        buttonHasBeenUp = 0;
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonCountSinceLastChange = 0;
            }
            if (pwmCycleCount > editingTimeoutCount) {
                // we leave the ESC powered down until this point because some ESCs have timing issues that the bootloader delay seems to affect
                escServo.attach(EscPWM); // enable the ESC output
                machineState = waitingButtonStart;
                pwmCycleCount = 0;
                saveSettings();
            }
            trippleFlash(pwmCycleCount);
            break;
        case waitingButtonStart:
            checkPowerDown(pwmCycleCount);
            servoPosition = (servoPosition - 2 > MinPwm) ? servoPosition - 2 : MinPwm;
            dtServo.write(servoPosition);
            if (servoPosition <= MinPwm) {
                if (buttonCountSinceLastChange > buttonDebounceValue) {
                    if (ButtonIsDown) {
                        if (buttonHasBeenUp == 1) {
                            machineState = waitingButtonRelease;
                            buttonHasBeenUp = 0;
                            powerUp();
                        }
                    } else {
                        buttonHasBeenUp = 1;
                    }
                    buttonCountSinceLastChange = 0;
                }
                slowFlash(pwmCycleCount);
            }
            break;
        case waitingButtonRelease:
            escPosition = (escPosition < waitingEscValue) ? escPosition + 1 : waitingEscValue; // spin up the motor to the waiting speed
            escServo.write(escPosition);
            if (buttonCountSinceLastChange > buttonDebounceValue) {
                if (!ButtonIsDown) {
                    if (buttonHasBeenUp == 0) {
                        machineState = motorRun;
                        pwmCycleCount = 0;
                        buttonHasBeenUp = 1;
                    }
                } else {
                    buttonHasBeenUp = 0;
                }
                buttonCountSinceLastChange = 0;
            }
            fastFlash(pwmCycleCount);
            break;
        case motorRun:
            if ((pwmCycleCount + (/*powerDownCycles*/ MaxPwm - MinPwm)) / cyclesPerSecond > motorSeconds[motorSecondsIndex]) {
                // power down and switch state
                escPosition = (escPosition > MinPwm) ? escPosition - 1 : MinPwm;
                escServo.write(escPosition);
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
                    escPosition = (escPosition < MaxPwm) ? escPosition + 1 : MaxPwm;
                    escServo.write(escPosition);
                }
                if (RcDtIsActive) { // respond to an RC DT trigger
                    machineState = triggerDT;
                    pwmCycleCount = 0;
                } else if (buttonCountSinceLastChange > buttonDebounceValue) {
                    // allow restarts starts
                    if (ButtonIsDown) {
                        if (buttonHasBeenUp == 1) {
                            machineState = waitingButtonStart;
                            pwmCycleCount = 0;
                            buttonHasBeenUp = 0;
                            // power down the motor in the case of restarts
                            escPosition = MinPwm;
                            escServo.write(escPosition);
                        }
                    } else {
                        buttonHasBeenUp = 1;
                    }
                    buttonCountSinceLastChange = 0;
                }
            }
            break;
        case freeFlight:
            if (RcDtIsActive) { // respond to an RC DT trigger
                machineState = triggerDT;
                pwmCycleCount = 0;
            } else if (pwmCycleCount / cyclesPerSecond > dethermalSeconds[dethermalSecondsIndex]) {
                machineState = triggerDT;
                pwmCycleCount = 0;
            }
            break;
        case triggerDT:
            escPosition = MinPwm; // power down the motor in the case of RC DT
            escServo.write(escPosition);
            servoPosition = (servoPosition + 2 < MaxPwm) ? servoPosition + 2 : MaxPwm;
            dtServo.write(servoPosition);
            if (servoPosition >= MaxPwm) {
                machineState = waitingForRestart;
            }
            break;
        case waitingForRestart:
            checkPowerDown(pwmCycleCount);
            if (buttonCountSinceLastChange > buttonDebounceValue) {
                if (ButtonIsDown) {
                    if (buttonHasBeenUp == 1) {
                        machineState = waitingButtonStart;
                        buttonHasBeenUp = 0;
                        pwmCycleCount = 0;
                        powerUp();
                    }
                } else {
                    buttonHasBeenUp = 1;
                }
                buttonCountSinceLastChange = 0;
            }
            fastFlash(pwmCycleCount);
            break;
    }
}

void setupRegisters() {
    pinMode(IndicatorLed, IndicatorLed); // set the LED to output
    pinMode(ButtonPin, INPUT); // set the button to input
    pinMode(ButtonPin, INPUT_PULLUP); // activate the internal pull up resistor
    attachInterrupt(ButtonPin, pinChangeInterrupt, CHANGE);
    dtServo.write(MinPwm); // set the servo to the minimum for now
    escServo.write(MinPwm); // set the ESC to the minimum for now
}

void setup() {
    cli();
    loadSavedSettings();
    setupRegisters();
    //sei();
    attachInterrupt(1, pinChangeInterrupt, CHANGE);
    if (ButtonIsDown) {
        machineState = throttleMax;
    } else {
        machineState = startWipe1;
    }
}
