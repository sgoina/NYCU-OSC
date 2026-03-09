RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy

INC_DIR = include
SRC_DIR = src

CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall -I$(INC_DIR)
TARGET = kernel

SRCS_S = start.S
SRCS_C = main.c $(wildcard $(SRC_DIR)/*.c)

OBJS = $(patsubst %.S, %.o, $(SRCS_S)) \
       $(patsubst %.c, %.o, $(SRCS_C))

all: $(TARGET).fit

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(LD) -T link.ld -o $@ $(OBJS)

$(TARGET).fit: $(TARGET).elf
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin
	mkimage -f $(TARGET).its $@

clean:
	rm $(TARGET).elf $(TARGET).bin $(TARGET).fit $(OBJS)

.PHONY: all clean
