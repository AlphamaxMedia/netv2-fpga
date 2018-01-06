NETV2_DIR=../build

include $(NETV2_DIR)/software/include/generated/variables.mak
include $(SOC_DIRECTORY)/software/common.mak

OBJECTS=isr.o \
		config.o \
		processor.o \
		hdmi_in0.o \
		hdmi_out0.o \
		pattern.o \
		edid.o \
		mmcm.o \
		ci.o \
		encoder.o \
		i2c.o \
		main.o \
		stdio_wrap.o

CFLAGS += -I.

all: firmware.bin

# pull in dependency info for *existing* .o files
-include $(OBJECTS:.o=.d)

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
	chmod -x $@
	cp $@ boot.bin

firmware.elf: $(OBJECTS)
	$(LD) $(LDFLAGS) \
		-T linker.ld \
		-N -o $@ \
		 $(NETV2_DIR)/software/libbase/crt0-$(CPU).o \
		$(OBJECTS) \
		-L$(NETV2_DIR)/software/libbase \
		-L$(NETV2_DIR)/software/libcompiler_rt \
		-lbase-nofloat -lcompiler_rt
	chmod -x $@

main.o: main.c
	$(compile)

%.o: %.c
	$(compile)

%.o: %.S
	$(assemble)

load: firmware.bin
	litex_term --kernel firmware.bin COM8

clean:
	$(RM) $(OBJECTS) $(OBJECTS:.o=.d) firmware.elf firmware.bin .*~ *~

.PHONY: all main.o clean load