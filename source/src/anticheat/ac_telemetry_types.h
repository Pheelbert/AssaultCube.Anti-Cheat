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

#pragma pack(pop)

#endif // AC_TELEMETRY_TYPES_H
