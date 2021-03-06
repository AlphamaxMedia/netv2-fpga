NETV2_DIR=../build

include $(NETV2_DIR)/software/include/generated/variables.mak
include $(SOC_DIRECTORY)/software/common.mak

ifeq ($(OS),Windows_NT)
COPY := cmd /c copy
else
COPY := cp
endif

OBJECTS=isr.o \
		config.o \
		processor.o \
		hdmi_in0.o \
		hdmi_in1.o \
		hdmi_out0.o \
		pattern.o \
		edid.o \
		mmcm.o \
		ci.o \
		encoder.o \
		i2c.o \
		main.o \
		bist.o \
		dump.o \
                km.o \
                compute_ksv.o \
		stdio_wrap.o \
                uptime.o

CFLAGS += -I.

all: firmware.bin

# pull in dependency info for *existing* .o files
-include $(OBJECTS:.o=.d)

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@
ifneq ($(OS),Windows_NT)
	chmod -x $@
endif
	$(COPY) $@ boot.bin

firmware.elf: $(OBJECTS)
	$(LD) $(LDFLAGS) \
		-T linker.ld \
		-N -o $@ \
		 $(NETV2_DIR)/software/libbase/crt0-$(CPU)-ctr.o \
		$(OBJECTS) \
		-L$(NETV2_DIR)/software/libbase \
		-L$(NETV2_DIR)/software/libcompiler_rt \
		-lbase-nofloat -lcompiler_rt
ifneq ($(OS),Windows_NT)
	chmod -x $@
endif

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
