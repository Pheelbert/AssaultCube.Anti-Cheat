#ifndef INPUT_MONITOR_H
#define INPUT_MONITOR_H

#include <ntddk.h>

//
// input_monitor.h - Public API for the kernel input monitor filter.
//
// The input monitor attaches as an upper filter on keyboard and mouse
// class device stacks. These functions are called from driver_main.c
// to manage the filter lifecycle and route IRPs.
//

// Attach filter devices to keyboard/mouse class stacks.
// Call from DriverEntry after IoCreateDevice for the control device.
NTSTATUS AcInputMonitorAttach(PDRIVER_OBJECT DriverObject);

// Detach and delete filter devices. Call from DriverUnload before
// deleting the control device.
void AcInputMonitorDetach(void);

// IRP_MJ_READ dispatch for filter devices. Sets a completion routine
// to monitor I/O data returning from the port driver.
NTSTATUS AcInputMonitorDispatchRead(PDEVICE_OBJECT DeviceObject, PIRP Irp);

// Passthrough dispatch for all non-read IRPs on filter devices.
NTSTATUS AcInputMonitorDispatchPassthrough(PDEVICE_OBJECT DeviceObject, PIRP Irp);

// Check if a device object belongs to the input monitor filter.
// Used by driver_main.c dispatch to route IRPs to the correct handler.
extern PDEVICE_OBJECT g_KeyboardFilterDevice;
extern PDEVICE_OBJECT g_MouseFilterDevice;

#endif // INPUT_MONITOR_H
