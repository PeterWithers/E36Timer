/*
 * Copyright (C) 2016 Peter Withers
 */

/*
 * timer_testing_tool.cpp
 *
 * Created: 21/03/2016 21:03:12
 * Author : Peter Withers <peter@gthb-bambooradical.com>
 */

#include <U8glib.h>
#include <avr/interrupt.h>

U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_NONE | U8G_I2C_OPT_DEV_0); // I2C / TWI

volatile int pulseWidth = 0;
volatile int cycleLength = 0;
volatile int microsLastPulse = 0;
volatile int lastPINB = 0;

ISR(PCINT0_vect) {
    if ((PINB & (1 << PINB3)) == 0) {
        pulseWidth = micros() - microsLastPulse;
    } else {
        cycleLength = micros() - microsLastPulse;
        microsLastPulse = micros();
    }
}

void setup() {
    // set PwmInput1 and PwmInput2 to inputs
    DDRB &= ~(1 << DDB3);
    //DDRB &= ~(1 << DDB2);
    //DDRB &= ~(1 << DDB1);
    DDRB &= ~(1 << DDB4);
    //DDRB &= ~(1 << DDB5);
    //DDRB &= ~(1 << DDB6);
    //DDRB &= ~(1 << DDB7);
    //DDRB &= ~(1 << DDB0);

    // turn On the Pull-up
    PORTB |= (1 << PORTB3);
    //PORTB |= (1 << PORTB2);
    //PORTB |= (1 << PORTB1);
    //PORTB |= (1 << PORTB0);
    PORTB |= (1 << PORTB4);
    //PORTB |= (1 << PORTB5);
    //PORTB |= (1 << PORTB6);
    //PORTB |= (1 << PORTB7);

    // start the timer1
    //TCCR1B |= (1 << CS12);
    //TCCR1B |= (1 << CS10);

    PCICR |= (1 << PCIE0);
    // enable pin interrupts
    PCMSK0 |= (1 << PCINT3);
//    PCMSK0 |= (1 << PCINT4);

    // EICRA

    sei();
}

void loop() {
    u8g.firstPage();
    do {
        draw();
    } while (u8g.nextPage());
    delay(10);
}

void draw(void) {
    u8g.setFont(u8g_font_unifont);
    u8g.drawStr(0, 20, "TimerTestingTool");
    String hertzString = String(1000000.0 / cycleLength, 2);    
    //String cycleLengthString = String(cycleLength , DEC);
    String pulseWidthString = String(pulseWidth, DEC);
    String secondsString = String(millis() / 1000 % 60, DEC);
    String minutesString = String(millis() / 1000 / 60 % 60, DEC);
    String hoursString = String(millis() / 1000 / 60 / 60, DEC);
    if (secondsString.length() < 2) {
        secondsString = "0" + secondsString;
    }
    if (minutesString.length() < 2) {
        minutesString = "0" + minutesString;
    }
    u8g.setPrintPos(0, 60);
    u8g.print(hertzString + "hz " + pulseWidthString);
    u8g.setPrintPos(0, 40);
    u8g.print(hoursString + ":" + minutesString + ":" + secondsString);
}
