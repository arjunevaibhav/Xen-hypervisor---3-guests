# FreeRTOS Xen guest (32-bit ARM). Bare metal: no libc, fixed load address.
#
#   make CC=arm-...-gcc OBJCOPY=arm-...-objcopy KERNEL=<FreeRTOS-Kernel dir>

KERNEL  ?= FreeRTOS-Kernel
PORT     = $(KERNEL)/portable/GCC/ARM_CA9

ARCHFLAGS = -march=armv7ve -marm -mfpu=neon-vfpv4 -mfloat-abi=hard
CFLAGS    = $(ARCHFLAGS) -O2 -g -Wall -ffreestanding -fno-builtin -fno-common \
            -fno-pie -fno-pic -fno-stack-protector -mno-unaligned-access \
            -fno-tree-loop-distribute-patterns \
            -I. -I$(KERNEL)/include -I$(PORT)
LDFLAGS   = $(ARCHFLAGS) -nostdlib -nostartfiles -static -no-pie -T guest.ld

SRCS = startup.S main.c \
       $(KERNEL)/tasks.c $(KERNEL)/list.c $(KERNEL)/queue.c \
       $(KERNEL)/portable/MemMang/heap_4.c \
       $(PORT)/port.c $(PORT)/portASM.S

OBJS = $(patsubst %.c,%.o,$(patsubst %.S,%.o,$(notdir $(SRCS))))
vpath %.c . $(KERNEL) $(KERNEL)/portable/MemMang $(PORT)
vpath %.S . $(PORT)

all: freertos-xen-guest.bin

%.o: %.c FreeRTOSConfig.h
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

freertos-xen-guest.elf: $(OBJS) guest.ld
	$(CC) $(LDFLAGS) $(OBJS) -lgcc -o $@

freertos-xen-guest.bin: freertos-xen-guest.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f *.o *.elf *.bin

.PHONY: all clean
