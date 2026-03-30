//
// driver_main.c - PhantiCheat kernel driver entry point and IOCTL dispatch.
//
// This is a minimal WDM driver that creates a device object, handles
// user-mode communication via IOCTLs, and delegates scanning to the
// modular scan engine.
//
// Build with WDK. Load with test signing enabled:
//   bcdedit /set testsigning on
//   sc create phanticheat type=kernel binPath=C:\path\to\phanticheat.sys
//   sc start phanticheat
//

#include <ntddk.h>
#include "../ac_shared.h"
#include "scan_engine.h"
#include "modules/input_monitor.h"

// Track driver load time for uptime calculation (non-static: referenced by heartbeat module)
LARGE_INTEGER g_DriverLoadTime;

// Forward declarations
static NTSTATUS AcDispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static NTSTATUS AcDispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static NTSTATUS AcDispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static NTSTATUS AcDispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static NTSTATUS AcDispatchPassthrough(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static void AcDriverUnload(PDRIVER_OBJECT DriverObject);

// Helper: check if this device object is one of our input filter devices
static BOOLEAN AcIsFilterDevice(PDEVICE_OBJECT DeviceObject)
{
    return (DeviceObject == g_KeyboardFilterDevice ||
            DeviceObject == g_MouseFilterDevice);
}

// Helper: get uptime in seconds since driver load
static ULONG AcGetUptimeSeconds(void)
{
    LARGE_INTEGER now;
    KeQuerySystemTime(&now);
    // 100-nanosecond intervals to seconds
    return (ULONG)((now.QuadPart - g_DriverLoadTime.QuadPart) / 10000000ULL);
}

// ---------------------------------------------------------------------------
// DriverEntry
// ---------------------------------------------------------------------------
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    NTSTATUS status;
    PDEVICE_OBJECT deviceObject = NULL;
    UNICODE_STRING deviceName;
    UNICODE_STRING symlinkName;

    UNREFERENCED_PARAMETER(RegistryPath);

    DbgPrint("[PhantiCheat] DriverEntry called.\n");

    KeQuerySystemTime(&g_DriverLoadTime);

    // Create device
    RtlInitUnicodeString(&deviceName, AC_DEVICE_NAME);
    status = IoCreateDevice(
        DriverObject,
        0,                          // DeviceExtensionSize
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,                      // Exclusive
        &deviceObject
    );

    if (!NT_SUCCESS(status))
    {
        DbgPrint("[PhantiCheat] IoCreateDevice failed: 0x%08X\n", status);
        return status;
    }

    // Create symbolic link for user-mode access
    RtlInitUnicodeString(&symlinkName, AC_SYMLINK_NAME);
    status = IoCreateSymbolicLink(&symlinkName, &deviceName);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[PhantiCheat] IoCreateSymbolicLink failed: 0x%08X\n", status);
        IoDeleteDevice(deviceObject);
        return status;
    }

    // Set up dispatch routines
    DriverObject->MajorFunction[IRP_MJ_CREATE] = AcDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = AcDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AcDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_READ] = AcDispatchRead;
    // Internal device control is used by class drivers for keyboard/mouse data
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = AcDispatchPassthrough;
    DriverObject->DriverUnload = AcDriverUnload;

    // Initialize scan engine (registers built-in modules)
    status = AcScanEngineInit();
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[PhantiCheat] Scan engine init failed: 0x%08X\n", status);
        // Non-fatal: driver still loads, just no modules
    }

    // Attach input monitor filters to keyboard/mouse device stacks
    status = AcInputMonitorAttach(DriverObject);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[PhantiCheat] Input monitor attach failed: 0x%08X\n", status);
        // Non-fatal: driver still works, just no kernel-level input monitoring
    }

    DbgPrint("[PhantiCheat] Driver loaded successfully.\n");
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Driver unload
// ---------------------------------------------------------------------------
static void AcDriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING symlinkName;

    DbgPrint("[PhantiCheat] Driver unloading.\n");

    // Detach input monitor filters before deleting our control device
    AcInputMonitorDetach();

    RtlInitUnicodeString(&symlinkName, AC_SYMLINK_NAME);
    IoDeleteSymbolicLink(&symlinkName);

    if (DriverObject->DeviceObject)
    {
        IoDeleteDevice(DriverObject->DeviceObject);
    }

    DbgPrint("[PhantiCheat] Driver unloaded.\n");
}

