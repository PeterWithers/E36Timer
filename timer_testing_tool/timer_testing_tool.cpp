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

volatile int pulseWidthEsc = 0;
volatile int pulseWidthServo = 0;
volatile int cycleLength = 0;
volatile int microsLastPulse = 0;
volatile int lastPINB = 0;
volatile int milisServoStart = 0;
int milisEscStart = 0;
//int escPowerTimout = 0;
volatile int lastDethermalSeconds = 0;
volatile int lastMotorMilis = 0;

ISR(PCINT0_vect) {
    int changedBits = PINB^lastPINB;
    lastPINB = PINB;

    if ((changedBits & (1 << PINB4)) != 0) {
        if ((PINB & (1 << PINB4)) == 0) {
            pulseWidthServo = micros() - microsLastPulse;
        }
    }

    if ((changedBits & (1 << PINB3)) != 0) {
        if ((PINB & (1 << PINB3)) == 0) {
            pulseWidthEsc = micros() - microsLastPulse;
            if (pulseWidthEsc > 1400) {
                //if(escPowerTimout==0){
                //  milisEscStart = millis();
                //  escPowerTimout=10;
                //}
                lastMotorMilis = millis() - milisEscStart;
            } else if (pulseWidthEsc < 1400) {
                milisEscStart = millis();
                //escPowerTimout = (escPowerTimout>0)?escPowerTimout--:0;
            }
        } else {
            cycleLength = micros() - microsLastPulse;
            microsLastPulse = micros();
        }
    }
}

void setup() {
    // set PwmInput1 and PwmInput2 to inputs
    DDRB &= ~(1 << DDB3);
    DDRB &= ~(1 << DDB4);

    // turn On the Pull-up
    PORTB |= (1 << PORTB3);
    PORTB |= (1 << PORTB4);

    // enable pin interrupts
    PCICR |= (1 << PCIE0);
    PCMSK0 |= (1 << PCINT3);
    PCMSK0 |= (1 << PCINT4);

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
    u8g.drawFrame(0, 0, 128, 20);
    u8g.drawFrame(0, 21, 63, 64 - 21);
    u8g.drawFrame(65, 21, 63, 64 - 21);
    String hertzString = String(1000000.0 / cycleLength, 2);
    String secondsString = String(millis() / 1000 % 60, DEC);
    String minutesString = String(millis() / 1000 / 60 % 60, DEC);
    String hoursString = String(millis() / 1000 / 60 / 60, DEC);
    if (secondsString.length() < 2) {
        secondsString = "0" + secondsString;
    }
    if (minutesString.length() < 2) {
        minutesString = "0" + minutesString;
    }
    u8g.setPrintPos(70, 15);
    u8g.print(hertzString + "hz");
    u8g.setPrintPos(2, 15);
    u8g.print(hoursString + ":" + minutesString + ":" + secondsString);
    u8g.setPrintPos(2, 35);
    //u8g.print(pulseWidthServo + "\xB5s");
    u8g.print(String(pulseWidthServo / 10 - 100) + "%");
    //u8g.print(String(lastPINB,BIN));
    u8g.setPrintPos(2, 50);
    u8g.print(String(lastMotorMilis / 1000.0, 2) + "s");
    u8g.setPrintPos(66, 35);
    u8g.print(String(pulseWidthEsc / 10 - 100) + "%");
}
