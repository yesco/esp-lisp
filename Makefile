# Simple makefile for eps-lisp
PROGRAM=esp-lisp

# spiffs configuration   
EXTRA_COMPONENTS+=extras/spiffs
EXTRA_COMPONENTS+=extras/dht

FLASH_SIZE=32
SPIFFS_SINGLETON=1

SPIFFS_BASE_ADDR=0x200000

# save upload time by smaller value
#SPIFFS_SIZE=0x100000 # 1MB
SPIFFS_SIZE=0x10000 # 128K

include ../esp-open-rtos/common.mk

# change wifi host and password in: ~esp-open-rtos/include/ssid_config.h
# change dht11/dht22 in: ~esp-open-rtos/extras/dht/dht.h

# remove if trouble...
ESPBAUD=460800

$(eval $(call make_spiffs_image,SPIFFS))
