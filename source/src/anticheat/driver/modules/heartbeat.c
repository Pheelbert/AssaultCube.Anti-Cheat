//
// heartbeat.c - Scan module that reports driver heartbeat / status.
//
// Returns driver version, uptime, and scan count to prove the driver
// is alive and responding to the user-mode client.
//

#include <ntddk.h>
#include "../../ac_shared.h"
#include "../ac_log.h"
#include "../scan_engine.h"

// Defined in driver_main.c
extern LARGE_INTEGER g_DriverLoadTime;

static NTSTATUS AcHeartbeatExecute(AC_TELEMETRY_ENTRY* OutEntry)
{
    AC_HEARTBEAT_DATA* payload;
    LARGE_INTEGER now;

    if (OutEntry == NULL)
        return STATUS_INVALID_PARAMETER;

    if (sizeof(AC_HEARTBEAT_DATA) > AC_MAX_TELEMETRY_DATA)
        return STATUS_BUFFER_TOO_SMALL;

    KeQuerySystemTime(&now);

    OutEntry->Type = AC_TELEMETRY_HEARTBEAT;
    OutEntry->DataLength = sizeof(AC_HEARTBEAT_DATA);

    payload = (AC_HEARTBEAT_DATA*)OutEntry->Data;
    payload->DriverVersion = AC_DRIVER_VERSION;
    payload->UptimeSeconds = (ULONG)((now.QuadPart - g_DriverLoadTime.QuadPart) / 10000000ULL);
    payload->ScanCount = AcScanEngineGetScanCount();

    return STATUS_SUCCESS;
}

NTSTATUS AcRegisterHeartbeatModule(void)
{
    AC_SCAN_MODULE module;

    module.Type = AC_TELEMETRY_HEARTBEAT;
    module.Name = "Heartbeat";
    module.Execute = AcHeartbeatExecute;

    return AcScanEngineRegisterModule(&module);
}
