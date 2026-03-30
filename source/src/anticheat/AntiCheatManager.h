#ifndef ANTICHEATMANAGER_H
#define ANTICHEATMANAGER_H

#ifdef WIN32

#include <windows.h>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include "ac_shared.h"
#include "InputTracker.h"

namespace PhantiCheat {

    class AntiCheatManager {
    public:
        AntiCheatManager();
        ~AntiCheatManager();

        // Opens driver device handle and starts the background polling thread.
        // Returns true if the driver was found and communication established.
        // Returns false if driver not loaded (caller should abort launch).
        bool initialize();

        // Stops background thread and closes driver handle. Safe to call multiple times.
        void shutdown();

        // Returns true if there is queued telemetry ready to send over the network.
        bool hasPendingTelemetry() const;

        // Pops and returns the next telemetry response from the queue.
        // Only call when hasPendingTelemetry() returns true.
        AC_TELEMETRY_RESPONSE popTelemetry();

        // Whether the kernel driver is connected.
        bool isDriverConnected() const;

        // Input tracking: call after the game window is created.
        // hwnd is the game's main SDL window handle.
        bool initializeInputTracking(HWND hwnd);

        // Accessor so the SDL event loop can record events.
        InputTracker* getInputTracker() { return &m_inputTracker; }

        // Debug: configure simulated input anomalies for testing detection.
        // When active, artificial counter offsets are mixed into each snapshot
        // so different hook layers report conflicting data.
        void setInputDebugConfig(const AC_INPUT_DEBUG_CONFIG &cfg);
        void clearInputDebugConfig();
        bool isInputDebugActive() const;

    private:
        void workerThread();

        HANDLE m_driverHandle;
        std::thread m_worker;
        std::atomic<bool> m_running;
        std::atomic<bool> m_driverConnected;

        mutable std::mutex m_mutex;
        std::queue<AC_TELEMETRY_RESPONSE> m_telemetryQueue;

        InputTracker m_inputTracker;

        // Prevent copying
        AntiCheatManager(const AntiCheatManager&) = delete;
        AntiCheatManager& operator=(const AntiCheatManager&) = delete;
    };

} // namespace PhantiCheat

#endif // WIN32
#endif // ANTICHEATMANAGER_H
