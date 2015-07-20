
// DrJones 2012
//
// Clock with Tics for JY-MCU 3208 by Michael LeBlanc, NSCAD University
// http://generaleccentric.net
// March 2013
//
// Version 002 adds typography for letters
// Version 003 adds randomize
// Version 004 replaces manual brightness control with Child Safe (swearwords off) mode,
// corrects randomization functions and uses CdS cell on AD0 for auto brightness control
//
// button1: adjust time forward, keep pressed for a while for fast forward
// button2: adjust time backward, keep pressed for a while for fast backward
// button3: toggle "Child Safe" mode

#define F_CPU 1000000

#include <avr/pgmspace.h>
#include <avr/io.h>
#include <util/delay.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/eeprom.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include "ht1632c.h"

#define byte uint8_t
#define word uint16_t

unsigned int n;				/* for random number function */
int childsafe = 0;

const byte bigdigits[10][6] PROGMEM = {
  {0b01111110,0b10000001,0b10000001,0b10000001,0b10000001,0b01111110},	// 0
  {0b00000000,0b00000000,0b10000010,0b11111111,0b10000000,0b00000000},	// 1
  {0b11100010,0b10010001,0b10010001,0b10001001,0b10001001,0b10000110},	// 2
  {0b01000001,0b10000001,0b10001001,0b10001001,0b10010101,0b01100011},	// 3
  {0b00110000,0b00101000,0b00100100,0b00100010,0b11111111,0b00100000},	// 4
  {0b01001111,0b10001001,0b10001001,0b10001001,0b10001001,0b01110001},  // 5
  {0b01111110,0b10001001,0b10001001,0b10001001,0b10001001,0b01110000},	// 6
  {0b00000001,0b00000001,0b11100001,0b00010001,0b00001001,0b00000111},  // 7
  {0b01110110,0b10001001,0b10001001,0b10001001,0b10001001,0b01110110},	// 8
  {0b00001110,0b10010001,0b10010001,0b10010001,0b10010001,0b01111110},  // 9
};

const byte letters[13][6] PROGMEM = {
  {0b00010000,0b10101000,0b10101000,0b10101000,0b10101000,0b01000000},	// s
  {0b11111111,0b00001000,0b00001000,0b11110000,0b00000000,0b00000000},	// h
  {0b11111010,0b00000000,0b00000000,0b00000000,0b00000000,0b00000000},	// i
  {0b00001000,0b01111110,0b10001000,0b10000000,0b01000000,0b00000000},	// t
  {0b00001000,0b11111110,0b00001001,0b00001001,0b00000010,0b00000000},	// f
  {0b01111000,0b10000000,0b10000000,0b10000000,0b01000000,0b11111000},  // u
  {0b01110000,0b10001000,0b10001000,0b10001000,0b10001000,0b01000000},	// c
  {0b11111111,0b00010000,0b00010000,0b00101000,0b01000100,0b10000000},  // k
  {0b00000000,0b00111110,0b01000001,0b01000001,0b01000001,0b00100010},	// C
  {0b00100110,0b01001001,0b01001001,0b01001001,0b00110010,0b00000000},	// S
  {0b00000000,0b00111110,0b01000001,0b01000001,0b00111110,0b00000000},	// O
  {0b01111111,0b00000110,0b00001000,0b00110000,0b01111111,0b00000000},	// N
  {0b01111111,0b00001001,0b00000000,0b01111111,0b00001001,0b00000000},	// F
};

const byte letter_widths[13] PROGMEM = {6, 4, 1, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6};

#define key1 ((PIND&(1 << 7)) == 0)
#define key2 ((PIND&(1 << 6)) == 0)
#define key3 ((PIND&(1 << 5)) == 0)
#define keysetup() do{ DDRD &= 0xff - (1 << 7) - (1 << 6) - (1 << 5); PORTD |= (1 << 7) + (1 << 6) + (1 << 5); }while(0)  //input, pull up

int ADCsingleREAD(uint8_t adctouse)
{
    int ADCval;

    ADMUX = adctouse;                   // use #1 ADC
    ADMUX |= (1 << REFS0);              // use AVcc as the reference
    ADMUX &= ~(1 << ADLAR);             // clear for 10 bit resolution

    ADCSRA|= (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);    // 128 prescale for 8Mhz
    ADCSRA |= (1 << ADEN);              // Enable the ADC

    ADCSRA |= (1 << ADSC);              // Start the ADC conversion

    while(ADCSRA & (1 << ADSC));        // Thanks T, this line waits for the ADC to finish

    ADCval = ADCL;
    ADCval = (ADCH << 8) + ADCval;    // ADCH is read so ADC can be updated again

    return ADCval;
}

int gen_rand(void)
{
    unsigned int m;
    m = rand() % n;
    return(m);
}

//------------------------------------------------------------------------------------- CLOCK ------------------


volatile byte sec = 5;
byte sec0 = 200, minute, hour, day, month; word year;


inline void clocksetup() {  // CLOCK, interrupt every second
    ASSR |= (1 << AS2);    //timer2 async from external quartz
    TCCR2 = 0b00000101;    //normal,off,/128; 32768Hz/256/128 = 1 Hz
    TIMSK |= (1 << TOIE2); //enable timer2-overflow-int
    sei();               //enable interrupts
}

