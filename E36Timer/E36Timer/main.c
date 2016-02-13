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


int main(void)
{
	DDRB |= 1 << IndicatorLed; // set the LED to output
	DDRB |= 1 << ServoPWM; // set the servo to output
	DDRB |= 1 << EscPWM; // set the ESC to output
	DDRB &= ~(1 << ButtonPin); // set the button to input
	PORTB |= 1 << ButtonPin; // activate the internal pull up resistor

    while (1) 
    {
    }
}

