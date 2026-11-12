import subprocess
import time
import os
import struct

repo_dir = r"C:\Users\m1oky\Desktop\GrassGreen-v-4-1-0"
iso_path = os.path.join(repo_dir, "custom_os.iso")
ppm1 = os.path.join(repo_dir, "tab_desktop.ppm")
ppm2 = os.path.join(repo_dir, "tab_terminal.ppm")
bmp1 = os.path.join(repo_dir, "tab_desktop.bmp")
bmp2 = os.path.join(repo_dir, "tab_terminal.bmp")

for p in [ppm1, ppm2, bmp1, bmp2]:
    if os.path.exists(p): os.remove(p)

qemu_exe = r"C:\Program Files\qemu\qemu-system-i386.exe"

proc = subprocess.Popen(
    [qemu_exe, "-cdrom", iso_path, "-m", "256M", "-display", "none", "-monitor", "stdio"],
    cwd=repo_dir,
    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
)

print("1. Waiting for OS boot (3.0s)...")
time.sleep(3.0)

print("2. Screendump Tab 1 (desktop)...")
proc.stdin.write("screendump tab_desktop.ppm\n")
proc.stdin.flush()
time.sleep(2.0)

print("3. Switching to Tab 2 (terminal) via 'sendkey 2'...")
proc.stdin.write("sendkey 2\n")
proc.stdin.flush()
time.sleep(2.0)

print("4. Screendump Tab 2 (terminal)...")
proc.stdin.write("screendump tab_terminal.ppm\n")
proc.stdin.flush()
time.sleep(2.0)

print("5. Exiting QEMU...")
proc.stdin.write("quit\n")
proc.stdin.flush()

try:
    proc.communicate(timeout=20)
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
    print(f"Saved {p_out} ({w}x{h}, size {file_size})")

convert_ppm_to_bmp(ppm1, bmp1)
convert_ppm_to_bmp(ppm2, bmp2)

# Verification
with open(ppm1, "rb") as f1, open(ppm2, "rb") as f2:
    f1.readline(); f1.readline(); f1.readline()
    f2.readline(); f2.readline(); f2.readline()
    r1, r2 = f1.read(), f2.read()

def p1(x, y):
    i = (y * 1024 + x) * 3
    return '%02x%02x%02x' % (r1[i], r1[i+1], r1[i+2])

def p2(x, y):
    i = (y * 1024 + x) * 3
    return '%02x%02x%02x' % (r2[i], r2[i+1], r2[i+2])

diff_bytes = sum(1 for a, b in zip(r1, r2) if a != b)
print("\n--- TAB SWITCH VERIFICATION REPORT ---")
print(f"Total diff bytes: {diff_bytes}")
print(f"Tab 1 (Desktop)  -> Btn1 (60, 130): #{p1(60, 130)}, Btn2 (320, 130): #{p1(320, 130)}, Tab2 Btn (60, 390): #{p1(60, 390)}")
print(f"Tab 2 (Terminal) -> Btn1 (60, 130): #{p2(60, 130)}, Btn2 (320, 130): #{p2(320, 130)}, Tab2 Btn (60, 390): #{p2(60, 390)}")
if p1(60, 130) == "162b2e" and p2(60, 390) == "2a1820" and p2(60, 130) == "09090b":
    print("SUCCESS: Tab switching, widget rendering, and screen clearing verified 100%!")
else:
    print("FAILURE: Colors do not match expected values!")
