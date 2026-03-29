//
// windows_version.c - Scan module that reports the Windows version.
//
// Uses RtlGetVersion() (kernel-mode safe, not deprecated like GetVersionEx)
// to collect the OS major/minor/build and service pack string.
//

#include <ntddk.h>
#include "../../ac_shared.h"
#include "../scan_engine.h"

static NTSTATUS AcWindowsVersionExecute(AC_TELEMETRY_ENTRY* OutEntry)
{
    RTL_OSVERSIONINFOW versionInfo;
    AC_WINDOWS_VERSION_DATA* payload;
    NTSTATUS status;

    if (OutEntry == NULL)
        return STATUS_INVALID_PARAMETER;

    // Ensure payload fits in the entry data buffer
    if (sizeof(AC_WINDOWS_VERSION_DATA) > AC_MAX_TELEMETRY_DATA)
        return STATUS_BUFFER_TOO_SMALL;

    RtlZeroMemory(&versionInfo, sizeof(versionInfo));
    versionInfo.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOW);

    status = RtlGetVersion(&versionInfo);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[PhantiCheat] RtlGetVersion failed: 0x%08X\n", status);
        return status;
    }

    OutEntry->Type = AC_TELEMETRY_WINDOWS_VERSION;
    OutEntry->DataLength = sizeof(AC_WINDOWS_VERSION_DATA);

    payload = (AC_WINDOWS_VERSION_DATA*)OutEntry->Data;
    payload->MajorVersion = versionInfo.dwMajorVersion;
    payload->MinorVersion = versionInfo.dwMinorVersion;
    payload->BuildNumber = versionInfo.dwBuildNumber;

    // Copy service pack string (truncate if needed)
    RtlZeroMemory(payload->CSDVersion, sizeof(payload->CSDVersion));
    RtlCopyMemory(
        payload->CSDVersion,
        versionInfo.szCSDVersion,
        min(sizeof(payload->CSDVersion) - sizeof(WCHAR), sizeof(versionInfo.szCSDVersion))
    );

    DbgPrint("[PhantiCheat] Windows version: %lu.%lu.%lu\n",
             payload->MajorVersion, payload->MinorVersion, payload->BuildNumber);

    return STATUS_SUCCESS;
}

NTSTATUS AcRegisterWindowsVersionModule(void)
{
    AC_SCAN_MODULE module;

    module.Type = AC_TELEMETRY_WINDOWS_VERSION;
    module.Name = "WindowsVersion";
    module.Execute = AcWindowsVersionExecute;

    return AcScanEngineRegisterModule(&module);
}
