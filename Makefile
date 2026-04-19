RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy

INC_DIR = include
SRC_DIR = src

# -mcmodel=medany: access a global symbol by PC-relative
# -ffreestanding: Independent running (No int main(void))
# -nostdlib: No common Libraries
# -g: GNU Debugger
# -Wall: Open all warning
# -fno-pie: (Link) access global symbol use absolute address instead of global offset table (GOT)
CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall -fno-pie -I$(INC_DIR)
TARGET = boot_loader

SRCS_S = start.S
SRCS_C_COMMON = $(wildcard $(SRC_DIR)/*.c)
COMMON_OBJS = $(patsubst %.S, %.o, $(SRCS_S)) \
    	      $(patsubst %.c, %.o, $(SRCS_C_COMMON))
              
       
.PHONY: all clean boot_loader kernel

all: boot_loader kernel

boot_loader: kernel.fit

kernel: kernel.img


%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.elf: %.o $(COMMON_OBJS)
	$(LD) -T link.ld -o $@ $^
	
%.img: %.elf
	$(OBJCOPY) -O binary $< $@
	
kernel.fit: boot_loader.elf
	$(OBJCOPY) -O binary $< kernel.bin
	mkimage -f kernel.its $@

clean:
	rm -f boot_loader.elf kernel.bin kernel.fit boot_loader.o
	rm -f kernel.elf kernel.o kernel.img
	rm -f $(COMMON_OBJS)