// ---------------------------------------------------------------------------
// IRP_MJ_READ - route to input monitor for filter devices, reject for control
// ---------------------------------------------------------------------------
static NTSTATUS AcDispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    if (AcIsFilterDevice(DeviceObject))
        return AcInputMonitorDispatchRead(DeviceObject, Irp);

    // Our control device doesn't support reads
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

// ---------------------------------------------------------------------------
// Passthrough for IRPs on filter devices (INTERNAL_DEVICE_CONTROL etc.)
// ---------------------------------------------------------------------------
static NTSTATUS AcDispatchPassthrough(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    if (AcIsFilterDevice(DeviceObject))
        return AcInputMonitorDispatchPassthrough(DeviceObject, Irp);

    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

// ---------------------------------------------------------------------------
// IRP_MJ_CREATE - called when user-mode opens the device handle
// ---------------------------------------------------------------------------
static NTSTATUS AcDispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    // If this is a filter device, pass through
    if (AcIsFilterDevice(DeviceObject))
        return AcInputMonitorDispatchPassthrough(DeviceObject, Irp);

    DbgPrint("[PhantiCheat] Device opened.\n");

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// IRP_MJ_CLOSE - called when user-mode closes the device handle
// ---------------------------------------------------------------------------
static NTSTATUS AcDispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    if (AcIsFilterDevice(DeviceObject))
        return AcInputMonitorDispatchPassthrough(DeviceObject, Irp);

    DbgPrint("[PhantiCheat] Device closed.\n");

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// IRP_MJ_DEVICE_CONTROL - IOCTL dispatch
// ---------------------------------------------------------------------------
static NTSTATUS AcDispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION irpSp;
    ULONG ioControlCode;
    ULONG outputLength;
    PVOID outputBuffer;
    NTSTATUS status = STATUS_SUCCESS;
    ULONG bytesWritten = 0;

    // Filter devices don't handle our IOCTLs - pass through
    if (AcIsFilterDevice(DeviceObject))
        return AcInputMonitorDispatchPassthrough(DeviceObject, Irp);

    irpSp = IoGetCurrentIrpStackLocation(Irp);
    ioControlCode = irpSp->Parameters.DeviceIoControl.IoControlCode;
    outputLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
    outputBuffer = Irp->AssociatedIrp.SystemBuffer;

    switch (ioControlCode)
    {
    case IOCTL_AC_GET_STATUS:
    {
        AC_DRIVER_STATUS driverStatus;

        if (outputLength < sizeof(AC_DRIVER_STATUS))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        driverStatus.Version = AC_DRIVER_VERSION;
        driverStatus.ModuleCount = AcScanEngineGetModuleCount();
        driverStatus.ScanCount = AcScanEngineGetScanCount();
        driverStatus.UptimeSeconds = AcGetUptimeSeconds();

        RtlCopyMemory(outputBuffer, &driverStatus, sizeof(AC_DRIVER_STATUS));
        bytesWritten = sizeof(AC_DRIVER_STATUS);
        status = STATUS_SUCCESS;
        break;
    }

    case IOCTL_AC_GET_TELEMETRY:
    {
        AC_TELEMETRY_RESPONSE response;

        if (outputLength < sizeof(AC_TELEMETRY_RESPONSE))
        {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        status = AcScanEngineExecuteAll(&response);
        if (NT_SUCCESS(status))
        {
            RtlCopyMemory(outputBuffer, &response, sizeof(AC_TELEMETRY_RESPONSE));
            bytesWritten = sizeof(AC_TELEMETRY_RESPONSE);
        }
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = bytesWritten;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}
