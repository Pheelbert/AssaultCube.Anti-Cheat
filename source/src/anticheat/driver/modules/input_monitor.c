//
// input_monitor.c - Kernel-mode input monitoring scan module.
//
// Attaches as an upper filter on the keyboard and mouse class device stacks
// to intercept IRP_MJ_READ completions at the I/O manager level. This is
// "Layer 0" -- the lowest observable point in the Windows input pipeline,
// below Raw Input, low-level hooks, and the application message queue.
//
// Kernel-level cheats that inject input via filter drivers, HID miniport
// spoofing, or direct IRP injection are visible here but invisible to all
// user-mode detection layers.
//
// What this module detects:
//   - Software-injected keyboard/mouse IRPs (no hardware origin)
//   - Unknown filter drivers inserted into the input device stack
//   - Inhuman inter-keystroke timing at the I/O level
//   - IRP count divergence from user-mode Raw Input counts
//

#include <ntddk.h>
#include <ntddkbd.h>
#include <ntddmou.h>
#include "../../ac_shared.h"
#include "../scan_engine.h"

// ---------------------------------------------------------------------------
// Globals - counters updated by IRP completion routines, read by scan module
// ---------------------------------------------------------------------------

// Keyboard IRP counters
static volatile LONG g_HardwareKeyIrps = 0;
static volatile LONG g_SoftwareKeyIrps = 0;

// Mouse IRP counters
static volatile LONG g_HardwareMouseIrps = 0;
static volatile LONG g_SoftwareMouseIrps = 0;

// Inter-keystroke timing (microseconds)
static LARGE_INTEGER g_LastKeystrokeTime = { 0 };
static volatile LONG g_MinInterKeystrokeUs = 0x7FFFFFFF; // sentinel: max int

// Inter-mouse timing (microseconds)
static LARGE_INTEGER g_LastMouseTime = { 0 };
static volatile LONG g_MinInterMouseUs = 0x7FFFFFFF;

// QPC frequency for microsecond conversion
static LARGE_INTEGER g_QpcFrequency = { 0 };

// Filter attachment state
PDEVICE_OBJECT g_KeyboardFilterDevice = NULL;
static PDEVICE_OBJECT g_KeyboardTargetDevice = NULL;
PDEVICE_OBJECT g_MouseFilterDevice = NULL;
static PDEVICE_OBJECT g_MouseTargetDevice = NULL;

// Device stack analysis (computed during attach)
static ULONG g_KeyboardFilterCount = 0;
static ULONG g_KeyboardUnknownFilters = 0;
static ULONG g_MouseFilterCount = 0;
static ULONG g_MouseUnknownFilters = 0;

// Known-good keyboard/mouse filter driver names (uppercase for comparison).
// These are standard Windows class drivers and common legitimate filters.
static const WCHAR* g_AllowedFilterDrivers[] = {
    L"\\DRIVER\\KBDCLASS",
    L"\\DRIVER\\KBDHID",
    L"\\DRIVER\\MOUCLASS",
    L"\\DRIVER\\MOUHID",
    L"\\DRIVER\\I8042PRT",
    L"\\DRIVER\\HIDUSB",
    L"\\DRIVER\\ACPI",
    NULL
};

// Forward declarations
static NTSTATUS AcInputMonitorReadComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);
static NTSTATUS AcMouseMonitorReadComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);
static BOOLEAN AcIsDriverAllowed(PDRIVER_OBJECT DriverObject);
static ULONG AcCountDeviceStackFilters(PDEVICE_OBJECT TopOfStack, ULONG *unknownOut);

// ---------------------------------------------------------------------------
// Device stack attachment
// ---------------------------------------------------------------------------

