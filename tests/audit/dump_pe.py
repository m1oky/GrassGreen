import struct, sys

with open("multifile_app.exe", "rb") as f:
    data = f.read()

e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
magic, machine, num_sec, time_date, sym_tab, num_sym, opt_hdr_sz, chars = struct.unpack_from("<IHHIIIHH", data, e_lfanew)
opt_magic = struct.unpack_from("<H", data, e_lfanew + 24)[0]
entry_point, code_base = struct.unpack_from("<II", data, e_lfanew + 24 + 16)
image_base = struct.unpack_from("<Q", data, e_lfanew + 24 + 24)[0]
subsystem = struct.unpack_from("<H", data, e_lfanew + 24 + 68)[0]
print(f"PE Magic: {hex(magic)}, OptMagic: {hex(opt_magic)}")
print(f"Entry point RVA: {hex(entry_point)}, ImageBase: {hex(image_base)}, Subsystem: {subsystem}")

# Section headers
sec_hdr_off = e_lfanew + 24 + opt_hdr_sz
for i in range(num_sec):
    name, virt_sz, virt_addr, raw_sz, raw_ptr = struct.unpack_from("<8sIIII", data, sec_hdr_off + i * 40)
    print(f"Sec {name.decode('latin1').strip(chr(0))}: VA={hex(virt_addr)}, VSz={hex(virt_sz)}, RawPtr={hex(raw_ptr)}, RawSz={hex(raw_sz)}")
