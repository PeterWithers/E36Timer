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
//#include <avr/eeprom.h>

#define IndicatorLed     PB1
#define ServoPWM         PB0
#define EscPWM           PB4
#define ButtonPin        PB2

//#define MaxOCR0A 125
//#define MinOCR0A 60
#define MaxOCR0A 245
#define MinOCR0A 118

#define DethermaliseHold OCR0A = MinOCR0A;
#define DethermaliseRelease OCR0A = MaxOCR0A;

#define ButtonIsDown !(PINB & (1 << ButtonPin))

#define TurnOnLed PORTB |= (1 << IndicatorLed);
#define TurnOffLed PORTB &= ~(1 << IndicatorLed);

#define DisplayMotorTime OCR0A = MinOCR0A + ((MaxOCR0A - MinOCR0A) * motorIncrementValue / (motorIncrementSize - 1));
#define DisplayDethermalTime OCR0A = MinOCR0A + ((MaxOCR0A - MinOCR0A) * dethermalIncrementValue / (dethermalIncrementSize - 1));

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
volatile int pwmCycleCount = 0;

volatile const int editingTimeoutSeconds = 10;
volatile const int cyclesPerSecond = 500;

const int motorIncrements[] = {5, 10, 15};
const int motorIncrementSize = 3;
volatile int motorIncrementValue = 2;
const int dethermalIncrements[] = {0, 5, 30, 60, 90, 120, 180, 240, 300};
const int dethermalIncrementSize = 9;
volatile int dethermalIncrementValue = 3;
volatile int timer0OverflowCounter = 0;

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

ISR(PCINT0_vect) {
    if (buttonCountSinceLastChange > 10) {
        switch (machineState) {
            case editMotorTime:
                if (ButtonIsDown) {
                    // adjust motorSeconds
                    motorIncrementValue = (motorIncrementValue < motorIncrementSize - 1) ? motorIncrementValue + 1 : 0;
                    DisplayMotorTime;
                    pwmCycleCount = 0;
                }
                break;
            case editDtTime:
                if (ButtonIsDown) {
                    // adjust dethermalSeconds
                    dethermalIncrementValue = (dethermalIncrementValue < dethermalIncrementSize - 1) ? dethermalIncrementValue + 1 : 0;
                    DisplayDethermalTime;
                    pwmCycleCount = 0;
                }
                break;
            case waitingButtonStart:
                if (ButtonIsDown) {
                    machineState = waitingButtonRelease;
                }
                break;
            case waitingButtonRelease:
                if (!ButtonIsDown) {
                    machineState = motorRun;
                } else {
                    machineState = waitingButtonRelease;
                }
                break;
            case motorRun:
                break;
            case freeFlight:
                break;
            case triggerDT:
                break;
            case waitingForRestart:
                break;
            default:
                break;
        }
    }
}

ISR(TIMER0_OVF_vect) {
    timer0OverflowCounter = (timer0OverflowCounter > 8) ? 0 : timer0OverflowCounter + 1;
    if (timer0OverflowCounter == 1) {
        PORTB |= (1 << ServoPWM);
        PORTB |= (1 << EscPWM);
    }
    pwmCycleCount++;
    buttonCountSinceLastChange++;
    int pwmCyclesPerWipeStep = 1000;
    int pwmCyclesPerEscStep = 1000;
    int pwmCyclesFreeFlight = 10000;
    switch (machineState) {
        case setupSystem:
            break;
        case startWipe:
            if (pwmCycleCount > pwmCyclesPerWipeStep) {
                OCR0A = (OCR0A < MaxOCR0A) ? OCR0A + 1 : MaxOCR0A;
                if (OCR0A >= MaxOCR0A) {
                    machineState = endWipe;
                    pwmCycleCount = 0;
                }
            }
            break;
        case endWipe:
            if (pwmCycleCount > pwmCyclesPerWipeStep) {
                OCR0A = (OCR0A > MinOCR0A) ? OCR0A - 1 : MinOCR0A;
                if (OCR0A <= MinOCR0A) {
                    machineState = editMotorTime;
                    DisplayMotorTime;
                    pwmCycleCount = 0;
                }
            }
            break;
        case editMotorTime:
            if (pwmCycleCount > editingTimeoutSeconds * cyclesPerSecond) {
                machineState = editDtTime;
                DisplayDethermalTime;
                pwmCycleCount = 0;
            }
            doubleFlash(pwmCycleCount);
            break;
        case editDtTime:
            if (pwmCycleCount > editingTimeoutSeconds * cyclesPerSecond) {
                machineState = waitingButtonStart;
                DethermaliseHold;
                pwmCycleCount = 0;
            }
            trippleFlash(pwmCycleCount);
            break;
        case waitingButtonStart:
            slowFlash(pwmCycleCount);
            break;
        case waitingButtonRelease:
            fastFlash(pwmCycleCount);
            break;
        case motorRun:
            if (pwmCycleCount > pwmCyclesPerEscStep) {
                if (OCR0B >= MaxOCR0A) {
                    TurnOffLed;
                    if (pwmCycleCount > 1000) {
                        OCR0B = MinOCR0A;
                        machineState = freeFlight;
                        pwmCycleCount = 0;
                    }
                } else {
                    TurnOnLed;
                    OCR0B = (OCR0B < MaxOCR0A) ? OCR0B + 1 : MaxOCR0A;
                }
            }
            break;
        case freeFlight:
            if (pwmCycleCount >= pwmCyclesFreeFlight) {
                machineState = triggerDT;
                pwmCycleCount = 0;
            }
            break;
        case triggerDT:
            DethermaliseRelease;
            machineState = waitingForRestart;
            break;
        case waitingForRestart:
            break;
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

void loadSavedSettings() {
    // use update not write to preserve eeprom life
//    uint8_t motorRunAddress = 1;
//    uint8_t dtTimeAddress = 2;
//    eeprom_update_byte(&motorRunAddress, 10);
//    uint8_t z = eeprom_read_byte(&motorRunAddress);
    //http://www.atmel.com/webdoc/AVRLibcReferenceManual/group__avr__eeprom.html
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
    TCCR0A = 1 << WGM00;
    OCR0A = MinOCR0A; // set the servo to the minimum for now
    OCR0B = MinOCR0A; // set the ESC to the minimum for now
    //    TCCR0B |= (1 << CS00) | (1 << CS02); // start timer0 31.5hz
    //    TCCR0B |= (1 << CS02); // start timer0 126.8hz : 10hz
    TCCR0B |= (1 << CS00) | (1 << CS01); // start timer0 126.8hz : 10hz
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
