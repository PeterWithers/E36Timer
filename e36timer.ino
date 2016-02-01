/*
 * Copyright (C) 2015 Peter Withers
 */

/**
 * @since Feb 01, 2016 19:26:06 PM (creation date)
 * @author Peter Withers <peter@gthb-bambooradical.com>
 */


int indicatorLed = PB1;

int maxOCR0A = 125;
int minOCR0A = 60;  

void setup() {
  	pinMode(led, OUTPUT);
  	// set up the PWM timer with a frequency to suit the servo and ESC, probably a 20ms period and pulse width of 1 to 2 ms.
    // prefered PWM Frequency: 50 kHz 
  	// timer0 is used for functions like delay() so we dont change its frequency.
  	// set Timer/Counter Control Register A
  	// with settins to clear OC0A/OC0B on Compare Match, set OC0A/OC0B at BOTTOM (non-inverting mode)
  	TCCR0A = _BV(COM0A1) | _BV(WGM00);
  	OCR0A = minOCR0A; // set the servo to the minimum for now
}