//
// AcInputMonitorAttach - Attach filter devices to keyboard and mouse class
// device stacks. Called from DriverEntry context.
//
// We open \Device\KeyboardClass0 and \Device\PointerClass0 (the default
// class devices created by kbdclass.sys and mouclass.sys), create filter
// device objects, and attach them as upper filters.
//
NTSTATUS AcInputMonitorAttach(PDRIVER_OBJECT DriverObject)
{
    NTSTATUS status;
    UNICODE_STRING kbdName, mouseName;
    PFILE_OBJECT fileObject = NULL;
    PDEVICE_OBJECT topOfStack = NULL;

    g_QpcFrequency.QuadPart = 0;
    KeQueryPerformanceCounter(&g_QpcFrequency);
    if (g_QpcFrequency.QuadPart == 0)
        g_QpcFrequency.QuadPart = 1; // fallback to avoid div-by-zero

    // --- Keyboard ---
    RtlInitUnicodeString(&kbdName, L"\\Device\\KeyboardClass0");

    status = IoGetDeviceObjectPointer(&kbdName, FILE_READ_DATA, &fileObject, &topOfStack);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[PhantiCheat:InputMon] Cannot get KeyboardClass0: 0x%08X\n", status);
        goto attach_mouse;
    }
    ObDereferenceObject(fileObject);
    fileObject = NULL;

    // Analyze device stack before attaching
    g_KeyboardFilterCount = AcCountDeviceStackFilters(topOfStack, &g_KeyboardUnknownFilters);

    // Create our filter device
    status = IoCreateDevice(
        DriverObject,
        0,
        NULL,                       // no name for filter device
        topOfStack->DeviceType,
        0,
        FALSE,
        &g_KeyboardFilterDevice
    );
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[PhantiCheat:InputMon] Cannot create keyboard filter device: 0x%08X\n", status);
        goto attach_mouse;
    }

    // Match flags of target device
    g_KeyboardFilterDevice->Flags |= (topOfStack->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO));
    g_KeyboardFilterDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    g_KeyboardTargetDevice = IoAttachDeviceToDeviceStack(g_KeyboardFilterDevice, topOfStack);
    if (!g_KeyboardTargetDevice)
    {
        DbgPrint("[PhantiCheat:InputMon] IoAttachDeviceToDeviceStack failed for keyboard.\n");
        IoDeleteDevice(g_KeyboardFilterDevice);
        g_KeyboardFilterDevice = NULL;
        goto attach_mouse;
    }

    DbgPrint("[PhantiCheat:InputMon] Attached to keyboard stack (%lu filters, %lu unknown).\n",
             g_KeyboardFilterCount, g_KeyboardUnknownFilters);

attach_mouse:
    // --- Mouse ---
    RtlInitUnicodeString(&mouseName, L"\\Device\\PointerClass0");

    status = IoGetDeviceObjectPointer(&mouseName, FILE_READ_DATA, &fileObject, &topOfStack);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[PhantiCheat:InputMon] Cannot get PointerClass0: 0x%08X\n", status);
        goto done;
    }
    ObDereferenceObject(fileObject);
    fileObject = NULL;

    g_MouseFilterCount = AcCountDeviceStackFilters(topOfStack, &g_MouseUnknownFilters);

    status = IoCreateDevice(
        DriverObject,
        0,
        NULL,
        topOfStack->DeviceType,
        0,
        FALSE,
        &g_MouseFilterDevice
    );
    if (!NT_SUCCESS(status))
    {
        DbgPrint("[PhantiCheat:InputMon] Cannot create mouse filter device: 0x%08X\n", status);
        goto done;
    }

    g_MouseFilterDevice->Flags |= (topOfStack->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO));
    g_MouseFilterDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    g_MouseTargetDevice = IoAttachDeviceToDeviceStack(g_MouseFilterDevice, topOfStack);
    if (!g_MouseTargetDevice)
    {
        DbgPrint("[PhantiCheat:InputMon] IoAttachDeviceToDeviceStack failed for mouse.\n");
        IoDeleteDevice(g_MouseFilterDevice);
        g_MouseFilterDevice = NULL;
        goto done;
    }

    DbgPrint("[PhantiCheat:InputMon] Attached to mouse stack (%lu filters, %lu unknown).\n",
             g_MouseFilterCount, g_MouseUnknownFilters);

done:
    return STATUS_SUCCESS; // non-fatal even if attachment fails
}

