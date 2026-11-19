######################################
# GB47 - a Game Boy for the SwissMicros DM42n (DMCP5 native program, .pg5), and its tool FPS47
# Based on the SwissMicros DMCP5_SDK Makefile (BSD-3), as ZX47, HP47, Chess 47, NAV47 and TERM47
######################################
# make                 build/GB47.pg5 (loads on DMCP5 3.57 and later: interface 3.17)
# make test            the PC tests: Blargg's CPU tests, dmg-acid2, the ROM cache, save states, the sound
# make sim             the PC simulator (build/sim/sim); make shots: its screens -> build/sim/run/*.png
# make demo            release/GB47DEMO.GB, our own little cartridge (tools/mkdemo.py)
# make zip             build/GB47-$(VERSION).zip for the GitHub release: GB47.pg5, the demo, the manual, LICENSE
# make fps             tools/fps47: FPS47.pg5, the LCD frame-rate test -> build/fps/FPS47.pg5
# make fps-sim         its PC simulator (build/fps/sim); screens: make fps-shots -> build/fps/run/*.png

BUILD_DIR = build
PYTHON ?= python3

CC      = arm-none-eabi-gcc
AS      = arm-none-eabi-gcc -x assembler-with-cpp
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size

# the DMCP5 interface the .pg5 asks for: 17 loads on DMCP5 3.57 and later (FPS47 needs nothing newer);
# 18 (DMCP5 3.58) only for the serial console, later in the emulators: make IFC=18
IFC ?= 17
C_DEFS   = -D__weak="__attribute__((weak))" -D__packed="__attribute__((__packed__))" -DPLATFORM_IFC_VER=$(IFC)
CPUFLAGS = -mthumb -march=armv7e-m -mfloat-abi=hard -mfpu=fpv4-sp-d16
LDSCRIPT = stm32_program.ld

all: $(BUILD_DIR)/GB47.pg5

# ------------------------------------------------------------------ GB47
TARGET     = GB47
C_SOURCES  = dmcp/sys/pgm_syscalls.c src/main.c src/gb.c src/sound.c src/screen.c src/keys.c
ASM_SOURCES = dmcp/startup_pgm.s
OBJ_DIR    = $(BUILD_DIR)/obj
CFLAGS     = -std=gnu11 $(CPUFLAGS) $(C_DEFS) -Idmcp -Isrc -Ithird_party/peanut_gb -Wall -fdata-sections \
             -ffunction-sections -O2 -g -MD -MP -MF $(OBJ_DIR)/$(@F).d
LDFLAGS    = $(CPUFLAGS) -T$(LDSCRIPT) -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref -Wl,--gc-sections \
             -Wl,--wrap=_malloc_r -g
