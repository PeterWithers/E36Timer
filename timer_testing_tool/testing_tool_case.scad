/*
 * Copyright (C) 2016 Peter Withers
 */

/*
 * testing_tool_case.scad
 *
 * Created: 21/03/2016 23:18
 * Author : Peter Withers <peter@gthb-bambooradical.com>
 */

lcdHoleSpacing = 23;
lcdBoardWidth = 28; // the board is square
lcdDisplayHeight = 18; // the lcd is as wide as the board

atmegaBoardWidth = 18;
atmegaBoardLength = 43;

gapBetweenBoardEdgeAndLcd = 5;

usbWidth = 8;
usbHeight = 4;

cube([atmegaBoardLength, atmegaBoardWidth, 3], center = true);
translate([-5,18,5]) cube([lcdBoardWidth, 3, lcdBoardWidth], center = true);
translate([-5,20,4]) cube([lcdBoardWidth, 5, lcdDisplayHeight], center = true);
translate([25,0,-3]) cube([usbWidth, usbWidth, usbHeight], center = true);
translate([-25,0,-3]) rotate(90, [0,1,0]) cylinder(r = 3, h = 10, center = true);