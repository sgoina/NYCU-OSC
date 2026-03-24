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
TARGET = kernel

SRCS_S = start.S
SRCS_C = main.c $(wildcard $(SRC_DIR)/*.c)

OBJS = $(patsubst %.S, %.o, $(SRCS_S)) \
       $(patsubst %.c, %.o, $(SRCS_C))

all: $(TARGET).img

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(LD) -T link.ld -o $@ $(OBJS)
	
$(TARGET).img: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	
clean:
	rm $(TARGET).img $(TARGET).elf $(OBJS)

.PHONY: all clean
