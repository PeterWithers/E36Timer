# Copyright (C) 2016 Peter Withers
# build.sh
# Created: 18/02/2016 20:20:12
# Author : Peter Withers <peter@gthb-bambooradical.com>

rm main.o main.elf
avr-gcc -Wall -Os -DF_CPU=8000000 -mmcu=attiny85 -c E36Timer/E36Timer/main.c -o main.o
avr-gcc -Wall -Os -DF_CPU=8000000 -mmcu=attiny85 -o main.elf main.o
avr-objcopy -j .text -j .data -O ihex main.elf main.hex
avr-size --format=avr --mcu=attiny85 main.elf
micronucleus --run main.hex
