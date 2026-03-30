#include "scan_engine.h"

// Static storage for registered modules
static AC_SCAN_MODULE g_Modules[AC_MAX_SCAN_MODULES];
static ULONG g_ModuleCount = 0;
static ULONG g_ScanCount = 0;

NTSTATUS AcScanEngineRegisterModule(const AC_SCAN_MODULE* Module)
{
    if (Module == NULL || Module->Execute == NULL)
        return STATUS_INVALID_PARAMETER;

    if (g_ModuleCount >= AC_MAX_SCAN_MODULES)
        return STATUS_INSUFFICIENT_RESOURCES;

    g_Modules[g_ModuleCount] = *Module;
    g_ModuleCount++;

    DbgPrint("[PhantiCheat] Registered scan module: %s (type=%lu)\n",
             Module->Name ? Module->Name : "unnamed", Module->Type);

    return STATUS_SUCCESS;
}

NTSTATUS AcScanEngineExecuteAll(AC_TELEMETRY_RESPONSE* Response)
{
    ULONG i;
    NTSTATUS status;

    if (Response == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Response, sizeof(AC_TELEMETRY_RESPONSE));
    Response->EntryCount = 0;

    for (i = 0; i < g_ModuleCount && Response->EntryCount < AC_MAX_TELEMETRY_ENTRIES; i++)
    {
        AC_TELEMETRY_ENTRY* entry = &Response->Entries[Response->EntryCount];
        RtlZeroMemory(entry, sizeof(AC_TELEMETRY_ENTRY));

        status = g_Modules[i].Execute(entry);
        if (NT_SUCCESS(status))
        {
            // Module filled the entry successfully
            Response->EntryCount++;
        }
        else
        {
            DbgPrint("[PhantiCheat] Module '%s' failed with status 0x%08X\n",
                     g_Modules[i].Name ? g_Modules[i].Name : "unnamed", status);
        }
    }

    g_ScanCount++;
    return STATUS_SUCCESS;
}

ULONG AcScanEngineGetModuleCount(void)
{
    return g_ModuleCount;
}

ULONG AcScanEngineGetScanCount(void)
{
    return g_ScanCount;
}

NTSTATUS AcScanEngineInit(void)
{
    NTSTATUS status;

    g_ModuleCount = 0;
    g_ScanCount = 0;
    RtlZeroMemory(g_Modules, sizeof(g_Modules));

    DbgPrint("[PhantiCheat] Initializing scan engine...\n");

    status = AcRegisterWindowsVersionModule();
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[PhantiCheat] Failed to register WindowsVersion module: 0x%08X\n", status);
    }

    status = AcRegisterHeartbeatModule();
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[PhantiCheat] Failed to register Heartbeat module: 0x%08X\n", status);
    }

    status = AcRegisterInputMonitorModule();
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[PhantiCheat] Failed to register InputMonitor module: 0x%08X\n", status);
    }

    DbgPrint("[PhantiCheat] Scan engine initialized with %lu modules.\n", g_ModuleCount);
    return STATUS_SUCCESS;
}
