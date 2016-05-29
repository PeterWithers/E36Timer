/*
 * Copyright (C) 2016 Peter Withers
 */

/*
 * esc-program.c
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

#define MaxOCR0A 129
#define MinOCR0A 64
//#define MaxOCR0A 140
//#define MinOCR0A 50

#define ButtonIsDown ((PINB & (1 << ButtonPin)) == 0)

#define TurnOnLed PORTB |= (1 << IndicatorLed);
#define TurnOffLed PORTB &= ~(1 << IndicatorLed);

enum MachineState {
    setupSystem,
    throttleMax,
    waitingButtonRelease1,
    throttleMin,
    waitingButtonRelease2
};

volatile enum MachineState machineState = setupSystem;

volatile int buttonCountSinceLastChange = 0;
volatile int buttonDebounceValue = 3;
volatile int pwmCycleCount = 0;

volatile const int editingTimeoutSeconds = 3;
volatile const int cyclesPerSecond = 49;

volatile int timer0OverflowCounter = 0;
volatile int buttonHasBeenUp = 0;

void loadSavedSettings() {
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
    machineState = throttleMax;
    while (1) {
    }
}
