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

U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_NONE | U8G_I2C_OPT_DEV_0); // I2C / TWI

void setup() {
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
    String secondsString = String(millis() / 100 % 60, DEC);
    String minutesString = String(millis() / 100 / 60 % 60, DEC);
    String hoursString = String(millis() / 100 / 60 / 60, DEC);
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
