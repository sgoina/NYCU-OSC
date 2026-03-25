import struct

kernel_file_path = "kernel.img"

with open(kernel_file_path, "rb") as f:
    kernel_data = f.read()
    
header = struct.pack('<II', 0x544F4F42, len(kernel_data))

with open('/dev/ttyUSB0', "wb", buffering = 0) as tty:
    tty.write(header)
    tty.write(kernel_data)
