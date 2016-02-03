# E36Timer
Simple and cheep free flight timer

This timer is based on the attiny86 usb board. These boards usually ship with the micronucleus boot loader, in which case it can be flashed with the command line tool: micronucleus --run e36timer.hex

The components required for this are: attiny86 usb board, push button and wiring for the button, servo and ESC.

When the device is powered up there will be a period of time in which the motor run time and DT time can be adjusted. The options will be: 
Motor times: 5, 10, 15 seconds
DT time: 0, 5, 30, 60, 90, 120, 180, 240, 300 seconds

It is possible that powersupply noise or interruptions could cause the attiny to brownout. For this reason the first action on power up should be to release the DT, otherwise a brownout mid flight could prevent or delay DT.
