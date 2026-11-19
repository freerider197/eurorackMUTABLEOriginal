# Tomline: alternative firmware for the Mutable Instruments Streams hardware.
# Build from the root of a pichenettes/eurorack checkout: make -f tomline/makefile

# System specifications (same as streams/makefile)
F_CRYSTAL      = 8000000L
F_CPU          = 72000000L
SYSCLOCK       = SYSCLK_FREQ_72MHz
FAMILY         = f10x
DENSITY        = md
MEMORY_MODE    = flash

APPLICATION    = TRUE
BOOTLOADER     = streams_bootloader

# Preferred upload command
UPLOAD_COMMAND  = upload_combo_jtag

# Packages to build
TARGET         = tomline
PACKAGES       = tomline streams/drivers stmlib/utils stmlib/system
RESOURCES      = streams/resources

include stmlib/makefile.inc

# Firmware update file for the Streams audio bootloader (same parameters as
# streams/makefile).
wav:  $(TARGET_BIN)
	python3 tomline/tools/encoder.py \
		-s 48000 -b 12000 -c 6000 -p 256 \
		$(TARGET_BIN)