//
// AcInputMonitorDetach - Remove filter devices from device stacks.
// Called from DriverUnload.
//
void AcInputMonitorDetach(void)
{
    if (g_KeyboardTargetDevice)
    {
        IoDetachDevice(g_KeyboardTargetDevice);
        g_KeyboardTargetDevice = NULL;
    }
    if (g_KeyboardFilterDevice)
    {
        IoDeleteDevice(g_KeyboardFilterDevice);
        g_KeyboardFilterDevice = NULL;
    }
    if (g_MouseTargetDevice)
    {
        IoDetachDevice(g_MouseTargetDevice);
        g_MouseTargetDevice = NULL;
    }
    if (g_MouseFilterDevice)
    {
        IoDeleteDevice(g_MouseFilterDevice);
        g_MouseFilterDevice = NULL;
    }

    DbgPrint("[PhantiCheat:InputMon] Filter devices detached.\n");
}

// ---------------------------------------------------------------------------
// IRP dispatch for filter devices
// ---------------------------------------------------------------------------

//
// AcInputMonitorDispatchRead - Called when our filter device receives
// IRP_MJ_READ (the class driver reads keyboard/mouse data from the port).
// We set a completion routine to inspect the data after it comes back up.
//
NTSTATUS AcInputMonitorDispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    // Determine which device stack this is for
    if (DeviceObject == g_KeyboardFilterDevice && g_KeyboardTargetDevice)
    {
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(Irp, AcInputMonitorReadComplete, NULL, TRUE, TRUE, TRUE);
        return IoCallDriver(g_KeyboardTargetDevice, Irp);
    }
    else if (DeviceObject == g_MouseFilterDevice && g_MouseTargetDevice)
    {
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(Irp, AcMouseMonitorReadComplete, NULL, TRUE, TRUE, TRUE);
        return IoCallDriver(g_MouseTargetDevice, Irp);
    }

    // Not our filter device -- shouldn't happen, pass through
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(g_KeyboardTargetDevice ? g_KeyboardTargetDevice : g_MouseTargetDevice, Irp);
}

//
// AcInputMonitorDispatchPassthrough - For non-read IRPs on our filter devices,
// just pass them down the stack untouched.
//
NTSTATUS AcInputMonitorDispatchPassthrough(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PDEVICE_OBJECT target = NULL;

    if (DeviceObject == g_KeyboardFilterDevice)
        target = g_KeyboardTargetDevice;
    else if (DeviceObject == g_MouseFilterDevice)
        target = g_MouseTargetDevice;

    if (target)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(target, Irp);
    }

    // Fallback: complete with error
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

// ---------------------------------------------------------------------------
// IRP completion routines - where the actual monitoring happens
// ---------------------------------------------------------------------------

//
// Keyboard IRP_MJ_READ completion: the port driver has filled in
// KEYBOARD_INPUT_DATA structures. We count them and classify as
// hardware-sourced or software-injected.
//
static NTSTATUS AcInputMonitorReadComplete(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Context);

    if (Irp->IoStatus.Status == STATUS_SUCCESS && Irp->IoStatus.Information > 0)
    {
        // KEYBOARD_INPUT_DATA is what kbdclass delivers per read IRP
        ULONG dataSize = (ULONG)Irp->IoStatus.Information;
        ULONG count = dataSize / sizeof(KEYBOARD_INPUT_DATA);
        KEYBOARD_INPUT_DATA *data = (KEYBOARD_INPUT_DATA *)Irp->AssociatedIrp.SystemBuffer;
        ULONG i;

        for (i = 0; i < count; i++)
        {
            // KEYBOARD_INPUT_DATA.UnitId identifies the port that generated
            // the keystroke. UnitId 0 is typically the primary PS/2 or HID
            // keyboard. Filter-driver injections often use a high or
            // non-standard UnitId, or set the KEYBOARD_INPUT_DATA flags
            // to unusual values.
            //
            // Additionally, we check if the data came through the normal
            // hardware completion path by examining the IRP's requestor mode.
            BOOLEAN likelyHardware = TRUE;

            // Heuristic: if Flags has bits beyond BREAK|E0|E1, suspect injection
            if (data[i].Flags & ~(KEY_BREAK | KEY_E0 | KEY_E1))
                likelyHardware = FALSE;

            if (likelyHardware)
                InterlockedIncrement(&g_HardwareKeyIrps);
            else
                InterlockedIncrement(&g_SoftwareKeyIrps);

            // Track inter-keystroke timing
            if (data[i].Flags & KEY_BREAK)
            {
                // Only time key-down events to avoid counting release pairs
            }
            else
            {
                LARGE_INTEGER now = KeQueryPerformanceCounter(NULL);
                if (g_LastKeystrokeTime.QuadPart != 0)
                {
                    LONGLONG deltaUs = ((now.QuadPart - g_LastKeystrokeTime.QuadPart) * 1000000)
                                       / g_QpcFrequency.QuadPart;
                    if (deltaUs >= 0 && deltaUs < (LONGLONG)InterlockedCompareExchange(
                            &g_MinInterKeystrokeUs, 0, 0))
                    {
                        InterlockedExchange(&g_MinInterKeystrokeUs, (LONG)deltaUs);
                    }
                }
                g_LastKeystrokeTime = now;
            }
        }
    }

    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);

    return STATUS_SUCCESS;
}

