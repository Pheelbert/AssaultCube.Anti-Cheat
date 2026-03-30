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
#include "../ac_log.h"
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

// Remove locks: track outstanding IRPs so we can drain them before detach.
static IO_REMOVE_LOCK g_KeyboardRemoveLock;
static IO_REMOVE_LOCK g_MouseRemoveLock;

// Set to 1 when detach starts; dispatch functions reject new IRPs.
static volatile LONG g_Detaching = 0;

// Ensures AcInputMonitorDetach() is idempotent (called from both Cleanup and DriverUnload).
static volatile LONG g_DetachDone = 0;

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

    IoInitializeRemoveLock(&g_KeyboardRemoveLock, 'KbdL', 0, 0);
    IoInitializeRemoveLock(&g_MouseRemoveLock, 'MouL', 0, 0);

    g_QpcFrequency.QuadPart = 0;
    KeQueryPerformanceCounter(&g_QpcFrequency);
    if (g_QpcFrequency.QuadPart == 0)
        g_QpcFrequency.QuadPart = 1; // fallback to avoid div-by-zero

    // --- Keyboard ---
    RtlInitUnicodeString(&kbdName, L"\\Device\\KeyboardClass0");

    status = IoGetDeviceObjectPointer(&kbdName, FILE_READ_DATA, &fileObject, &topOfStack);
    if (!NT_SUCCESS(status))
    {
        AcLogWrite("InputMon: Cannot get KeyboardClass0: 0x%08X", status);
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
        AcLogWrite("InputMon: Cannot create keyboard filter device: 0x%08X", status);
        goto attach_mouse;
    }

    // Match flags of target device
    g_KeyboardFilterDevice->Flags |= (topOfStack->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO));
    g_KeyboardFilterDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    g_KeyboardTargetDevice = IoAttachDeviceToDeviceStack(g_KeyboardFilterDevice, topOfStack);
    if (!g_KeyboardTargetDevice)
    {
        AcLogWrite("InputMon: IoAttachDeviceToDeviceStack failed for keyboard");
        IoDeleteDevice(g_KeyboardFilterDevice);
        g_KeyboardFilterDevice = NULL;
        goto attach_mouse;
    }

    AcLogWrite("InputMon: Attached to keyboard stack (%lu filters, %lu unknown). FilterDev=%p TargetDev=%p",
             g_KeyboardFilterCount, g_KeyboardUnknownFilters, g_KeyboardFilterDevice, g_KeyboardTargetDevice);

attach_mouse:
    // --- Mouse ---
    RtlInitUnicodeString(&mouseName, L"\\Device\\PointerClass0");

    status = IoGetDeviceObjectPointer(&mouseName, FILE_READ_DATA, &fileObject, &topOfStack);
    if (!NT_SUCCESS(status))
    {
        AcLogWrite("InputMon: Cannot get PointerClass0: 0x%08X", status);
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
        AcLogWrite("InputMon: Cannot create mouse filter device: 0x%08X", status);
        goto done;
    }

    g_MouseFilterDevice->Flags |= (topOfStack->Flags & (DO_BUFFERED_IO | DO_DIRECT_IO));
    g_MouseFilterDevice->Flags &= ~DO_DEVICE_INITIALIZING;

    g_MouseTargetDevice = IoAttachDeviceToDeviceStack(g_MouseFilterDevice, topOfStack);
    if (!g_MouseTargetDevice)
    {
        AcLogWrite("InputMon: IoAttachDeviceToDeviceStack failed for mouse");
        IoDeleteDevice(g_MouseFilterDevice);
        g_MouseFilterDevice = NULL;
        goto done;
    }

    AcLogWrite("InputMon: Attached to mouse stack (%lu filters, %lu unknown). FilterDev=%p TargetDev=%p",
             g_MouseFilterCount, g_MouseUnknownFilters, g_MouseFilterDevice, g_MouseTargetDevice);

done:
    return STATUS_SUCCESS; // non-fatal even if attachment fails
}

