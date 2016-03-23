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

volatile int changeCounter = 0;
volatile int pulseLenthCounter = 0;
int milisLastReset = 0;
int milisOverLast100Pulses = 0;

ISR(PCINT0_vect) {
    changeCounter++;
    if (changeCounter == 100) {
        milisOverLast100Pulses = millis() - milisLastReset;
        milisLastReset = millis();
        changeCounter = 0;
    }
    if ((PINB & (1 << PINB3)) == 1) {
        TCNT1 = 0;
    } else {
        pulseLenthCounter = TCNT1;
    }
}

void setup() {
    // set PwmInput1 and PwmInput2 to inputs
    DDRB &= ~(1 << DDB3);
    DDRB &= ~(1 << DDB5);

    // turn On the Pull-up
    PORTB |= (1 << PORTB3);
    PORTB |= (1 << PORTB5);

    // start the timer1
    TCCR1B |= (1 << CS12);
    TCCR1B |= (1 << CS10);

    PCICR |= (1 << PCIE0);
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
    String changeCounterString = String(changeCounter, DEC);
    String pulseLenthCounterString = String(pulseLenthCounter, DEC);
    String milisOverLast100PulsesString = String((long)(milisOverLast100Pulses / 20.0), 3);
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
    u8g.print(changeCounterString + ":" + pulseLenthCounterString + ":" + milisOverLast100PulsesString);
    u8g.setPrintPos(0, 40);
    u8g.print(hoursString + ":" + minutesString + ":" + secondsString);
}
