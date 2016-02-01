/*
 * Copyright (C) 2015 Peter Withers
 */

/**
 * @since Feb 01, 2016 19:26:06 PM (creation date)
 * @author Peter Withers <peter@gthb-bambooradical.com>
 */


int indicatorLed = PB1;

void setup() {
  	pinMode(led, OUTPUT);
  	// todo: set up the PWM timer with a frequency to suit the servo and ESC, probably a 20ms period and pulse width of 1 to 2 ms.
    // PWM Frequency: 50 kHz 
    // Clock Selection: PCK/8
    // CS1[3:0]: 0100
    // OCR1C: 159
    // RESOLUTION: 7.3
    
  	// todo: keep in mind that timer0 is used for functions like delay() so timer1 is a better choice.
}
