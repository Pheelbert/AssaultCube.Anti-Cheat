#ifndef SCAN_ENGINE_H
#define SCAN_ENGINE_H

#include <ntddk.h>
#include "../ac_shared.h"

// Maximum number of scan modules that can be registered
#define AC_MAX_SCAN_MODULES 32

// Function pointer type for a scan module's execute routine.
// The module fills in the provided telemetry entry and returns STATUS_SUCCESS,
// or a failure status to indicate the entry should be skipped.
typedef NTSTATUS (*AC_SCAN_MODULE_EXECUTE)(AC_TELEMETRY_ENTRY* OutEntry);

// Describes a single scan module
typedef struct _AC_SCAN_MODULE {
    ULONG Type;                         // AC_TELEMETRY_* type this module produces
    const char* Name;                   // Human-readable name for logging
    AC_SCAN_MODULE_EXECUTE Execute;     // Callback to produce one telemetry entry
} AC_SCAN_MODULE;

// Initialize the scan engine and register all built-in modules.
NTSTATUS AcScanEngineInit(void);

// Register an individual scan module. Returns STATUS_INSUFFICIENT_RESOURCES if full.
NTSTATUS AcScanEngineRegisterModule(const AC_SCAN_MODULE* Module);

// Execute all registered modules and fill the response buffer.
NTSTATUS AcScanEngineExecuteAll(AC_TELEMETRY_RESPONSE* Response);

// Return the number of registered modules.
ULONG AcScanEngineGetModuleCount(void);

// Return the total number of scans (ExecuteAll calls) since init.
ULONG AcScanEngineGetScanCount(void);

// Built-in module registration functions (called by AcScanEngineInit)
NTSTATUS AcRegisterWindowsVersionModule(void);
NTSTATUS AcRegisterHeartbeatModule(void);
NTSTATUS AcRegisterInputMonitorModule(void);

#endif // SCAN_ENGINE_H
