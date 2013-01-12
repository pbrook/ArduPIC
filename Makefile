
ARDUINO_LIBS = debugf TimerOne
ARDUINO_PORT = /dev/ttyACM0

ifeq (0, 1)
BOARD_TAG = leonardo
else

BOARD_TAG = minimus32
BOARDS_TXT=$(ARDUINO_SKETCHBOOK)/hardware/minimus/boards.txt
ARDUINO_VAR_PATH=$(ARDUINO_SKETCHBOOK)/hardware/minimus/variants
ARDUINO_CORE_PATH=$(ARDUINO_SKETCHBOOK)/hardware/minimus/cores/minimus
endif

include /home/paul/bin/Arduino.mk
