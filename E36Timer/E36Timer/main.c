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

#define IndicatorLed PB1
#define ServoPWM	 PB0
#define EscPWM		 PB4
#define ButtonPin	 PB2

#define MaxOCR0A 125
#define MinOCR0A 60

#define ButtonIsDown 0 // todo: test the button state // digitalRead(buttonPin) == HIGH

#define TurnOnLed ; // todo digitalWrite(indicatorLed, HIGH);
#define TurnOffLed ; // todo digitalWrite(indicatorLed, LOW);


enum MachineState {
	setup,
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

volatile enum MachineState machineState = setup;

/*volatile int buttonDownCount = 0;

ISR(PCINT0_vect)
{
buttonDownCount = buttonDownCount++;             // Increment volatile variable
}*/

ISR(TIMER1_COMPA_vect) {
	PORTB &= ~(1 << EscPWM);
}

ISR(TIMER1_OVF_vect) {
	//showMotorTime();
	//showDtTime();
	//startFlightMode();
	
	PORTB ~= (1 << EscPWM);
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
	
	// set up the PWM timer with a frequency to suit the servo and ESC, probably a 20ms period and pulse width of 1 to 2 ms.
	// preferred PWM Frequency: 50 kHz
	// timer0 is used for functions like delay() so care must be taken.
	// set Timer/Counter Control Register A
	// with settings to clear OC0A/OC0B on Compare Match, set OC0A/OC0B at BOTTOM (non-inverting mode)
	TCCR0A = 1 << COM0A1 | 1 << WGM00;
	OCR0A = MinOCR0A; // set the servo to the minimum for now
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

int main(void)
{
	cli();
	loadSavedSettings();
	setupRegisters();
	sei();
	while (1)
	{
	}
}

