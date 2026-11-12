import subprocess
import time
import socket
import os
import struct

repo_dir = r"C:\Users\m1oky\Desktop\GrassGreen-v-4-1-0"
iso_path = os.path.join(repo_dir, "custom_os.iso")
ppm1 = os.path.join(repo_dir, "tab1.ppm")
ppm2 = os.path.join(repo_dir, "tab2.ppm")
bmp1 = os.path.join(repo_dir, "tab1.bmp")
bmp2 = os.path.join(repo_dir, "tab2.bmp")

for p in [ppm1, ppm2, bmp1, bmp2]:
    if os.path.exists(p): os.remove(p)

qemu_exe = r"C:\Program Files\qemu\qemu-system-i386.exe"

# Launch QEMU with monitor on tcp:127.0.0.1:4545 and serial on stdio
proc = subprocess.Popen(
    [qemu_exe, "-cdrom", iso_path, "-m", "256M", "-display", "none",
     "-monitor", "tcp:127.0.0.1:4545,server,nowait",
     "-serial", "pipe:COM1"],
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE
)

print("Waiting for QEMU to boot...")
time.sleep(2.5)

# Connect to monitor
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
connected = False
for attempt in range(10):
    try:
        s.connect(("127.0.0.1", 4545))
        connected = True
        break
    except:
        time.sleep(0.5)

if not connected:
    print("Failed to connect to QEMU monitor!")
    proc.kill()
    exit(1)

def send_mon(cmd):
    s.sendall((cmd + "\n").encode())
    time.sleep(0.5)

print("Dumping tab1 screenshot...")
send_mon(f"screendump {ppm1.replace(chr(92), '/')}")
time.sleep(1.0)

print("Sending mouse move and click on 'Открыть терминал' button at (170, 142)...")
# Mouse is at (512, 384). Target is (170, 142). Delta is (-342, -242).
send_mon("mouse_move -342 -242")
time.sleep(0.3)
send_mon("mouse_button 1")
time.sleep(0.1)
send_mon("mouse_button 0")
time.sleep(1.0)

print("Dumping tab2 screenshot after mouse click...")
send_mon(f"screendump {ppm2.replace(chr(92), '/')}")
time.sleep(1.0)

send_mon("quit")
s.close()

try:
    proc.communicate(timeout=5)
except:
    proc.kill()
    proc.communicate()

def convert_ppm_to_bmp(p_in, p_out):
    if not os.path.exists(p_in):
        print(f"File {p_in} not found!")
        return
    with open(p_in, "rb") as f:
        magic = f.readline()
        dim = f.readline().split()
        w, h = int(dim[0]), int(dim[1])
        maxv = f.readline()
        raw = f.read()
    row_len = w * 3
    pad = (4 - (row_len % 4)) % 4
    bmp_data = bytearray()
    for y in range(h):
        row = raw[y*row_len:(y+1)*row_len]
        bgr_row = bytearray()
        for i in range(0, len(row), 3):
            r, g, b = row[i], row[i+1], row[i+2]
            bgr_row.extend([b, g, r])
        bgr_row.extend(b"\x00" * pad)
        bmp_data.extend(bgr_row)
    file_size = 54 + len(bmp_data)
    header = struct.pack("<2sIHHI", b"BM", file_size, 0, 0, 54)
    dib = struct.pack("<IIIHHIIIIII", 40, w, -h & 0xFFFFFFFF, 1, 24, 0, len(bmp_data), 2835, 2835, 0, 0)
    with open(p_out, "wb") as out:
        out.write(header + dib + bmp_data)
    print(f"Saved {p_out} ({w}x{h})")

convert_ppm_to_bmp(ppm1, bmp1)
convert_ppm_to_bmp(ppm2, bmp2)
