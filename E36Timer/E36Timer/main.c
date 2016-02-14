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
#define ServoPWM	 PB0
#define EscPWM		 PB4
#define ButtonPin	 PB2

#define MaxOCR0A 125
#define MinOCR0A 60

#define ButtonIsDown 0 // todo: test the button state // digitalRead(buttonPin) == HIGH

#define TurnOnLed PORTB |= (1 << IndicatorLed);
#define TurnOffLed PORTB &= ~(1 << IndicatorLed);

enum MachineState {
    setupSystem,
    startWipe, // when the device resets we wipe the servo arm to release the DT lever so that a reset in midair does not prevent DT
    endWipe,
    editMotorTime,
    editDtTime,
    waitingStartButton,
    motorRun,
    freeFlight,
    triggerDT,
    waitingForRestart
};

volatile enum MachineState machineState = setupSystem;

volatile int buttonDownCount = 0;
volatile int pwmCycleCount = 0;

ISR(PCINT0_vect) {
    switch (machineState) {
        case editMotorTime:
            break;
        case editDtTime:
            break;
        case waitingStartButton:
            machineState = motorRun;
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
    //    buttonDownCount = buttonDownCount++; // Increment volatile variable
}

ISR(TIMER0_COMPA_vect) {
    switch (machineState) {
        case setupSystem:
            break;
        case startWipe:
            break;
        case endWipe:
            break;
        case editMotorTime:
            break;
        case editDtTime:
            break;
        case waitingStartButton:
            break;
        case motorRun:
            PORTB |= (1 << EscPWM);
            break;
        case freeFlight:
            break;
        case triggerDT:
            break;
        case waitingForRestart:
            break;
    }
}

ISR(TIMER0_COMPB_vect) {
    pwmCycleCount++;
    int pwmCyclesPerWipeStep = 100;
    int pwmCyclesPerEscStep = 100;
    int pwmCyclesFreeFlight = 1000;
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
                    machineState = waitingStartButton;
                    pwmCycleCount = 0;
                }
            }
            break;
        case editMotorTime:
            break;
        case editDtTime:
            break;
        case waitingStartButton:
            if (pwmCycleCount / 100 % 2 == 0) {
                TurnOnLed;
            } else {
                TurnOffLed;
            }
            break;
        case motorRun:
            PORTB &= ~(1 << EscPWM);
            if (pwmCycleCount > pwmCyclesPerEscStep) {
                // for now we are using the difference between OCR0A and OCR0B to produce the ESC PWM
                if (OCR0B >= OCR0A + MaxOCR0A) {
                    TurnOffLed;
                    if (pwmCycleCount > 1000) {
                        OCR0B = OCR0A + MinOCR0A;
                        machineState = freeFlight;
                        pwmCycleCount = 0;
                    }
                } else {
                    TurnOnLed;
                    OCR0B = (OCR0B < OCR0A + MinOCR0A) ? OCR0B + 1 : OCR0A + MaxOCR0A;
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
            OCR0A = MaxOCR0A;
            machineState = waitingForRestart;
            break;
        case waitingForRestart:
            break;
    }
    //showMotorTime();
    //showDtTime();
    //startFlightMode();
}

void loadSavedSettings() {
    //EEPROM.write(a,b);
    //z = EEPROM.read(a);
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
    // set up the PWM timer with a frequency to suit the servo and ESC, probably a 20ms period and pulse width of 1 to 2 ms.
    // preferred PWM Frequency: 50 kHz
    // timer0 is used for functions like delay() so care must be taken.
    // set Timer/Counter Control Register A
    // with settings to clear OC0A/OC0B on Compare Match, set OC0A/OC0B at BOTTOM (non-inverting mode)
    TCCR0A = 1 << COM0A1 | 1 << WGM00;
    OCR0A = MinOCR0A; // set the servo to the minimum for now
    OCR0B = 0;
}

int getInput() {
    int hitCount = 0;
    for (int counter = 10; counter > 0; counter--) {
        if (ButtonIsDown) {
            hitCount++;
            TurnOnLed;
        } else {
            TurnOffLed;
        }
        TCNT1 = 0;
        while (TCNT1 < 10) {

        }
    }
    TurnOffLed;
    if (hitCount > 5) {
        return -1;
    } else if (hitCount > 2) {
        return 1;
    } else {
        return 0;
    }
}

void showMotorTime() {
    int inputTimeout = 10;
    while (inputTimeout > 0) {
        int input = getInput();
        if (input != 0) {
            OCR0A += input;
        } else {
            inputTimeout--;
        }
    }
    //delay(1000);
}

void showDtTime() {
    OCR0A = 80;
    //delay(1000);
}

void startFlightMode() {
    OCR0A = 60;
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

