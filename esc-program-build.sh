# Copyright (C) 2016 Peter Withers
# esc-program-build.sh
# Created: 18/02/2016 20:20:12
# Author : Peter Withers <peter@gthb-bambooradical.com>

rm esc-program.o esc-program.elf
avr-gcc -Wall -Os -DF_CPU=8000000 -mmcu=attiny85 -c esc-program.c -o esc-program.o
avr-gcc -Wall -Os -DF_CPU=8000000 -mmcu=attiny85 -o esc-program.elf esc-program.o
avr-objcopy -j .text -j .data -O ihex esc-program.elf esc-program.hex
avr-size --format=avr --mcu=attiny85 esc-program.elf
micronucleus --run esc-program.hex