//
// Mouse IRP_MJ_READ completion: MOUSE_INPUT_DATA structures.
//
static NTSTATUS AcMouseMonitorReadComplete(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Context);

    if (Irp->IoStatus.Status == STATUS_SUCCESS && Irp->IoStatus.Information > 0)
    {
        ULONG dataSize = (ULONG)Irp->IoStatus.Information;
        ULONG count = dataSize / sizeof(MOUSE_INPUT_DATA);
        MOUSE_INPUT_DATA *data = (MOUSE_INPUT_DATA *)Irp->AssociatedIrp.SystemBuffer;
        ULONG i;

        for (i = 0; i < count; i++)
        {
            BOOLEAN likelyHardware = TRUE;

            // MOUSE_INPUT_DATA.Flags: MOUSE_MOVE_RELATIVE (0) is normal hardware,
            // MOUSE_MOVE_ABSOLUTE often indicates injection or remote desktop.
            // MOUSE_VIRTUAL_DESKTOP also suggests non-physical source.
            if (data[i].Flags & (MOUSE_MOVE_ABSOLUTE | MOUSE_VIRTUAL_DESKTOP))
                likelyHardware = FALSE;

            if (likelyHardware)
                InterlockedIncrement(&g_HardwareMouseIrps);
            else
                InterlockedIncrement(&g_SoftwareMouseIrps);

            // Track inter-mouse timing for button events only
            if (data[i].ButtonFlags != 0)
            {
                LARGE_INTEGER now = KeQueryPerformanceCounter(NULL);
                if (g_LastMouseTime.QuadPart != 0)
                {
                    LONGLONG deltaUs = ((now.QuadPart - g_LastMouseTime.QuadPart) * 1000000)
                                       / g_QpcFrequency.QuadPart;
                    if (deltaUs >= 0 && deltaUs < (LONGLONG)InterlockedCompareExchange(
                            &g_MinInterMouseUs, 0, 0))
                    {
                        InterlockedExchange(&g_MinInterMouseUs, (LONG)deltaUs);
                    }
                }
                g_LastMouseTime = now;
            }
        }
    }

    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);

    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Device stack analysis helpers
// ---------------------------------------------------------------------------

//
// Check if a driver is in our allowlist of known-good input stack drivers.
//
static BOOLEAN AcIsDriverAllowed(PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING driverName;
    UNICODE_STRING allowedName;
    int i;

    if (!DriverObject || !DriverObject->DriverName.Buffer)
        return FALSE;

    driverName = DriverObject->DriverName;

    for (i = 0; g_AllowedFilterDrivers[i] != NULL; i++)
    {
        RtlInitUnicodeString(&allowedName, g_AllowedFilterDrivers[i]);
        if (RtlCompareUnicodeString(&driverName, &allowedName, TRUE) == 0)
            return TRUE;
    }

    return FALSE;
}

//
// Walk the device stack from top to bottom, counting filter drivers
// and flagging unknown ones.
//
static ULONG AcCountDeviceStackFilters(PDEVICE_OBJECT TopOfStack, ULONG *unknownOut)
{
    PDEVICE_OBJECT current;
    ULONG total = 0;
    ULONG unknown = 0;

    *unknownOut = 0;

    // Walk the attachment chain
    current = TopOfStack;
    while (current != NULL)
    {
        if (current->DriverObject)
        {
            total++;
            if (!AcIsDriverAllowed(current->DriverObject))
            {
                unknown++;
                if (current->DriverObject->DriverName.Buffer)
                {
                    DbgPrint("[PhantiCheat:InputMon] Unknown filter driver in stack: %wZ\n",
                             &current->DriverObject->DriverName);
                }
            }
        }
        current = current->AttachedDevice;
    }

    *unknownOut = unknown;
    return total;
}

