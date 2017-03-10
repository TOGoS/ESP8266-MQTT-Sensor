port ?= /dev/ttyUSB0
board ?= esp8266:esp8266:nodemcuv2:CpuFrequency=80,FlashSize=4M3M,UploadSpeed=115200

.PHONY: \
	upload \
	watch-serial

upload:
	@echo "Hahaha, good luck getting this to work."
	@echo "To program your board you may just have to open the Arduino IDE."
	@echo
	arduino --verbose --upload --pref programmer=arduino:avrispmkii --board "${board}" --port "${port}" ESP8266-MQTT-Sensor.ino

# Alternatively, try minicom, putty, screen, etc.
# There's lots of programs to turn the serial port into a terminal.
# You may be able to pipe things right to the serial port, too...
watch-serial:
	cu -l "$port" -s 115200