//
// AcInputMonitorDetach - Remove filter devices from device stacks.
// Called from DriverUnload.
//
void AcInputMonitorDetach(void)
{
    // Idempotency: only run once (called from both Cleanup and DriverUnload)
    if (InterlockedCompareExchange(&g_DetachDone, 1, 0) != 0)
    {
        AcLogWrite("InputMon: Detach already completed, skipping.");
        return;
    }

    AcLogWrite("InputMon: Detaching. KbdTarget=%p KbdFilter=%p MouseTarget=%p MouseFilter=%p",
               g_KeyboardTargetDevice, g_KeyboardFilterDevice,
               g_MouseTargetDevice, g_MouseFilterDevice);

    // Stop dispatch functions from accepting new IRPs
    InterlockedExchange(&g_Detaching, 1);

    // --- Keyboard: drain pending IRPs, then detach ---
    if (g_KeyboardTargetDevice)
    {
        AcLogWrite("InputMon: Waiting for pending keyboard IRPs to drain...");
        IoReleaseRemoveLockAndWait(&g_KeyboardRemoveLock, NULL);
        AcLogWrite("InputMon: Keyboard IRPs drained. Detaching.");

        IoDetachDevice(g_KeyboardTargetDevice);
        g_KeyboardTargetDevice = NULL;
    }
    if (g_KeyboardFilterDevice)
    {
        IoDeleteDevice(g_KeyboardFilterDevice);
        g_KeyboardFilterDevice = NULL;
    }

    // --- Mouse: drain pending IRPs, then detach ---
    if (g_MouseTargetDevice)
    {
        AcLogWrite("InputMon: Waiting for pending mouse IRPs to drain...");
        IoReleaseRemoveLockAndWait(&g_MouseRemoveLock, NULL);
        AcLogWrite("InputMon: Mouse IRPs drained. Detaching.");

        IoDetachDevice(g_MouseTargetDevice);
        g_MouseTargetDevice = NULL;
    }
    if (g_MouseFilterDevice)
    {
        IoDeleteDevice(g_MouseFilterDevice);
        g_MouseFilterDevice = NULL;
    }

    // Flush all queued DPCs across all processors before returning.
    // The I/O manager may have queued DPCs as part of IRP completion
    // processing that reference our completion routine code. If the
    // driver image is unloaded before these DPCs execute, BSOD.
    AcLogWrite("InputMon: Flushing queued DPCs...");
    KeFlushQueuedDpcs();

    AcLogWrite("InputMon: Filter devices detached.");
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
    NTSTATUS status;

    // Reject new IRPs if we are detaching
    if (InterlockedCompareExchange(&g_Detaching, 0, 0) != 0)
        goto complete_invalid;

    if (DeviceObject == g_KeyboardFilterDevice)
    {
        status = IoAcquireRemoveLock(&g_KeyboardRemoveLock, Irp);
        if (!NT_SUCCESS(status))
            goto complete_invalid;

        if (g_KeyboardTargetDevice)
        {
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, AcInputMonitorReadComplete, NULL, TRUE, TRUE, TRUE);
            return IoCallDriver(g_KeyboardTargetDevice, Irp);
            // Lock released in AcInputMonitorReadComplete
        }

        IoReleaseRemoveLock(&g_KeyboardRemoveLock, Irp);
        goto complete_invalid;
    }
    else if (DeviceObject == g_MouseFilterDevice)
    {
        status = IoAcquireRemoveLock(&g_MouseRemoveLock, Irp);
        if (!NT_SUCCESS(status))
            goto complete_invalid;

        if (g_MouseTargetDevice)
        {
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, AcMouseMonitorReadComplete, NULL, TRUE, TRUE, TRUE);
            return IoCallDriver(g_MouseTargetDevice, Irp);
            // Lock released in AcMouseMonitorReadComplete
        }

        IoReleaseRemoveLock(&g_MouseRemoveLock, Irp);
        goto complete_invalid;
    }

complete_invalid:
    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

