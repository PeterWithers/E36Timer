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

//#define MaxOCR0A 125
//#define MinOCR0A 60
#define MaxOCR0A 140
#define MinOCR0A 50

#define ButtonIsDown ((PINB & (1 << ButtonPin)) == 0)

#define TurnOnLed PORTB |= (1 << IndicatorLed);
#define TurnOffLed PORTB &= ~(1 << IndicatorLed);

#define DisplayMotorTime OCR0A = MinOCR0A + ((MaxOCR0A - MinOCR0A) * motorSecondsIndex / (motorSecondsSize - 1));
#define DisplayDethermalTime OCR0A = MinOCR0A + ((MaxOCR0A - MinOCR0A) * dethermalSecondsIndex / (dethermalSecondsSize - 1));

enum MachineState {
    setupSystem,
    startWipe, // when the device resets we wipe the servo arm to release the DT lever so that a reset in midair does not prevent DT
    endWipe,
    editMotorTime,
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

volatile const int editingTimeoutSeconds = 10;
volatile const int cyclesPerSecond = 50;

const int motorSeconds[] = {2, 4, 5, 7, 10, 15};
const int motorSecondsSize = 6;
volatile uint8_t motorSecondsIndex = 0;
const int dethermalSeconds[] = {0, 5, 30, 60, 90, 120, 180, 240, 300};
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
}

ISR(PCINT0_vect) {
    buttonCountSinceLastChange = 0;
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
            case startWipe:
                OCR0A = (OCR0A + 2 < MaxOCR0A) ? OCR0A + 2 : MaxOCR0A;
                if (OCR0A >= MaxOCR0A) {
                    machineState = endWipe;
                }
                break;
            case endWipe:
                OCR0A = (OCR0A - 2 > MinOCR0A) ? OCR0A - 2 : MinOCR0A;
                if (OCR0A <= MinOCR0A) {
                    machineState = editMotorTime;
                    DisplayMotorTime;
                }
                break;
            case editMotorTime:
                if (buttonCountSinceLastChange > buttonDebounceValue) {
                    if (ButtonIsDown) {
                        if (buttonHasBeenUp == 1) {
                            // adjust motorSeconds
                            motorSecondsIndex = (motorSecondsIndex < motorSecondsSize - 1) ? motorSecondsIndex + 1 : 0;
                            DisplayMotorTime;
                            pwmCycleCount = 0;
                            buttonHasBeenUp = 0;
                        }
                    } else {
                        buttonHasBeenUp = 1;
                    }
                    buttonCountSinceLastChange = 0;
                }
                if (pwmCycleCount > editingTimeoutSeconds * cyclesPerSecond) {
                    machineState = editDtTime;
                    DisplayDethermalTime;
                    pwmCycleCount = 0;
                }
                doubleFlash(pwmCycleCount);
                break;
            case editDtTime:
                if (buttonCountSinceLastChange > buttonDebounceValue) {
                    if (ButtonIsDown) {
                        if (buttonHasBeenUp == 1) {
                            // adjust dethermalSeconds
                            dethermalSecondsIndex = (dethermalSecondsIndex < dethermalSecondsSize - 1) ? dethermalSecondsIndex + 1 : 0;
                            DisplayDethermalTime;
                            pwmCycleCount = 0;
                            buttonHasBeenUp = 0;
                        }
                    } else {
                        buttonHasBeenUp = 1;
                    }
                    buttonCountSinceLastChange = 0;
                }
                if (pwmCycleCount > editingTimeoutSeconds * cyclesPerSecond) {
                    machineState = waitingButtonStart;
                    pwmCycleCount = 0;
                    saveSettings();
                }
                trippleFlash(pwmCycleCount);
                break;
            case waitingButtonStart:
                OCR0A = (OCR0A - 2 > MinOCR0A) ? OCR0A - 2 : MinOCR0A;
                if (OCR0A <= MinOCR0A) {
                    if (buttonCountSinceLastChange > buttonDebounceValue) {
                        if (ButtonIsDown) {
                            if (buttonHasBeenUp == 1) {
                                machineState = waitingButtonRelease;
                                buttonHasBeenUp = 0;
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
                if (pwmCycleCount / cyclesPerSecond > motorSeconds[motorSecondsIndex]) {
                    // power down and switch state
                    OCR0B = (OCR0B > MinOCR0A) ? OCR0B - 1 : MinOCR0A;
                    if (OCR0B <= MinOCR0A) {
                        TurnOffLed;
                        machineState = freeFlight;
                        pwmCycleCount = 0;
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
                    // allow restarts starts
                    if (buttonCountSinceLastChange > buttonDebounceValue) {
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
                if (pwmCycleCount / cyclesPerSecond > dethermalSeconds[dethermalSecondsIndex]) {
                    machineState = triggerDT;
                    pwmCycleCount = 0;
                }
                break;
            case triggerDT:
                OCR0A = (OCR0A + 2 < MaxOCR0A) ? OCR0A + 2 : MaxOCR0A;
                if (OCR0A >= MaxOCR0A) {
                    machineState = waitingForRestart;
                }
                break;
            case waitingForRestart:
                if (buttonCountSinceLastChange > buttonDebounceValue) {
                    if (ButtonIsDown) {
                        if (buttonHasBeenUp == 1) {
                            machineState = waitingButtonStart;
                            buttonHasBeenUp = 0;
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
    DDRB |= 1 << EscPWM; // set the ESC to output
    DDRB &= ~(1 << ButtonPin); // set the button to input
    PORTB |= 1 << ButtonPin; // activate the internal pull up resistor
    GIMSK |= 1 << PCIE; // enable pin change interrupts
    PCMSK |= 1 << ButtonPin; // enable button interrupts 
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
    machineState = startWipe;
    while (1) {
    }
}
