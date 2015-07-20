OPT_DEFS += -DTIC_LENGTH_MS=250
OPT_DEFS += -DBINARY_SECONDS

ARCH = AVR8

MCU = atmega8

TARGET = clock

SRC = clockmtx004.c ht1632c.c

include rules.mk