OBJECTS    = $(addprefix $(OBJ_DIR)/,$(notdir $(C_SOURCES:.c=.o))) $(addprefix $(OBJ_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(OBJ_DIR)/%.o: %.c Makefile | $(OBJ_DIR)
	$(CC) -c $(CFLAGS) $< -o $@
$(OBJ_DIR)/%.o: %.s Makefile | $(OBJ_DIR)
	$(AS) -c $(CFLAGS) $< -o $@
$(OBJ_DIR):
	mkdir -p $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

# QSPI CRC is compiled into the program header: if it changes, rebuild once.
$(BUILD_DIR)/$(TARGET).pg5: $(BUILD_DIR)/$(TARGET).elf tools/pg5tool.py
	$(OBJCOPY) --remove-section .qspi -O binary $< $(BUILD_DIR)/$(TARGET)_flash.bin
	$(OBJCOPY) --only-section   .qspi -O binary $< $(BUILD_DIR)/$(TARGET)_qspi.bin
	@if ! $(PYTHON) tools/pg5tool.py qspi $(BUILD_DIR)/$(TARGET)_qspi.bin src/qspi_crc.h; then \
	   rm -f $(OBJ_DIR)/pgm_syscalls.o $<; $(MAKE) --no-print-directory $@; exit $$?; fi
	$(PYTHON) tools/pg5tool.py pg5 $(BUILD_DIR)/$(TARGET)_flash.bin $@
	$(SIZE) $<

VERSION = $(shell sed -n 's/.*PROGRAM_VERSION *"\(.*\)".*/\1/p' src/main.h)
zip: $(BUILD_DIR)/GB47.pg5
	rm -rf $(BUILD_DIR)/zip && mkdir -p $(BUILD_DIR)/zip/GB47-$(VERSION)
	cp $(BUILD_DIR)/GB47.pg5 release/GB47DEMO.GB docs/MANUAL.md LICENSE $(BUILD_DIR)/zip/GB47-$(VERSION)/
	sed -i 's|(../README.md)|(https://github.com/valdesvj/gb47.pg5)|' $(BUILD_DIR)/zip/GB47-$(VERSION)/MANUAL.md
	cd $(BUILD_DIR)/zip && $(PYTHON) -c "import shutil; shutil.make_archive('../GB47-$(VERSION)', 'zip', '.', 'GB47-$(VERSION)')"
	@echo "-> $(BUILD_DIR)/GB47-$(VERSION).zip"

# ------------------------------------------------------------------ PC tests, demo, simulator
T = $(BUILD_DIR)/test
test:
	mkdir -p $(T)
	gcc -std=gnu11 -O2 -Wall -DGB47_TEST -Isrc -Ithird_party/peanut_gb -o $(T)/gbtest test/gbtest.c src/gb.c src/sound.c
	cd $(T) && ./gbtest

demo:
	$(PYTHON) tools/mkdemo.py release/GB47DEMO.GB

S = $(BUILD_DIR)/sim
sim:
	mkdir -p $(S)
	gcc -std=gnu11 -O2 -w '-D__packed=__attribute__((__packed__))' '-D__weak=__attribute__((weak))' -DGB47_SIM \
	    -Itest/sim -Isrc -Ithird_party/peanut_gb -o $(S)/sim test/sim/gbsim.c src/main.c src/gb.c src/sound.c \
	    src/screen.c src/keys.c

# the screens: the menu, dmg-acid2 big, two-tone, small, the demo playing, the ROM cache, About
shots: sim demo
	rm -rf $(S)/run && mkdir -p $(S)/run/simdisk/GB47
	$(PYTHON) tools/hex2bin.py test/roms/dmg-acid2.gb.h $(S)/run/simdisk/GB47/ACID2.GB
	cp release/GB47DEMO.GB $(S)/run/simdisk/GB47/
	cd $(S)/run && GB47_FILE=/GB47/ACID2.GB ../sim WAIT:300 SNAP:menu F1 WAIT:2500 SNAP:acid2 F3 WAIT:400 SNAP:two \
	    F3 F4 WAIT:600 SNAP:small F4 WAIT:300
	cd $(S)/run && GB47_FILE=/GB47/GB47DEMO.GB SIM_HEAP=120 ../sim EXIT WAIT:300 F1 WAIT:3000 SNAP:demo \
	    6 6 6 6 6 6 MUL WAIT:200 SNAP:demo2 F6 WAIT:2200 SNAP:speed EXIT WAIT:300 8 WAIT:300 SNAP:about EXIT
	for n in menu acid2 two small demo demo2 speed about; do $(PYTHON) test/sim/render.py $(S)/run/$$n $(S)/run/$$n.png; done

# FPS47: the LCD frame-rate test (tools/fps47), its own program on the DMCP base
FPS_DIR = $(BUILD_DIR)/fps
FPS_CFLAGS = -std=gnu11 $(CPUFLAGS) $(C_DEFS) -Itools/fps47 -Idmcp -Wall -fdata-sections -ffunction-sections -O2 -g
fps:
	mkdir -p $(FPS_DIR)
	$(CC) $(FPS_CFLAGS) -c dmcp/sys/pgm_syscalls.c -o $(FPS_DIR)/pgm_syscalls.o
	$(CC) $(FPS_CFLAGS) -c tools/fps47/fps47.c -o $(FPS_DIR)/fps47.o
	$(AS) -c $(FPS_CFLAGS) dmcp/startup_pgm.s -o $(FPS_DIR)/startup_pgm.o
	$(CC) $(FPS_DIR)/*.o $(CPUFLAGS) -T$(LDSCRIPT) -Wl,-Map=$(FPS_DIR)/FPS47.map,--cref -Wl,--gc-sections \
	    -Wl,--wrap=_malloc_r -g -o $(FPS_DIR)/FPS47.elf
	$(OBJCOPY) --remove-section .qspi -O binary $(FPS_DIR)/FPS47.elf $(FPS_DIR)/FPS47_flash.bin
	$(OBJCOPY) --only-section   .qspi -O binary $(FPS_DIR)/FPS47.elf $(FPS_DIR)/FPS47_qspi.bin
	$(PYTHON) tools/pg5tool.py qspi $(FPS_DIR)/FPS47_qspi.bin tools/fps47/qspi_crc.h
	$(PYTHON) tools/pg5tool.py pg5 $(FPS_DIR)/FPS47_flash.bin $(FPS_DIR)/FPS47.pg5
	$(SIZE) $(FPS_DIR)/FPS47.elf

fps-sim:
	mkdir -p $(FPS_DIR)
	gcc -std=gnu11 -O2 -w '-D__packed=__attribute__((__packed__))' '-D__weak=__attribute__((weak))' -DGB47_SIM \
	    -Itest/sim -Itools/fps47 -Isrc -o $(FPS_DIR)/sim test/sim/sim.c test/sim/sercon_sim.c tools/fps47/fps47.c

# the simulator's screens: the intro, the results, WATCH GB, WATCH ZX
fps-shots: fps-sim
	rm -rf $(FPS_DIR)/run && mkdir -p $(FPS_DIR)/run
	cd $(FPS_DIR)/run && ../sim SNAP:intro ENTER WAIT:200 SNAP:test1 WAIT:29000 SNAP:results F1 WAIT:2500 SNAP:watch EXIT F2 WAIT:2500 SNAP:watchzx EXIT
	for n in intro test1 results watch watchzx; do $(PYTHON) test/sim/render.py $(FPS_DIR)/run/$$n $(FPS_DIR)/run/$$n.png; done

clean:
	-rm -fR $(BUILD_DIR)

-include $(wildcard $(OBJ_DIR)/*.d)

.PHONY: all zip test demo sim shots fps fps-sim fps-shots clean