//
// AcInputMonitorDispatchPassthrough - For non-read IRPs on our filter devices,
// just pass them down the stack untouched.
//
NTSTATUS AcInputMonitorDispatchPassthrough(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PDEVICE_OBJECT target = NULL;
    PIO_REMOVE_LOCK removeLock = NULL;
    NTSTATUS status;
    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);

    // Filter drivers MUST pass PnP and Power IRPs down the stack
    // unconditionally - even during teardown.
    BOOLEAN mustPassDown = (irpSp->MajorFunction == IRP_MJ_PNP ||
                            irpSp->MajorFunction == IRP_MJ_POWER);

    // Reject new IRPs if we are detaching (except PnP/Power)
    if (!mustPassDown && InterlockedCompareExchange(&g_Detaching, 0, 0) != 0)
        goto complete_error;

    if (DeviceObject == g_KeyboardFilterDevice)
    {
        target = g_KeyboardTargetDevice;
        removeLock = &g_KeyboardRemoveLock;
    }
    else if (DeviceObject == g_MouseFilterDevice)
    {
        target = g_MouseTargetDevice;
        removeLock = &g_MouseRemoveLock;
    }

    if (target && removeLock)
    {
        // During detach, PnP/Power IRPs bypass the remove lock so they
        // don't block the drain.  The target pointer is still valid
        // because IoDetachDevice has not been called yet (it waits
        // behind IoReleaseRemoveLockAndWait).
        if (mustPassDown && InterlockedCompareExchange(&g_Detaching, 0, 0) != 0)
        {
            IoSkipCurrentIrpStackLocation(Irp);
            if (irpSp->MajorFunction == IRP_MJ_POWER)
                return PoCallDriver(target, Irp);
            return IoCallDriver(target, Irp);
        }

        status = IoAcquireRemoveLock(removeLock, Irp);
        if (!NT_SUCCESS(status))
            goto complete_error;

        IoSkipCurrentIrpStackLocation(Irp);
        status = IoCallDriver(target, Irp);
        IoReleaseRemoveLock(removeLock, Irp);
        return status;
    }

complete_error:
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

    if (Irp->IoStatus.Status == STATUS_SUCCESS && Irp->IoStatus.Information > 0
        && Irp->AssociatedIrp.SystemBuffer != NULL)
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
                LONGLONG lastTime = InterlockedCompareExchange64(&g_LastKeystrokeTime.QuadPart, 0, 0);
                if (lastTime != 0)
                {
                    LONGLONG deltaUs = ((now.QuadPart - lastTime) * 1000000)
                                       / g_QpcFrequency.QuadPart;
                    if (deltaUs >= 0 && deltaUs < (LONGLONG)InterlockedCompareExchange(
                            &g_MinInterKeystrokeUs, 0, 0))
                    {
                        InterlockedExchange(&g_MinInterKeystrokeUs, (LONG)deltaUs);
                    }
                }
                InterlockedExchange64(&g_LastKeystrokeTime.QuadPart, now.QuadPart);
            }
        }
    }

    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);

    IoReleaseRemoveLock(&g_KeyboardRemoveLock, Irp);
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

    if (Irp->IoStatus.Status == STATUS_SUCCESS && Irp->IoStatus.Information > 0
        && Irp->AssociatedIrp.SystemBuffer != NULL)
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
                LONGLONG lastMouseTime = InterlockedCompareExchange64(&g_LastMouseTime.QuadPart, 0, 0);
                if (lastMouseTime != 0)
                {
                    LONGLONG deltaUs = ((now.QuadPart - lastMouseTime) * 1000000)
                                       / g_QpcFrequency.QuadPart;
                    if (deltaUs >= 0 && deltaUs < (LONGLONG)InterlockedCompareExchange(
                            &g_MinInterMouseUs, 0, 0))
                    {
                        InterlockedExchange(&g_MinInterMouseUs, (LONG)deltaUs);
                    }
                }
                InterlockedExchange64(&g_LastMouseTime.QuadPart, now.QuadPart);
            }
        }
    }

    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);

    IoReleaseRemoveLock(&g_MouseRemoveLock, Irp);
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
                    AcLogWrite("InputMon: Unknown filter driver in stack (ptr=%p)",
                             current->DriverObject);
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
