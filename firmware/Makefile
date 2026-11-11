# CH552P firmware for the USB-PD PPS board. `make` builds build/usbpd.hex/.bin with SDCC; `make test` runs the host
# logic tests; `make flash` uploads with wchisp (hold SEL while plugging in to enter the ROM bootloader).
CC      = sdcc
# Application space ends where the ROM bootloader starts (0x3800 = 14336 bytes); 1 KB xRAM, 256 B iRAM.
CFLAGS  = -mmcs51 --model-large --opt-code-size --std-sdcc11 \
          --code-size 14336 --xram-size 1024 --iram-size 256 --xram-loc 0x0000
SRC     = src/main.c src/hw.c src/oled.c src/logic.c
OBJ     = $(patsubst src/%.c,build/%.rel,$(SRC))
HOSTCC ?= cc

all: build/usbpd.hex build/usbpd.bin

build/%.rel: src/%.c src/*.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/usbpd.ihx: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@

build/usbpd.hex: build/usbpd.ihx
	packihx $< > $@

build/usbpd.bin: build/usbpd.ihx
	makebin -p $< $@

build:
	mkdir -p build

test: | build
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Isrc -o build/test_logic test/test_logic.c src/logic.c
	./build/test_logic
	# test_oled.c includes a copy of oled.c, so its #include "hw.h" finds test/shim/hw.h (the panel model), not src/
	mkdir -p build/host && cp src/oled.c build/host/oled.c
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Ibuild/host -Itest/shim -Isrc -o build/test_oled test/test_oled.c
	./build/test_oled

# pictures of the screens (../docs/images/screen-*.png) from the real ui_lines() + oled.c on the host
screens: build/screens
	python3 tools/screens.py

build/screens: test/screens.c src/logic.c src/oled.c src/*.h | build
	mkdir -p build/host && cp src/oled.c build/host/oled.c
	$(HOSTCC) -std=c11 -Wall -Wextra -Werror -Ibuild/host -Itest/shim -Isrc -o build/screens test/screens.c src/logic.c

flash: build/usbpd.bin
	wchisp flash build/usbpd.bin

clean:
	rm -rf build

.PHONY: all test screens flash clean