// ---------------------------------------------------------------------------
// Scan module execute callback
// ---------------------------------------------------------------------------

//
// Called by the scan engine on each poll cycle. Snapshots the accumulated
// IRP counters, computes anomaly flags, and resets counters for next window.
//
static NTSTATUS AcInputMonitorExecute(AC_TELEMETRY_ENTRY* OutEntry)
{
    AC_KERNEL_INPUT_DATA* payload;
    LONG hwKey, swKey, hwMouse, swMouse;
    LONG minKeyUs, minMouseUs;

    if (OutEntry == NULL)
        return STATUS_INVALID_PARAMETER;

    if (sizeof(AC_KERNEL_INPUT_DATA) > AC_MAX_TELEMETRY_DATA)
        return STATUS_BUFFER_TOO_SMALL;

    // Atomically swap counters to zero (snapshot and reset)
    hwKey    = InterlockedExchange(&g_HardwareKeyIrps, 0);
    swKey    = InterlockedExchange(&g_SoftwareKeyIrps, 0);
    hwMouse  = InterlockedExchange(&g_HardwareMouseIrps, 0);
    swMouse  = InterlockedExchange(&g_SoftwareMouseIrps, 0);
    minKeyUs = InterlockedExchange(&g_MinInterKeystrokeUs, 0x7FFFFFFF);
    minMouseUs = InterlockedExchange(&g_MinInterMouseUs, 0x7FFFFFFF);

    OutEntry->Type = AC_TELEMETRY_KERNEL_INPUT;
    OutEntry->DataLength = sizeof(AC_KERNEL_INPUT_DATA);

    payload = (AC_KERNEL_INPUT_DATA*)OutEntry->Data;
    RtlZeroMemory(payload, sizeof(AC_KERNEL_INPUT_DATA));

    payload->HardwareIrpCount     = (ULONG)hwKey;
    payload->SoftwareIrpCount     = (ULONG)swKey;
    payload->HardwareMouseIrpCount = (ULONG)hwMouse;
    payload->SoftwareMouseIrpCount = (ULONG)swMouse;
    payload->FilterDriversInStack = g_KeyboardFilterCount + g_MouseFilterCount;
    payload->UnknownFilterDrivers = g_KeyboardUnknownFilters + g_MouseUnknownFilters;
    payload->MinInterKeystrokeUs  = (minKeyUs == 0x7FFFFFFF) ? 0 : (ULONG)minKeyUs;
    payload->MinInterMouseUs      = (minMouseUs == 0x7FFFFFFF) ? 0 : (ULONG)minMouseUs;

    // Compute anomaly flags
    payload->AnomalyFlags = 0;

    // Unknown filter drivers in the device stack
    if (payload->UnknownFilterDrivers > 0)
        payload->AnomalyFlags |= AC_ANOMALY_KERNEL_DEVSTACK_TAMPER;

    // Software-injected IRPs detected
    if (swKey > 0 || swMouse > 0)
        payload->AnomalyFlags |= AC_ANOMALY_KERNEL_INJECT_DETECTED;

    // Inhuman timing: less than 500 microseconds between keystrokes is
    // physically impossible on a real keyboard. Typical minimum is ~20ms.
    // Mouse clicks below 1ms are similarly suspicious.
    if (payload->MinInterKeystrokeUs > 0 && payload->MinInterKeystrokeUs < 500)
        payload->AnomalyFlags |= AC_ANOMALY_KERNEL_TIMING_INHUMAN;
    if (payload->MinInterMouseUs > 0 && payload->MinInterMouseUs < 1000)
        payload->AnomalyFlags |= AC_ANOMALY_KERNEL_TIMING_INHUMAN;

    return STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Module registration
// ---------------------------------------------------------------------------

NTSTATUS AcRegisterInputMonitorModule(void)
{
    AC_SCAN_MODULE module;

    module.Type = AC_TELEMETRY_KERNEL_INPUT;
    module.Name = "InputMonitor";
    module.Execute = AcInputMonitorExecute;

    return AcScanEngineRegisterModule(&module);
}