// CLOCK interrupt
ISR(TIMER2_OVF_vect) {     //timer2-overflow-int
    sec++;
}

void incsec(byte add) {
    sec += add;
    while (sec >= 60) {
        sec -= 60;
        minute++;
        while (minute >= 60) {
            minute -= 60;
            hour++;
            while (hour >= 24) {
                hour -= 24;
                day++;
            }//24hours
        }//60min
    }//60sec
}

void decsec(byte sub) {
    while (sub > 0) {
        if (sec > 0) sec--;
        else {
            sec = 59;
            if (minute > 0) minute--;
            else {
                minute = 59;
                if (hour > 0) hour--;
                else {hour = 23; day--;}
            }//hour
        }//minute
        sub--;
    }//sec
}

byte clockhandler(void) {
    if (sec == sec0)
        return 0;   //check if something changed
    sec0 = sec;
    incsec(0);  //just carry over
    return 1;
}


//-------------------------------------------------------------------------------------- clock render ----------


void renderclock(void) {
    byte col = 0;
    leds[col++] = 0;	// add a 1 column space on the left
    for (byte i = 0;i<6;i++) leds[col++] = pgm_read_byte(&bigdigits[hour / 10][i]);
    leds[col++] = 0;
    for (byte i = 0;i<6;i++) leds[col++] = pgm_read_byte(&bigdigits[hour % 10][i]);
    leds[col++] = 0;

    if (sec % 2) {
        leds[col++] = 0b00000000;
        leds[col++] = 0b00100100;
    }
    else {
        leds[col++] = 0b00100100;
        leds[col++] = 0b00000000;
    }

    leds[col++] = 0;
    for (byte i = 0; i < 6; i++)
        leds[col++] = pgm_read_byte(&bigdigits[minute / 10][i]);
    leds[col++] = 0;
    for (byte i = 0; i < 6; i++)
        leds[col++] = pgm_read_byte(&bigdigits[minute % 10][i]);
    leds[col++] = 0;	// add a 1 column space on the right
}

void renderword(unsigned int *w, unsigned int length, bool centre) {
    byte col = 0;
    byte i;

    if (centre) {
        byte total_width = 0;
        for (byte pos = 0; pos < length; pos++)
            total_width += pgm_read_byte(&letter_widths[w[pos]]);
        if (total_width < 32) {
            while (col < ((32 - total_width - length + 1) / 2))
                leds[col++] = 0;
        }
    }

    for (byte pos = 0; pos < length; pos++) {
        if (w[pos] != 0xFFFF)
            for (i = 0; i < pgm_read_byte(&letter_widths[w[pos]]); i++) {
                leds[col++] = pgm_read_byte(&letters[w[pos]][i]);
                if (col >= 32)
                    return;
            }
        leds[col++] = 0;
    }

    while (col < 32)
        leds[col++] = 0;
}

void rendershit(void) {
    unsigned int word[] = {0, 1, 2, 3};
    renderword(word, 4, true);
}

void renderfuck(void) {
    unsigned int word[] = {4, 5, 6, 7};
    renderword(word, 4, true);
}

void rendercs_on(void) {
    unsigned int word[] = {8, 9, 0xFFFF, 10, 11};
    renderword(word, 5, true);
}

void rendercs_off(void) {
    unsigned int word[] = {8, 9, 0xFFFF, 10, 12};
    renderword(word, 5, true);
}

byte changing, bright = 3;

int main(void) {  //==================================================================== main ==================

    HTpinsetup();
    HTsetup();
    keysetup();
    clocksetup();

    for (byte i = 0; i < 32; i++)
        leds[i] = 0b01010101 << (i % 2);

    HTsendscreen();

    hour = 12;
    minute = 00;

    while (1) {
        bright = 1; // (ADCsingleREAD(0) - 80) / 62;  // set the display brightness
        HTbrightness(bright);

        if (key1) {
            if (changing > 250)
                incsec(20);
            else {
                changing++;
                incsec(1);
            }
        }
        else if (key2) {
            if (changing > 250)
                decsec(20);
            else {
                changing++;
                decsec(1);
            }
        }
        else if (key3) {
            if (!changing) {
                changing = 1;
                if (childsafe == 0) {
                    rendercs_on();
                    childsafe = 1;
                }
                else {
                    rendercs_off();
                    childsafe = 0;
                }
                HTsendscreen();
                _delay_ms(2000);
            } //	wait 2 seconds before reverting to clock
        }
        else
            changing = 0;

        if (clockhandler()) {
            renderclock();
            HTsendscreen();
        }
        if (childsafe == 0) {
            int shtfck = gen_rand();
            if ((shtfck > 20) && (shtfck < 25)) {
                rendershit();
                HTsendscreen();
                _delay_ms(TIC_LENGTH_MS);
                renderclock();
                HTsendscreen();
            }
            if ((shtfck > 0) && (shtfck < 5)) {
                renderfuck();
                HTsendscreen();
                _delay_ms(TIC_LENGTH_MS);
                renderclock();
                HTsendscreen();
            }
        }
    }
    return(0);
}//main
