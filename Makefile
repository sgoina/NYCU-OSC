RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy

INC_DIR = include
SRC_DIR = src

# -mcmodel=medany: can load anywhere in memory
# -ffreestanding: Independent running (No int main(void))
# -nostdlib: No common Libraries
# -g: GNU Debugger
# -Wall: Open all warning
CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall -fno-pie -I$(INC_DIR)
TARGET = boot_loader

SRCS_C_COMMON = $(wildcard $(SRC_DIR)/*.c)
COMMON_OBJS = $(patsubst %.c, %.o, $(SRCS_C_COMMON))
              
boot_loader.elf: LINK_SCRIPT = bootLoader_link.ld

kernel.elf: LINK_SCRIPT = kernel_link.ld
       
.PHONY: all clean boot_loader kernel

all: boot_loader kernel

boot_loader: boot_loader.fit

kernel: kernel.img

boot_loader.elf: start_bootLoader.o $(COMMON_OBJS)
kernel.elf: start_kernel.o $(COMMON_OBJS)

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.elf: %.o $(COMMON_OBJS)
	$(LD) -T $(LINK_SCRIPT) -o $@ $^
	
%.img: %.elf
	$(OBJCOPY) -O binary $< $@
	
%.fit: %.elf
	$(OBJCOPY) -O binary $< $*.bin
	mkimage -f kernel.its $@

clean:
	rm -f boot_loader.elf boot_loader.bin boot_loader.fit boot_loader.o start_bootLoader.o
	rm -f kernel.elf kernel.o kernel.img start_kernel.o
	rm -f $(COMMON_OBJS)
