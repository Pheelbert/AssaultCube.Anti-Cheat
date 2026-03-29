#ifndef AC_SHARED_H
#define AC_SHARED_H

//
// ac_shared.h - Shared definitions between kernel driver and user-mode anti-cheat client.
// This header must compile in both kernel-mode (C, WDK) and user-mode (C++, MSVC).
//

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <windows.h>
#endif

// Device names
#define AC_DEVICE_NAME      L"\\Device\\PhantiCheat"
#define AC_SYMLINK_NAME     L"\\DosDevices\\PhantiCheat"
#define AC_USERMODE_PATH    "\\\\.\\PhantiCheat"

// Driver version
#define AC_DRIVER_VERSION   1

// IOCTL codes (device type 0x8000, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define AC_IOCTL_TYPE       0x8000

#define IOCTL_AC_GET_STATUS     CTL_CODE(AC_IOCTL_TYPE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_AC_GET_TELEMETRY  CTL_CODE(AC_IOCTL_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Telemetry entry types (extensible -- add new types here)
#define AC_TELEMETRY_NONE             0
#define AC_TELEMETRY_WINDOWS_VERSION  1
#define AC_TELEMETRY_HEARTBEAT        2
#define AC_TELEMETRY_INPUT_ANOMALY    3
#define AC_TELEMETRY_INPUT_DEBUG      4

// Limits
#define AC_MAX_TELEMETRY_ENTRIES  16
#define AC_MAX_TELEMETRY_DATA     256

// ---------------------------------------------------------------------------
// Shared structures
// ---------------------------------------------------------------------------

#pragma pack(push, 1)

// Response for IOCTL_AC_GET_STATUS
typedef struct _AC_DRIVER_STATUS {
    ULONG Version;          // AC_DRIVER_VERSION
    ULONG ModuleCount;      // Number of registered scan modules
    ULONG ScanCount;        // Total scans completed since driver load
    ULONG UptimeSeconds;    // Seconds since driver loaded
} AC_DRIVER_STATUS;

// Single telemetry entry produced by a scan module
typedef struct _AC_TELEMETRY_ENTRY {
    ULONG Type;                             // AC_TELEMETRY_* constant
    ULONG DataLength;                       // Bytes used in Data[]
    UCHAR Data[AC_MAX_TELEMETRY_DATA];      // Type-specific payload
} AC_TELEMETRY_ENTRY;

// Response for IOCTL_AC_GET_TELEMETRY
typedef struct _AC_TELEMETRY_RESPONSE {
    ULONG EntryCount;                                   // Number of valid entries
    AC_TELEMETRY_ENTRY Entries[AC_MAX_TELEMETRY_ENTRIES];
} AC_TELEMETRY_RESPONSE;

// Payload for AC_TELEMETRY_WINDOWS_VERSION (fits inside AC_TELEMETRY_ENTRY.Data)
typedef struct _AC_WINDOWS_VERSION_DATA {
    ULONG MajorVersion;
    ULONG MinorVersion;
    ULONG BuildNumber;
    WCHAR CSDVersion[64];  // Service pack / edition string
} AC_WINDOWS_VERSION_DATA;

// Payload for AC_TELEMETRY_HEARTBEAT (fits inside AC_TELEMETRY_ENTRY.Data)
typedef struct _AC_HEARTBEAT_DATA {
    ULONG DriverVersion;
    ULONG UptimeSeconds;
    ULONG ScanCount;
} AC_HEARTBEAT_DATA;

// Payload for AC_TELEMETRY_INPUT_ANOMALY (fits inside AC_TELEMETRY_ENTRY.Data)
// Produced by user-mode InputTracker, not by the kernel driver.
//
// Four input layers are monitored to detect injection at different stack levels:
//   Layer 1: GetAsyncKeyState   - polls the kernel key state table directly
//   Layer 2: Raw Input (WM_INPUT) - hardware-level HID events
//   Layer 3: Low-level hooks    - WH_KEYBOARD_LL / WH_MOUSE_LL (tags injected events)
//   Layer 4: SDL events         - application-level events the game consumes
//
// Discrepancies between layers indicate injection at different points in the stack.
typedef struct _AC_INPUT_ANOMALY_DATA {
    ULONG TimestampMs;              // GetTickCount() when snapshot was taken
    ULONG InjectedKeyCount;         // Keyboard events with LLKHF_INJECTED flag
    ULONG InjectedMouseCount;       // Mouse events with LLMHF_INJECTED flag
    ULONG SyntheticKeyCount;        // SendInput / keybd_event detected via Raw Input gap
    ULONG SyntheticMouseCount;      // SendInput / mouse_event detected via Raw Input gap
    ULONG RawKeyboardEvents;        // Total raw keyboard events in window
    ULONG RawMouseEvents;           // Total raw mouse events in window
    ULONG SdlKeyEvents;            // Total SDL key events in window
    ULONG SdlMouseEvents;          // Total SDL mouse events in window
    // Layer 1: kernel key-state polling (GetAsyncKeyState)
    ULONG KeyStateTransitions;      // Key up->down transitions detected by polling
    ULONG MouseButtonTransitions;   // Mouse button transitions detected by polling
    // Cross-layer anomaly flags (bitfield)
    ULONG AnomalyFlags;
} AC_INPUT_ANOMALY_DATA;

// AnomalyFlags bitfield values
#define AC_ANOMALY_NONE                  0x00000000
#define AC_ANOMALY_SDL_WITHOUT_RAW       0x00000001  // SDL events with no Raw Input source
#define AC_ANOMALY_SDL_WITHOUT_KEYSTATE  0x00000002  // SDL key events not seen in kernel state
#define AC_ANOMALY_RAW_WITHOUT_KEYSTATE  0x00000004  // Raw Input not reflected in kernel state
#define AC_ANOMALY_LLHOOK_INJECTED       0x00000008  // LL hook flagged injected events
#define AC_ANOMALY_KEYSTATE_WITHOUT_SDL  0x00000010  // Kernel state changed but SDL saw nothing
#define AC_ANOMALY_LAYER_COUNT_MISMATCH  0x00000020  // General cross-layer count divergence
#define AC_ANOMALY_DEBUG_SIMULATED       0x80000000  // This anomaly was debug-injected

// Payload for AC_TELEMETRY_INPUT_DEBUG (debug simulation config, fits in Data)
typedef struct _AC_INPUT_DEBUG_CONFIG {
    ULONG Active;                   // 1 = debug injection on, 0 = off
    ULONG SimulatedSdlKeys;        // Extra SDL key events to inject per snapshot
    ULONG SimulatedSdlMouse;       // Extra SDL mouse events to inject per snapshot
    ULONG SimulatedRawKeys;        // Extra raw keyboard events to inject per snapshot
    ULONG SimulatedRawMouse;       // Extra raw mouse events to inject per snapshot
    ULONG SimulatedKeyStateKeys;   // Extra key-state transitions to inject per snapshot
    ULONG SimulatedInjectedKeys;   // Extra LL-hook injected key events per snapshot
    ULONG SimulatedInjectedMouse;  // Extra LL-hook injected mouse events per snapshot
    ULONG ForceAnomalyFlags;       // Anomaly flags to OR into every snapshot
} AC_INPUT_DEBUG_CONFIG;

#pragma pack(pop)

#endif // AC_SHARED_H
