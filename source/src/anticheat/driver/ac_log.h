#ifndef AC_LOG_H
#define AC_LOG_H

//
// ac_log.h - Kernel-mode file logging with immediate flush.
//
// Writes to C:\phanticheat.log. Every write is flushed to disk immediately
// so that logs survive a BSOD. File I/O is only performed at PASSIVE_LEVEL;
// at higher IRQLs the message is sent to DbgPrint only.
//

#include <ntddk.h>

// Initialize the log file. Call from DriverEntry.
NTSTATUS AcLogInit(void);

// Close the log file. Call from DriverUnload.
void AcLogClose(void);

// Write a formatted log message. Always DbgPrints; also writes to file
// if called at PASSIVE_LEVEL and the log file is open.
void AcLogWrite(const char* format, ...);

#endif // AC_LOG_H
