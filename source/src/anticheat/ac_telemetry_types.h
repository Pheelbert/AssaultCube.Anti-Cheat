#ifndef AC_TELEMETRY_TYPES_H
#define AC_TELEMETRY_TYPES_H

//
// ac_telemetry_types.h - Platform-independent telemetry type definitions.
//
// Used by the server (which may compile on Linux) to parse telemetry data
// received from Windows clients. Does not depend on windows.h.
//

#include <stdint.h>

// Telemetry entry types (must match ac_shared.h)
#define AC_TELEMETRY_NONE             0
#define AC_TELEMETRY_WINDOWS_VERSION  1
#define AC_TELEMETRY_HEARTBEAT        2
#define AC_TELEMETRY_INPUT_ANOMALY    3
#define AC_TELEMETRY_INPUT_DEBUG      4
#define AC_TELEMETRY_KERNEL_INPUT     5

#define AC_MAX_TELEMETRY_DATA         256

#pragma pack(push, 1)

// Windows version telemetry payload
typedef struct {
    uint32_t MajorVersion;
    uint32_t MinorVersion;
    uint32_t BuildNumber;
    uint16_t CSDVersion[64];  // UTF-16 service pack string (matches WCHAR on Windows)
} AcWindowsVersionData;

// Heartbeat telemetry payload
typedef struct {
    uint32_t DriverVersion;
    uint32_t UptimeSeconds;
    uint32_t ScanCount;
} AcHeartbeatData;

// Input anomaly telemetry payload
typedef struct {
    uint32_t TimestampMs;
    uint32_t InjectedKeyCount;
    uint32_t InjectedMouseCount;
    uint32_t SyntheticKeyCount;
    uint32_t SyntheticMouseCount;
    uint32_t RawKeyboardEvents;
    uint32_t RawMouseEvents;
    uint32_t SdlKeyEvents;
    uint32_t SdlMouseEvents;
    uint32_t KeyStateTransitions;
    uint32_t MouseButtonTransitions;
    uint32_t AnomalyFlags;
} AcInputAnomalyData;

// Kernel input telemetry payload (Layer 0 - from driver IRP interception)
typedef struct {
    uint32_t HardwareIrpCount;
    uint32_t SoftwareIrpCount;
    uint32_t HardwareMouseIrpCount;
    uint32_t SoftwareMouseIrpCount;
    uint32_t FilterDriversInStack;
    uint32_t UnknownFilterDrivers;
    uint32_t MinInterKeystrokeUs;
    uint32_t MinInterMouseUs;
    uint32_t AnomalyFlags;
} AcKernelInputData;

// Anomaly flag constants (must match ac_shared.h)
#define AC_ANOMALY_NONE                  0x00000000
#define AC_ANOMALY_SDL_WITHOUT_RAW       0x00000001
#define AC_ANOMALY_SDL_WITHOUT_KEYSTATE  0x00000002
#define AC_ANOMALY_RAW_WITHOUT_KEYSTATE  0x00000004
#define AC_ANOMALY_LLHOOK_INJECTED       0x00000008
#define AC_ANOMALY_KEYSTATE_WITHOUT_SDL  0x00000010
#define AC_ANOMALY_LAYER_COUNT_MISMATCH  0x00000020
#define AC_ANOMALY_DEBUG_SIMULATED       0x80000000

// Kernel-layer anomaly flags (must match ac_shared.h)
#define AC_ANOMALY_KERNEL_DEVSTACK_TAMPER  0x00000040
#define AC_ANOMALY_KERNEL_IRQ_MISMATCH     0x00000080
#define AC_ANOMALY_KERNEL_INJECT_DETECTED  0x00000100
#define AC_ANOMALY_KERNEL_TIMING_INHUMAN   0x00000200

// Debug simulation config payload
typedef struct {
    uint32_t Active;
    uint32_t SimulatedSdlKeys;
    uint32_t SimulatedSdlMouse;
    uint32_t SimulatedRawKeys;
    uint32_t SimulatedRawMouse;
    uint32_t SimulatedKeyStateKeys;
    uint32_t SimulatedInjectedKeys;
    uint32_t SimulatedInjectedMouse;
    uint32_t ForceAnomalyFlags;
} AcInputDebugConfig;

#pragma pack(pop)

#endif // AC_TELEMETRY_TYPES_H
