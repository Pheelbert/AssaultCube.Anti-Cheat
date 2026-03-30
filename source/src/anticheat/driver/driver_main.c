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
#include "ac_log.h"
#include "scan_engine.h"
#include "modules/input_monitor.h"

// Track driver load time for uptime calculation (non-static: referenced by heartbeat module)
LARGE_INTEGER g_DriverLoadTime;

// Client process tracking: detach filters when the client exits (even on crash).
// Windows sends IRP_MJ_CLEANUP for all open handles during process teardown.
static HANDLE g_ClientProcessId = NULL;

// Forward declarations
static NTSTATUS AcDispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static NTSTATUS AcDispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static NTSTATUS AcDispatchCleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp);
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

    KeQuerySystemTime(&g_DriverLoadTime);

    // Create device first, then init logging (need device to exist)
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

    // Initialize file logging (after device creation so we're past early failures)
    AcLogInit();
    AcLogWrite("DriverEntry called. DeviceObject=%p", deviceObject);

    // Filter driver MUST pass through ALL IRP types it doesn't explicitly handle.
    // The default handler (IopInvalidDeviceRequest) completes IRPs with an error,
    // which breaks the device stack for PnP, Power, WMI, etc.
    {
        ULONG i;
        for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
            DriverObject->MajorFunction[i] = AcDispatchPassthrough;
    }
    AcLogWrite("MajorFunction[0..%d] set to passthrough", IRP_MJ_MAXIMUM_FUNCTION);

    // Override specific handlers
    DriverObject->MajorFunction[IRP_MJ_CREATE] = AcDispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = AcDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = AcDispatchCleanup;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AcDispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_READ] = AcDispatchRead;
    DriverObject->DriverUnload = AcDriverUnload;
    AcLogWrite("Specific dispatch handlers registered");

    // Initialize scan engine (registers built-in modules)
    status = AcScanEngineInit();
    if (!NT_SUCCESS(status))
    {
        AcLogWrite("Scan engine init failed: 0x%08X", status);
        // Non-fatal: driver still loads, just no modules
    }
    else
    {
        AcLogWrite("Scan engine initialized OK");
    }

    // Attach input monitor filters to keyboard/mouse device stacks
    AcLogWrite("Attaching input monitor filters...");
    status = AcInputMonitorAttach(DriverObject);
    if (!NT_SUCCESS(status))
    {
        AcLogWrite("Input monitor attach failed: 0x%08X", status);
        // Non-fatal: driver still works, just no kernel-level input monitoring
    }
    else
    {
        AcLogWrite("Input monitor attach returned OK. KbdFilter=%p MouseFilter=%p",
                   g_KeyboardFilterDevice, g_MouseFilterDevice);
    }

    AcLogWrite("Driver loaded successfully.");
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Driver unload
// ---------------------------------------------------------------------------
static void AcDriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING symlinkName;

    AcLogWrite("Driver unloading...");

    AcLogWrite("Detaching input monitor filters...");
    AcInputMonitorDetach();
    AcLogWrite("Input monitor detached.");

    // Belt-and-suspenders: flush DPCs again before deleting the control
    // device and allowing the driver image to be unloaded.
    KeFlushQueuedDpcs();

    RtlInitUnicodeString(&symlinkName, AC_SYMLINK_NAME);
    IoDeleteSymbolicLink(&symlinkName);
    AcLogWrite("Symbolic link deleted.");

    if (DriverObject->DeviceObject)
    {
        IoDeleteDevice(DriverObject->DeviceObject);
        AcLogWrite("Device object deleted.");
    }

    AcLogWrite("Driver unloaded.");
    AcLogClose();
}

// ---------------------------------------------------------------------------
// IRP_MJ_READ - route to input monitor for filter devices, reject for control
// ---------------------------------------------------------------------------
static NTSTATUS AcDispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    if (AcIsFilterDevice(DeviceObject))
        return AcInputMonitorDispatchRead(DeviceObject, Irp);

    AcLogWrite("READ on control device (unexpected). DevObj=%p", DeviceObject);
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
    BOOLEAN isFilter = AcIsFilterDevice(DeviceObject);

    if (isFilter)
        return AcInputMonitorDispatchPassthrough(DeviceObject, Irp);

    // Control device: reject unknown IRP types
    {
        PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
        AcLogWrite("ControlPassthrough: DevObj=%p MJ=0x%X (rejected)", DeviceObject, (ULONG)irpSp->MajorFunction);
    }
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
    {
        AcLogWrite("CREATE on filter device: DevObj=%p", DeviceObject);
        return AcInputMonitorDispatchPassthrough(DeviceObject, Irp);
    }

    // Track the client process so we can clean up if it crashes
    g_ClientProcessId = PsGetCurrentProcessId();
    AcLogWrite("Device opened (control device). ClientPID=%lu", (ULONG)(ULONG_PTR)g_ClientProcessId);

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
    {
        AcLogWrite("CLOSE on filter device: DevObj=%p", DeviceObject);
        return AcInputMonitorDispatchPassthrough(DeviceObject, Irp);
    }

    AcLogWrite("Device closed (control device).");

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// IRP_MJ_CLEANUP - sent when a handle is being closed, including on process
// crash. This is our signal to detach filters and go inert when the client
// exits unexpectedly. Windows guarantees CLEANUP is sent even if the process
// is killed or crashes.
// ---------------------------------------------------------------------------
static NTSTATUS AcDispatchCleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    if (AcIsFilterDevice(DeviceObject))
    {
        AcLogWrite("CLEANUP on filter device: DevObj=%p", DeviceObject);
        return AcInputMonitorDispatchPassthrough(DeviceObject, Irp);
    }

    // Control device cleanup: our client is exiting (or crashed)
    AcLogWrite("CLEANUP on control device. ClientPID=%lu CurrentPID=%lu -- detaching filters.",
               (ULONG)(ULONG_PTR)g_ClientProcessId,
               (ULONG)(ULONG_PTR)PsGetCurrentProcessId());

    // Detach input filters so the keyboard/mouse stacks are clean.
    // The driver module stays loaded but is inert until the service is stopped.
    AcInputMonitorDetach();
    g_ClientProcessId = NULL;

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
    {
        AcLogWrite("DEVCTL on filter device: DevObj=%p (passthrough)", DeviceObject);
        return AcInputMonitorDispatchPassthrough(DeviceObject, Irp);
    }

    irpSp = IoGetCurrentIrpStackLocation(Irp);
    ioControlCode = irpSp->Parameters.DeviceIoControl.IoControlCode;
    outputLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
    outputBuffer = Irp->AssociatedIrp.SystemBuffer;

    AcLogWrite("IOCTL: code=0x%08X outLen=%lu outBuf=%p", ioControlCode, outputLength, outputBuffer);

    switch (ioControlCode)
    {
    case IOCTL_AC_GET_STATUS:
    {
        AC_DRIVER_STATUS driverStatus;

        if (outputLength < sizeof(AC_DRIVER_STATUS) || outputBuffer == NULL)
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

        if (outputLength < sizeof(AC_TELEMETRY_RESPONSE) || outputBuffer == NULL)
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
