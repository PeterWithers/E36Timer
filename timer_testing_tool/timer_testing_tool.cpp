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

#define PwmInput1 PB5
#define PwmInput2 PB3

int changeCounter = 0;

ISR(INT0_vect) {
    changeCounter++;
}

void setup() {
    // set PwmInput1 and PwmInput2 to inputs
    DDRD &= ~(1 << PwmInput1);
    DDRD &= ~(1 << PwmInput2);
//    PORTD |= (1 << PwmInput1); // turn On the Pull-up    

    // start the timer1
    TCCR1B |= (1 << CS12); 

    // enable pin interrupts
    PCMSK0 |= (1 << PCINT3);
    PCMSK0 |= (1 << PCINT5);

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
    String milliString = String(millis(), DEC);
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
    u8g.print(milliString);
    u8g.setPrintPos(0, 40);
    u8g.print(hoursString + ":" + minutesString + ":" + secondsString);
}
