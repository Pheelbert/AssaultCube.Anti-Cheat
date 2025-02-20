#!/usr/bin/env python3
import sys
import os
import pefile  # python -m pip install pefile

def fnv1a_hash(data: bytes) -> int:
    h = 2166136261
    for byte in data:
        h ^= byte
        h = (h * 16777619) & 0xffffffff
    return h

def hash_to_hex(h: int) -> str:
    return '{:08x}'.format(h)

def compute_file_hash(filepath: str) -> str:
    with open(filepath, "rb") as f:
        data = f.read()
    h = fnv1a_hash(data)
    return hash_to_hex(h)

def simulate_memory_mapping(filepath: str) -> bytes:
    """
    Simulate Windows' in-memory mapping of a PE file:
      - Allocate a buffer of size SizeOfImage.
      - Copy the headers (0..SizeOfHeaders) from the file.
      - For each section, copy raw data from the file (from PointerToRawData)
        into the buffer at the section's VirtualAddress.
    """
    with open(filepath, "rb") as f:
        file_data = f.read()
    pe = pefile.PE(data=file_data)
    size_of_image = pe.OPTIONAL_HEADER.SizeOfImage
    size_of_headers = pe.OPTIONAL_HEADER.SizeOfHeaders

    # Create a zero-initialized buffer sized to SizeOfImage.
    mem_image = bytearray(size_of_image)
    # Copy the headers.
    mem_image[0:size_of_headers] = file_data[0:size_of_headers]

    # For each section, copy its raw data into the buffer at VirtualAddress.
    for section in pe.sections:
        va = section.VirtualAddress
        size_raw = section.SizeOfRawData
        ptr_raw = section.PointerToRawData
        # Ensure we don't read beyond file_data.
        if ptr_raw + size_raw > len(file_data):
            size_raw = len(file_data) - ptr_raw
        mem_image[va:va+size_raw] = file_data[ptr_raw:ptr_raw+size_raw]
    return bytes(mem_image)

def compute_text_section_hash(filepath: str) -> str:
    # Simulate the in-memory image of the module.
    mem_image = simulate_memory_mapping(filepath)
    # Re-parse the PE headers from the simulated memory image.
    pe = pefile.PE(data=mem_image)
    text_data = None
    # Find the .text section in the simulated memory image.
    for section in pe.sections:
        section_name = section.Name.decode(errors="ignore").strip('\x00')
        if section_name == ".text":
            va = section.VirtualAddress
            size_raw = section.SizeOfRawData
            size_virtual = section.Misc_VirtualSize
            section_size = max(size_raw, size_virtual)
            print(f"Found .text section at VA 0x{va:08X} with size {section_size} bytes.")
            text_data = mem_image[va:va+section_size]
            break

    if text_data is None:
        raise ValueError("No .text section found in the file.")
    h = fnv1a_hash(text_data)
    return hash_to_hex(h)

def main():
    if len(sys.argv) < 2:
        print("Usage: {} <ac_client.exe>".format(sys.argv[0]))
        sys.exit(1)

    filepath = sys.argv[1]
    if not os.path.isfile(filepath):
        print("Error: file '{}' does not exist.".format(filepath))
        sys.exit(1)

    try:
        file_hash = compute_file_hash(filepath)
        text_hash = compute_text_section_hash(filepath)
    except Exception as e:
        print("Error:", e)
        sys.exit(1)

    config_contents = ""
    config_contents += f"HASH_FILE_AC_DOT_EXE={file_hash}\n"
    config_contents += f"HASH_MEMORY_DOT_TEXT={text_hash}\n"

    config_file_path = "./config/anticheat.cfg"
    with open(config_file_path, "w") as h:
        h.write(config_contents)

    print(f"Wrote config to {config_file_path}")
    print(config_contents)

if __name__ == "__main__":
    main()
