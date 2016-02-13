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

#define IndicatorLed PB1
#define ServoPWM	 PB0
#define EscPWM		 PB4
#define ButtonPin	 PB2

#define MaxOCR0A 125
#define MinOCR0A 60

#define ButtonIsDown 0 // todo: test the button state // digitalRead(buttonPin) == HIGH

#define TurnOnLed ; // todo digitalWrite(indicatorLed, HIGH);
#define TurnOffLed ; // todo digitalWrite(indicatorLed, LOW);

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
		//delay(500);
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
	loadSavedSettings();
	setupRegisters();
	loadSavedSettings();
	showMotorTime();
	showDtTime();
	startFlightMode();

	while (1)
	{
	}
}

