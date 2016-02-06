/*
 * Copyright (C) 2015 Peter Withers
 */

//#include "EEPROM.h"

/**
 * @since Feb 01, 2016 19:26:06 PM (creation date)
 * @author Peter Withers <peter@gthb-bambooradical.com>
 */


int indicatorLed = PB1;
int servoPWM = PB0;
int escPWM = PB4;
int buttonPin = PB2;

bool isOn = false;

int maxOCR0A = 125;
int minOCR0A = 60;

void setup() {
    pinMode(indicatorLed, OUTPUT);
    pinMode(servoPWM, OUTPUT);
    pinMode(escPWM, OUTPUT);
    pinMode(buttonPin, INPUT);
	digitalWrite(buttonPin, HIGH); // activate the internal pull up resistor

    // set up the PWM timer with a frequency to suit the servo and ESC, probably a 20ms period and pulse width of 1 to 2 ms.
    // prefered PWM Frequency: 50 kHz 
    // timer0 is used for functions like delay() so we dont change its frequency.
    // set Timer/Counter Control Register A
    // with settins to clear OC0A/OC0B on Compare Match, set OC0A/OC0B at BOTTOM (non-inverting mode)
    TCCR0A = _BV(COM0A1) | _BV(WGM00);
    OCR0A = minOCR0A; // set the servo to the minimum for now

    // Set up timer 1 in PWM mode for the ESC
    // PWM Frequency: 50 kHz 
    // Clock Selection: PCK/8
    // CS1[3:0]: 0100
    // OCR1C: 159
    // RESOLUTION: 7.3

    // Timer/Counter1 Control Register
    // Clock Select Bits 3, 2, 1, and 0
    TCCR1 = _BV(CS12);
    // enable PWM mode based on comparator OCR1B in Timer/Counter1
    // General Timer/Counter1 Control Register
    // Pulse Width Modulator B Enable
    // COM1B[1:0]: Comparator B Output Mode, Bits 1 and 0
    GTCCR = _BV(COM1B1) | _BV(PWM1B);
    OCR1C = 255; // Frequency
    OCR1B = 80; // Duty Cycle

    loadSavedSettings();
    setupRegisters();
    loadSavedSettings();
    showMotorTime();
    showDtTime();
    startFlightMode();
}

void loop() {
    // keep the servo moving for now
    delay(100);
    OCR0A = (OCR0A < maxOCR0A) ? OCR0A + 1 : minOCR0A;
}

void loadSavedSettings() {
    //EEPROM.write(a,b);
    //z = EEPROM.read(a);
}

void setupRegisters() {

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
    delay(1000);
}

void showDtTime() {
    OCR0A = 80;
    delay(1000);
}

void startFlightMode() {
    OCR0A = 60;
}

int getInput() {
    int hitCount = 0;
    for (int counter = 10; counter > 0; counter--) {
        if (digitalRead(buttonPin) == HIGH) {
            hitCount++;
            digitalWrite(indicatorLed, HIGH);
        } else {
            digitalWrite(indicatorLed, LOW);
        }
        delay(500);
    }
    digitalWrite(indicatorLed, LOW);
    if (hitCount > 5) {
        return -1;
    } else if (hitCount > 2) {
        return 1;
    } else {
        return 0;
    }
}

ISR(TIMER1_COMPA_vect) {
    digitalWrite(escPWM, LOW);
}

ISR(TIMER1_OVF_vect) {
    digitalWrite(escPWM, HIGH);
}
