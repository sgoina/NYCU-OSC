import struct

kernel_file_path = "kernel.img"

with open(kernel_file_path, "rb") as f:
    kernel_data = f.read()
    
header = struct.pack('<II', # < = litten-endian, I = unsigned int
    0x544F4F42,           # "BOOT" in hex
    len(kernel_data),     # size
)

with open('/dev/ttyUSB0', "wb", buffering = 0) as tty: # buffering = 0: no use buffer area and sending after writing 
    tty.write(header)
    tty.write(kernel_data)
