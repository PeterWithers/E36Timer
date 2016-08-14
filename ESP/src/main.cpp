/*
 * Copyright (C) 2016 Peter Withers
 */

/*
 * E36Timer.c
 *
 * Created: 13/02/2016 18:00:32
 * Author : Peter Withers <peter@gthb-bambooradical.com>
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>

#define IndicatorLed     PB1
#define ServoPWM         PB0
#define EscPWM           PB4
#define ButtonPin        PB2
#define RcDt1Pin         PB3 // on the attiny85 board the D- pin is pulled high by a 1k5 resistor and therefore should not trigger when no radio module is installed
#define RcDt2Pin         PB5

#define MaxOCR0A 129
#define MinOCR0A 64
//#define MaxOCR0A 140
//#define MinOCR0A 50

#define ButtonIsDown ((PINB & (1 << ButtonPin)) == 0)
#define RcDtIsActive ((PINB & (1 << RcDt1Pin)) == 0)

#define TurnOnLed PORTB |= (1 << IndicatorLed);
#define TurnOffLed PORTB &= ~(1 << IndicatorLed);

enum MachineState {
    setupSystem,
    throttleMax,
    waitingButtonRelease1,
    throttleMin,
    waitingButtonRelease2
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
volatile int pwmCycleCount = 0;

volatile const int editingTimeoutSeconds = 3;
volatile int editingTimeoutCount;
volatile const int powerDownServoSeconds = 30; // 30 seconds before the servo is powered down
volatile const int powerDownEscSeconds = 300; // 5 minutes before the ESC is powered down 
volatile const int cyclesPerSecond = 49;

const int motorSeconds[] = {2, 4, 5, 7, 10, 13, 15};
const int motorSecondsSize = 6;
volatile uint8_t motorSecondsIndex = 0;
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
volatile uint8_t dethermalSecondsIndex = 0;
volatile int timer0OverflowCounter = 0;
const int waitingEscValue = ((MaxOCR0A - MinOCR0A) / 3) + MinOCR0A;
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
    eeprom_update_byte((uint8_t*) 1, motorSecondsIndex);
    eeprom_update_byte((uint8_t*) 2, dethermalSecondsIndex);
}

void loadSavedSettings() {
    motorSecondsIndex = eeprom_read_byte((uint8_t*) 1);
    dethermalSecondsIndex = eeprom_read_byte((uint8_t*) 2);
    motorSecondsIndex = (motorSecondsIndex < motorSecondsSize) ? motorSecondsIndex : motorSecondsSize - 1;
    dethermalSecondsIndex = (dethermalSecondsIndex < dethermalSecondsSize) ? dethermalSecondsIndex : dethermalSecondsSize - 1;

    // if the osccalSavedIndicator value is not found then assume this is the first run after loading the firmware
    // after the firmware has been flashed the OSCCAL value will have been set by the boot loader so we save this to the EEPROM
    // on all other boots we set OSCCAL from the previously saved value from the EEPROM
    uint8_t osccalSavedIndicator = eeprom_read_byte((uint8_t*) 5);
    int osccalSavedIndex = 19;
    if (osccalSavedIndicator == osccalSavedIndex) {
        OSCCAL = eeprom_read_byte((uint8_t*) 6);
    } else {
        eeprom_update_byte((uint8_t*) 6, OSCCAL);
        eeprom_update_byte((uint8_t*) 5, osccalSavedIndex);
    }
}

ISR(PCINT0_vect) {
    buttonCountSinceLastChange = 0;
}

void updateStartWipe(enum MachineState completionState) {
    OCR0A = (OCR0A + 2 < MaxOCR0A) ? OCR0A + 2 : MaxOCR0A;
    if (OCR0A >= MaxOCR0A) {
        machineState = completionState;
        pwmCycleCount = 0;
    }
}

void updateEndWipe(enum MachineState completionState) {
    OCR0A = (OCR0A - 2 > MinOCR0A) ? OCR0A - 2 : MinOCR0A;
    if (OCR0A <= MinOCR0A) {
        machineState = completionState;
        pwmCycleCount = 0;
        editingTimeoutCount = editingTimeoutSeconds * cyclesPerSecond;
    }
}

void displayMotorTime(int pwmCycleCount) {
    // show the steps / divisions before resting at the selected value
    if ((pwmCycleCount / 20) <= motorSecondsIndex) {
        uint8_t stepValue = MinOCR0A + ((MaxOCR0A - MinOCR0A) * (pwmCycleCount / 20) / (motorSecondsSize - 1));
        editingTimeoutCount = pwmCycleCount + (editingTimeoutSeconds * cyclesPerSecond);
        OCR0A = stepValue;
    } else {
        uint8_t displayValue = MinOCR0A + ((MaxOCR0A - MinOCR0A) * motorSecondsIndex / (motorSecondsSize - 1));
        OCR0A = displayValue;
    }
}

void displayDethermalTime(int pwmCycleCount) {
    // show the steps / divisions before resting at the selected value
    if ((pwmCycleCount / 20) <= dethermalSecondsIndex) {
        uint8_t stepValue = MinOCR0A + ((MaxOCR0A - MinOCR0A) * (pwmCycleCount / 20) / (dethermalSecondsSize - 1));
        editingTimeoutCount = pwmCycleCount + (editingTimeoutSeconds * cyclesPerSecond);
        OCR0A = stepValue;
    } else {
        uint8_t displayValue = MinOCR0A + ((MaxOCR0A - MinOCR0A) * dethermalSecondsIndex / (dethermalSecondsSize - 1));
        OCR0A = displayValue;
    }
}

void powerUp() {
    DDRB |= 1 << ServoPWM; // enable the servo output
    DDRB |= 1 << EscPWM; // enable the ESC output
}

void checkPowerDown(int pwmCycleCount) {
    if (pwmCycleCount > powerDownServoSeconds * cyclesPerSecond) {
        // power down the servo after the given delay
        DDRB &= ~(1 << ServoPWM); // disable the servo output
    }
    if (pwmCycleCount > powerDownEscSeconds * cyclesPerSecond) {
        // power down the ESC after the given delay
        DDRB &= ~(1 << EscPWM); // disable the ESC output
    }
}

ISR(TIMER0_OVF_vect) {
    timer0OverflowCounter = (timer0OverflowCounter > 3) ? 0 : timer0OverflowCounter + 1;
    if (timer0OverflowCounter == 1) {
        PORTB |= (1 << ServoPWM);
        PORTB |= (1 << EscPWM);
    }
    if (timer0OverflowCounter == 3) {
        pwmCycleCount++;
        buttonCountSinceLastChange++;
        switch (machineState) {
            case setupSystem:
                break;
            case throttleMax:
                OCR0A = (OCR0A + 2 < MaxOCR0A) ? OCR0A + 2 : MaxOCR0A;
                OCR0B = OCR0A;
                if (OCR0A >= MaxOCR0A) {
                    DDRB |= 1 << EscPWM; // set the ESC to output
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
                OCR0A = (OCR0A - 2 > MinOCR0A) ? OCR0A - 2 : MinOCR0A;
                OCR0B = OCR0A;
                if (OCR0A <= MinOCR0A) {
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
                    DDRB |= 1 << EscPWM; // set the ESC to output
                    machineState = waitingButtonStart;
                    pwmCycleCount = 0;
                    saveSettings();
                }
                trippleFlash(pwmCycleCount);
                break;
            case waitingButtonStart:
                checkPowerDown(pwmCycleCount);
                OCR0A = (OCR0A - 2 > MinOCR0A) ? OCR0A - 2 : MinOCR0A;
                if (OCR0A <= MinOCR0A) {
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
                OCR0B = (OCR0B < waitingEscValue) ? OCR0B + 1 : waitingEscValue;
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
                if ((pwmCycleCount + (/*powerDownCycles*/ MaxOCR0A - MinOCR0A)) / cyclesPerSecond > motorSeconds[motorSecondsIndex]) {
                    // power down and switch state
                    OCR0B = (OCR0B > MinOCR0A) ? OCR0B - 1 : MinOCR0A;
                    if (OCR0B <= MinOCR0A) {
                        TurnOffLed;
                        machineState = freeFlight;
                        // do not reset the pwmCycleCount here because the DT time should overlap the motor run time
                    } else {
                        TurnOnLed;
                    }
                } else {
                    if (OCR0B >= MaxOCR0A) {
                        TurnOffLed;
                    } else {
                        TurnOnLed;
                        OCR0B = (OCR0B < MaxOCR0A) ? OCR0B + 1 : MaxOCR0A;
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
                                OCR0B = MinOCR0A;
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
                OCR0B = MinOCR0A; // power down the motor in the case of RC DT
                OCR0A = (OCR0A + 2 < MaxOCR0A) ? OCR0A + 2 : MaxOCR0A;
                if (OCR0A >= MaxOCR0A) {
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
}

ISR(TIMER0_COMPA_vect) {
    if (timer0OverflowCounter == 1) {
        PORTB &= ~(1 << ServoPWM);
    }
}

ISR(TIMER0_COMPB_vect) {
    if (timer0OverflowCounter == 1) {
        PORTB &= ~(1 << EscPWM);
    }
}

void setupRegisters() {
    DDRB |= 1 << IndicatorLed; // set the LED to output
    DDRB |= 1 << ServoPWM; // set the servo to output
    DDRB &= ~(1 << ButtonPin); // set the button to input
    PORTB |= 1 << ButtonPin; // activate the internal pull up resistor
    DDRB &= ~(1 << RcDt1Pin); // set the button to input
    DDRB &= ~(1 << RcDt2Pin); // set the button to input
    PORTB |= 1 << RcDt1Pin; // activate the internal pull up resistor
    GIMSK |= 1 << PCIE; // enable pin change interrupts
    PCMSK |= 1 << ButtonPin; // enable button interrupts 
    PCMSK |= 1 << RcDt1Pin; // enable button interrupts 
    TIMSK |= 1 << OCIE0A; // enable timer0 compare match A interrupt
    TIMSK |= 1 << OCIE0B; // enable timer0 compare match B interrupt
    TIMSK |= 1 << TOIE0; // enable timer0 overflow interrupt
    // set up the PWM timer with a frequency to suit the servo and ESC, probably a 20ms period and pulse width of 1 to 2 ms.
    // preferred PWM Frequency: 50 kHz
    // timer0 is used for functions like delay() so care must be taken.
    // set Timer/Counter Control Register A
    // with settings to clear OC0A/OC0B on Compare Match, set OC0A/OC0B at BOTTOM (non-inverting mode)
    //TCCR0A = 1 << COM0A1 | 1 << WGM00; // set PWM output to PB0
    //TCCR0A = 1 << WGM00;
    TCCR0A = 0;
    OCR0A = MinOCR0A; // set the servo to the minimum for now
    OCR0B = MinOCR0A; // set the ESC to the minimum for now
    //   TCCR0B |= (1 << CS00) | (1 << CS02); // start timer0 15.7hz
    TCCR0B |= (1 << CS02); // start timer0 126.8hz : 62.8hz
    //    TCCR0B |= (1 << CS00) | (1 << CS01); // start timer0 126.8hz : 10hz : 100hz
}

//void loop() {
//}

//void setup() {

int main(void) {
    cli();
    loadSavedSettings();
    setupRegisters();
    sei();
    if (ButtonIsDown) {
        machineState = throttleMax;
    } else {
        machineState = startWipe1;
    }
    while (1) {
    }
}
