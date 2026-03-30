/*
 * hashgen.c - Post-build tool to generate config/anticheat.cfg
 *
 * Computes FNV-1a hashes of ac_client.exe (whole file and .text PE section)
 * matching the exact algorithm used by the game client at runtime, and writes
 * them to a config file that the server reads for hash verification.
 *
 * Usage: hashgen.exe <ac_client.exe path> <output cfg path>
 *
 * Build: cl.exe /nologo /O2 hashgen.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* PE structures (minimal, no Windows.h dependency) */
#pragma pack(push, 1)

typedef struct {
    uint16_t e_magic;
    uint16_t e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc;
    uint16_t e_ss, e_sp, e_csum, e_ip, e_cs, e_lfarlc, e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid, e_oeminfo;
    uint16_t e_res2[10];
    int32_t  e_lfanew;
} DOS_HEADER;

typedef struct {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} FILE_HEADER;

typedef struct {
    uint32_t Signature;
    FILE_HEADER FileHeader;
    /* Optional header follows but we only need FileHeader + section offset */
} NT_HEADERS_PARTIAL;

typedef struct {
    char     Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} SECTION_HEADER;

#pragma pack(pop)

#define IMAGE_DOS_SIGNATURE 0x5A4D
#define IMAGE_NT_SIGNATURE  0x00004550
#define FNV_OFFSET_BASIS    2166136261u
#define FNV_PRIME           16777619u

/* FNV-1a hash matching HashUtil.cpp FastHash exactly */
static uint32_t fnv1a_update(uint32_t hash, const unsigned char *data, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

/* Hash an entire file, returns FNV-1a as 8-char hex string */
static int hash_file(const char *path, char *out_hex)
{
    FILE *f;
    unsigned char buf[4096];
    size_t n;
    uint32_t hash = FNV_OFFSET_BASIS;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "hashgen: cannot open '%s'\n", path);
        return -1;
    }

    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        hash = fnv1a_update(hash, buf, n);

    fclose(f);
    sprintf(out_hex, "%08x", hash);
    return 0;
}

/* Hash the .text PE section from file on disk.
 * Uses max(SizeOfRawData, VirtualSize) to match MemoryIntegrityChecker. */
static int hash_text_section(const char *path, char *out_hex)
{
    FILE *f;
    DOS_HEADER dos;
    NT_HEADERS_PARTIAL nt;
    SECTION_HEADER sec;
    int i;
    uint32_t hash = FNV_OFFSET_BASIS;
    uint32_t section_size, raw_size, virt_size;
    unsigned char *data;
    size_t n;
    static const unsigned char zeros[4096] = {0};

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "hashgen: cannot open '%s'\n", path);
        return -1;
    }

    /* Read DOS header */
    if (fread(&dos, sizeof(dos), 1, f) != 1 || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        fprintf(stderr, "hashgen: invalid DOS header\n");
        fclose(f);
        return -1;
    }

    /* Read NT headers */
    fseek(f, dos.e_lfanew, SEEK_SET);
    if (fread(&nt, sizeof(nt), 1, f) != 1 || nt.Signature != IMAGE_NT_SIGNATURE) {
        fprintf(stderr, "hashgen: invalid NT header\n");
        fclose(f);
        return -1;
    }

    /* Skip optional header to reach section headers */
    fseek(f, dos.e_lfanew + 4 + sizeof(FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader, SEEK_SET);

    /* Find .text section */
    for (i = 0; i < nt.FileHeader.NumberOfSections; i++) {
        if (fread(&sec, sizeof(sec), 1, f) != 1) {
            fprintf(stderr, "hashgen: failed to read section header %d\n", i);
            fclose(f);
            return -1;
        }

        if (strncmp(sec.Name, ".text", 5) == 0 && (sec.Name[5] == '\0' || sec.Name[5] == ' ')) {
            raw_size = sec.SizeOfRawData;
            virt_size = sec.VirtualSize;
            section_size = (raw_size > virt_size) ? raw_size : virt_size;

            if (section_size == 0) {
                fprintf(stderr, "hashgen: .text section has zero size\n");
                fclose(f);
                return -1;
            }

            /* Read raw data from file */
            data = (unsigned char *)malloc(raw_size);
            if (!data) {
                fprintf(stderr, "hashgen: out of memory\n");
                fclose(f);
                return -1;
            }

            fseek(f, sec.PointerToRawData, SEEK_SET);
            n = fread(data, 1, raw_size, f);
            if (n != raw_size) {
                fprintf(stderr, "hashgen: short read on .text section (got %zu, expected %u)\n", n, raw_size);
                free(data);
                fclose(f);
                return -1;
            }
            fclose(f);

            /* Hash the raw data */
            hash = fnv1a_update(hash, data, raw_size);
            free(data);

            /* If VirtualSize > SizeOfRawData, hash zero padding to match
             * the in-memory layout that MemoryIntegrityChecker sees */
            if (virt_size > raw_size) {
                uint32_t pad = virt_size - raw_size;
                while (pad > 0) {
                    uint32_t chunk = (pad > sizeof(zeros)) ? sizeof(zeros) : pad;
                    hash = fnv1a_update(hash, zeros, chunk);
                    pad -= chunk;
                }
            }

            sprintf(out_hex, "%08x", hash);
            return 0;
        }
    }

    fprintf(stderr, "hashgen: .text section not found\n");
    fclose(f);
    return -1;
}

int main(int argc, char *argv[])
{
    char file_hash[9];
    char text_hash[9];
    FILE *out;

    if (argc != 3) {
        fprintf(stderr, "Usage: hashgen.exe <ac_client.exe> <output.cfg>\n");
        return 1;
    }

    printf("hashgen: computing hashes for '%s'...\n", argv[1]);

    if (hash_file(argv[1], file_hash) != 0)
        return 1;

    if (hash_text_section(argv[1], text_hash) != 0)
        return 1;

    printf("hashgen: FILE_AC_CLIENT_EXE = %s\n", file_hash);
    printf("hashgen: MEMORY_DOT_TEXT    = %s\n", text_hash);

    out = fopen(argv[2], "w");
    if (!out) {
        fprintf(stderr, "hashgen: cannot write '%s'\n", argv[2]);
        return 1;
    }

    fprintf(out, "# Auto-generated by hashgen -- do not edit manually\n");
    fprintf(out, "# Hash order must match protocol.h identifiers\n");
    fprintf(out, "ac_client_exe=%s\n", file_hash);
    fprintf(out, "memory_dot_text=%s\n", text_hash);
    fclose(out);

    printf("hashgen: wrote '%s'\n", argv[2]);
    return 0;
}
