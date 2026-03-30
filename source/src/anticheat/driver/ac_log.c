//
// ac_log.c - Kernel-mode file logging with immediate flush to survive BSOD.
//
// Log file: C:\phanticheat.log
// - FILE_WRITE_THROUGH ensures data hits disk without OS caching
// - ZwFlushBuffersFile after every write for extra BSOD safety
// - FAST_MUTEX serializes concurrent writes at PASSIVE_LEVEL
// - At DISPATCH_LEVEL or above, only DbgPrint is used (no file I/O)
//

#include <ntddk.h>
#include <ntstrsafe.h>
#include "ac_log.h"

// ZwFlushBuffersFile is not declared in ntddk.h (it's in ntifs.h),
// but it's exported by ntoskrnl.lib. Declare it manually.
NTSYSCALLAPI
NTSTATUS
NTAPI
ZwFlushBuffersFile(
    HANDLE FileHandle,
    PIO_STATUS_BLOCK IoStatusBlock
);

// Log file path in NT namespace
#define AC_LOG_FILE_PATH  L"\\??\\C:\\phanticheat.log"

// Maximum size of a single formatted log line
#define AC_LOG_BUFFER_SIZE  512

static HANDLE g_LogHandle = NULL;
static KMUTEX g_LogMutex;
static BOOLEAN g_LogInitialized = FALSE;

NTSTATUS AcLogInit(void)
{
    NTSTATUS status;
    UNICODE_STRING filePath;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;

    KeInitializeMutex(&g_LogMutex, 0);

    RtlInitUnicodeString(&filePath, AC_LOG_FILE_PATH);
    InitializeObjectAttributes(
        &objAttr,
        &filePath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL
    );

    status = ZwCreateFile(
        &g_LogHandle,
        FILE_APPEND_DATA | SYNCHRONIZE,
        &objAttr,
        &ioStatus,
        NULL,                           // AllocationSize
        FILE_ATTRIBUTE_NORMAL,
        FILE_SHARE_READ,                // Allow reading while we write
        FILE_OVERWRITE_IF,              // Create or truncate (fresh log each load)
        FILE_NON_DIRECTORY_FILE |
        FILE_WRITE_THROUGH |            // Bypass OS write cache
        FILE_SYNCHRONOUS_IO_NONALERT,   // Synchronous I/O
        NULL,
        0
    );

    if (!NT_SUCCESS(status))
    {
        g_LogHandle = NULL;
        DbgPrint("[PhantiCheat:Log] Failed to open log file: 0x%08X\n", status);
        return status;
    }

    g_LogInitialized = TRUE;
    DbgPrint("[PhantiCheat:Log] Log file opened: C:\\phanticheat.log\n");

    // Write header
    AcLogWrite("=== PhantiCheat driver log started ===");

    return STATUS_SUCCESS;
}

void AcLogClose(void)
{
    if (g_LogHandle)
    {
        AcLogWrite("=== PhantiCheat driver log closing ===");
        ZwClose(g_LogHandle);
        g_LogHandle = NULL;
    }
    g_LogInitialized = FALSE;
}

void AcLogWrite(const char* format, ...)
{
    va_list args;
    char buffer[AC_LOG_BUFFER_SIZE];
    char finalBuffer[AC_LOG_BUFFER_SIZE + 64]; // Extra room for timestamp prefix
    NTSTATUS status;
    LARGE_INTEGER systemTime, localTime;
    TIME_FIELDS timeFields;
    size_t len;
    IO_STATUS_BLOCK ioStatus;

    // Format the user message
    va_start(args, format);
    status = RtlStringCbVPrintfA(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (!NT_SUCCESS(status))
    {
        // Truncated or error - still try to output what we have
        buffer[AC_LOG_BUFFER_SIZE - 1] = '\0';
    }

    // Always DbgPrint regardless of IRQL
    DbgPrint("[PhantiCheat] %s\n", buffer);

    // File I/O requires PASSIVE_LEVEL
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return;

    if (!g_LogInitialized || g_LogHandle == NULL)
        return;

    // Build timestamped line
    KeQuerySystemTime(&systemTime);
    ExSystemTimeToLocalTime(&systemTime, &localTime);
    RtlTimeToTimeFields(&localTime, &timeFields);

    status = RtlStringCbPrintfA(
        finalBuffer, sizeof(finalBuffer),
        "[%04d-%02d-%02d %02d:%02d:%02d.%03d] %s\r\n",
        timeFields.Year, timeFields.Month, timeFields.Day,
        timeFields.Hour, timeFields.Minute, timeFields.Second,
        timeFields.Milliseconds,
        buffer
    );

    if (!NT_SUCCESS(status))
        return;

    // Get the length of the formatted string
    status = RtlStringCbLengthA(finalBuffer, sizeof(finalBuffer), &len);
    if (!NT_SUCCESS(status) || len == 0)
        return;

    // Serialize writes (KMUTEX stays at PASSIVE_LEVEL, unlike FAST_MUTEX
    // which raises to APC_LEVEL and deadlocks ZwWriteFile/ZwFlushBuffersFile)
    KeWaitForSingleObject(&g_LogMutex, Executive, KernelMode, FALSE, NULL);

    // Write to file
    ZwWriteFile(
        g_LogHandle,
        NULL,           // Event
        NULL,           // ApcRoutine
        NULL,           // ApcContext
        &ioStatus,
        finalBuffer,
        (ULONG)len,
        NULL,           // ByteOffset (append mode)
        NULL            // Key
    );

    // Flush immediately - ensures data survives BSOD
    ZwFlushBuffersFile(g_LogHandle, &ioStatus);

    KeReleaseMutex(&g_LogMutex, FALSE);
}
