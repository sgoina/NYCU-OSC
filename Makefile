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

# ==========================================
# Bootloader 變數與路徑設定
# ==========================================
BL_DIR = bootloader
BL_INC = $(BL_DIR)/include
BL_SRC_DIR = $(BL_DIR)/src
BL_CFLAGS = $(CFLAGS) -I$(BL_INC)

BL_SRCS_S = $(BL_DIR)/start.S
BL_SRCS_C = $(BL_DIR)/main.c $(wildcard $(BL_SRC_DIR)/*.c)
BL_OBJS = $(patsubst %.S, %.o, $(BL_SRCS_S)) $(patsubst %.c, %.o, $(BL_SRCS_C))

# ==========================================
# Kernel 變數與路徑設定
# ==========================================
KERN_DIR = kernel
KERN_INC = $(KERN_DIR)/include
KERN_SRC_DIR = $(KERN_DIR)/src
KERN_CFLAGS = $(CFLAGS) -I$(KERN_INC)

KERN_SRCS_S = $(KERN_DIR)/start.S
KERN_SRCS_C = $(KERN_DIR)/main.c $(wildcard $(KERN_SRC_DIR)/*.c)
KERN_OBJS = $(patsubst %.S, %.o, $(KERN_SRCS_S)) $(patsubst %.c, %.o, $(KERN_SRCS_C))


.PHONY: all clean boot_loader kernel

all: boot_loader kernel

boot_loader: kernel.fit

kernel: kernel.img

# ==========================================
# Bootloader 編譯規則
# ==========================================
$(BL_DIR)/%.o: $(BL_DIR)/%.S
	$(CC) $(BL_CFLAGS) -c $< -o $@

$(BL_DIR)/%.o: $(BL_DIR)/%.c
	$(CC) $(BL_CFLAGS) -c $< -o $@

boot_loader.elf: $(BL_OBJS)
	$(LD) -T $(BL_DIR)/link.ld -o $@ $^
	
kernel.fit: boot_loader.elf
	$(OBJCOPY) -O binary $< $(BL_DIR)/kernel.bin
	mkimage -f $(BL_DIR)/kernel.its $@

# ==========================================
# Kernel 編譯規則
# ==========================================
$(KERN_DIR)/%.o: $(KERN_DIR)/%.S
	$(CC) $(KERN_CFLAGS) -c $< -o $@

$(KERN_DIR)/%.o: $(KERN_DIR)/%.c
	$(CC) $(KERN_CFLAGS) -c $< -o $@

kernel.elf: $(KERN_OBJS)
	$(LD) -T $(KERN_DIR)/link.ld -o $@ $^
	
kernel.img: kernel.elf
	$(OBJCOPY) -O binary $< $@

# ==========================================
# 清理規則
# ==========================================
clean:
	rm -f boot_loader.elf kernel.fit $(BL_DIR)/kernel.bin
	rm -f kernel.elf kernel.img kernel.bin
	rm -f $(BL_OBJS) $(KERN_OBJS)
