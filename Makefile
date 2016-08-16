# Simple makefile for eps-lisp
PROGRAM=esp-lisp
#CFLAGS += -std=gnu99 # takes TOO MUCH SPACE

# spiffs configuration   
EXTRA_COMPONENTS=extras/spiffs
FLASH_SIZE=32
SPIFFS_SINGLETON=1

SPIFFS_BASE_ADDR=0x200000

#SPIFFS_SIZE=0x100000 # 1MB
#SPIFFS_SIZE=0x10000 # 128K

include ../esp-open-rtos/common.mk

$(eval $(call make_spiffs_image,SPIFFS))
