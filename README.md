# NYCU-OSC

This repository contains my Lab 1–7 implementations for **Operating System Capstone (IOC5226), Spring 2026, at National Yang Ming Chiao Tung University**. Targeting the **OrangePi RV2 / RISC-V 64-bit** platform, the project uses C and RISC-V assembly to grow a bare-metal program into an operating system kernel with memory management, interrupt handling, multitasking, virtual memory, and file systems.

Full assignment requirements are available on the [OSC2026 course website](https://nycu-caslab.github.io/OSC2026/). The feature summaries below reflect the code in this repository.

## Project Goals

- Understand how a kernel initializes at boot, discovers hardware information, and accesses peripherals.
- Implement physical memory allocation, exception and interrupt handling, thread scheduling, and user processes.
- Establish separate process address spaces through paging and access permissions.
- Provide a unified file interface for memory-based file systems and device drivers.

Each `labN/` directory preserves the implementation at that stage. Later labs build on earlier functionality, so reading them in order shows how the kernel evolves.

## Repository Structure

```text
NYCU-OSC/
├── lab1/    # Bare-metal initialization, UART, and a basic shell
├── lab2/    # UART bootloader, Device Tree, and initramfs
├── lab3/    # Buddy system and dynamic memory allocation
├── lab4/    # Exceptions, interrupts, asynchronous UART, and timers
├── lab5/    # Threads, user processes, system calls, and signals
├── lab6/    # Sv39 virtual memory, demand paging, and copy-on-write
└── lab7/    # VFS, tmpfs, ramfs, and device files
```

Common files and directories:

| Path / File | Purpose |
| --- | --- |
| `src/`, `include/` | C source files and headers; from Lab 4 onward, each bootloader and kernel has its own copies. |
| `start.S`, `link.ld` | Startup code, assembly entry points, and memory layout at link time. |
| `boot_loader.c`, `kernel.c` | Bootloader and kernel entry points in Labs 2–3. |
| `bootloader/`, `kernel/` | Separate bootloader and kernel implementations in Labs 4–7. |
| `rootfs/`, `initramfs.cpio` | Initial file contents and their CPIO archive, introduced in Lab 2 and placed under `bootloader/` from Lab 4 onward. |
| `x1_orangepi-rv2.dtb`, `kernel.its` | Board Device Tree blob and U-Boot FIT image description. |
| `Makefile`, `upload.py` | Cross-compilation and image packaging; Labs 2–7 also include a Python script for uploading the kernel over UART. |

## Lab Highlights

### Lab 1 — Hello World

[Source](lab1/) · [Assignment](https://nycu-caslab.github.io/OSC2026/labs/lab1.html)

Build a minimal interactive environment on the board.

- Clear `.bss` and initialize the stack in startup assembly.
- Access UART through memory-mapped registers for polling-based character input and output.
- Implement a basic shell with `help`, `hello`, and `info` commands.
- Wrap SBI `ecall` to query the OpenSBI specification version, implementation ID, and implementation version.

### Lab 2 — Booting

[Source](lab2/) · [Assignment](https://nycu-caslab.github.io/OSC2026/labs/lab2.html)

Load updated kernels over the serial connection and obtain hardware and file information from firmware-provided data.

- Implement a UART bootloader and `upload.py`, using a transfer header containing the `BOOT` magic value and image size.
- Support bootloader self-relocation to free its original location for the kernel, then transfer control to the loaded image.
- Parse the Flattened Device Tree (DTB) to locate UART and the initramfs memory range.
- Parse CPIO archives in `newc` format and provide `ls` and `cat` shell commands to list and read files.

### Lab 3 — Memory Allocator

[Source](lab3/) · [Assignment](https://nycu-caslab.github.io/OSC2026/labs/lab3.html)

Allow the kernel to allocate and reclaim memory at runtime for kernel data structures and processes.

- Implement a buddy allocator using 4 KiB page frames, with block splitting, allocation, release, and merging.
- Build chunk pools for allocation sizes from 16 to 512 bytes on top of the page allocator.
- Read memory ranges and reserved regions from the Device Tree, and reserve space occupied by the kernel, DTB, initramfs, and allocator metadata.
- Find a safe location for the page metadata array during startup, and provide allocation logs and test functions.

### Lab 4 — Exception and Interrupt

[Source](lab4/) · [Assignment](https://nycu-caslab.github.io/OSC2026/labs/lab4.html)

Handle hardware events and defer work beyond the initial interrupt handler.

- Establish trap entry code that saves and restores registers and distinguishes synchronous exceptions from interrupts.
- Configure the PLIC and handle UART interrupts, using ring buffers for asynchronous input and output.
- Use the SBI timer to implement a timer queue, expiration callbacks, and the `settimeout` command.
- Implement a deferred work queue with priorities, allowing higher-priority work to run during nested interrupt handling.

### Lab 5 — Thread and User Process

[Source](lab5/) · [Assignment](https://nycu-caslab.github.io/OSC2026/labs/lab5.html)

Extend the kernel to support schedulable threads and U-mode user processes.

- Implement thread creation, context switching, round-robin scheduling, an idle thread, and zombie resource cleanup.
- Trigger preemptive scheduling through periodic timer interrupts and load user programs from initramfs.
- Provide system calls including `getpid`, UART read/write, `exec`, `fork`, `waitpid`, `exit`, `stop`, and `usleep`, along with framebuffer image display.
- Support signal handler registration, pending signals, a signal stack, and `sigreturn` to restore interrupted execution.

### Lab 6 — Virtual Memory

[Source](lab6/) · [Assignment](https://nycu-caslab.github.io/OSC2026/labs/lab6.html)

Enable RISC-V Sv39 paging and establish virtual address spaces for the kernel and user processes.

- Build three-level page tables and a higher-half kernel mapping, remove the temporary identity mapping after enabling the MMU, and configure device mappings.
- Give each process its own page tables, track code, stack, and virtual memory areas (VMAs), and update `satp` when switching address spaces.
- Provide anonymous `mmap` with either immediate allocation (`MAP_POPULATE`) or demand paging through page faults.
- Share page frames during `fork` using reference counts, copy pages on write through copy-on-write, and terminate processes on invalid memory access.

### Lab 7 — Virtual File System

[Source](lab7/) · [Assignment](https://nycu-caslab.github.io/OSC2026/labs/lab7.html)

Unify access to files, directories, and devices through VFS and expose resources to user programs through file descriptors.

- Implement vnodes, file handles, file system registration, mounting, path lookup, and traversal across mount points.
- Use a writable tmpfs as the root file system at `/`, and expose the initramfs CPIO contents through a read-only ramfs mounted at `/ramfs`.
- Maintain per-process file descriptor tables and working directories, with system calls including `open`, `close`, `read`, `write`, `mkdir`, `mount`, and `chdir`.
- Create `/dev/uart` and `/dev/fb` device files, with framebuffer position control through `lseek64` and information queries through `ioctl`.

## Build Overview

Before building, follow [Lab 0 — Environment Setup](https://nycu-caslab.github.io/OSC2026/labs/lab0.html) to install the development tools, prepare the bootable SD card, and configure the UART connection. The course primarily uses Linux; Windows users can use WSL with access to the serial device.

Each lab has its own `Makefile`, using the `riscv64-unknown-elf` toolchain by default and U-Boot's `mkimage` to package FIT images. Build from the desired lab directory:

```sh
cd lab7
make
```

Lab 1 produces a kernel FIT image, `kernel.fit`. From Lab 2 onward, the bootloader and kernel can also be built separately:

| Command | Output |
| --- | --- |
| `make` | Build both the bootloader and kernel. |
| `make boot_loader` | Build `kernel.fit`, which packages the bootloader for booting from the SD card. |
| `make kernel` | Build `kernel.img`, the kernel image sent over UART. |

The bootloader target is named `boot_loader` in the Makefiles. Install its `kernel.fit` on the SD card using the boot setup described in Lab 0.

Starting with **Lab 2**, `upload.py` supports sending `kernel.img` directly to the board over UART, so subsequent kernel updates do not require replacing the SD card image. For example, from the repository root:

```sh
cd lab2          # Or any later lab directory
make kernel
```

Boot the board into the bootloader and enter `load` in its serial console:

```text
OrangePi-RV2> load
```

Once the bootloader is waiting for the image, run this command in a separate host terminal from the same lab directory:

```sh
python3 upload.py
```

The script reads `kernel.img` from the current directory and sends it to `/dev/ttyUSB0`; update the device path in `upload.py` if your adapter uses a different port. The serial port must already be configured, and the host user must have write access to it. After receiving the image, the bootloader jumps to the kernel.
