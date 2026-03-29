#ifdef WIN32

#include "AntiCheatManager.h"
#include <iostream>
#include <chrono>

namespace PhantiCheat {

    // How often the background thread polls the driver (milliseconds)
    static const int AC_POLL_INTERVAL_MS = 5000;

    // Maximum queued telemetry responses before oldest are dropped
    static const size_t AC_MAX_QUEUE_SIZE = 16;

    AntiCheatManager::AntiCheatManager()
        : m_driverHandle(INVALID_HANDLE_VALUE)
        , m_running(false)
        , m_driverConnected(false)
    {
    }

    AntiCheatManager::~AntiCheatManager()
    {
        shutdown();
    }

    bool AntiCheatManager::initialize()
    {
        if (m_running.load())
            return m_driverConnected.load();

        // Try to open the kernel driver device
        m_driverHandle = CreateFileA(
            AC_USERMODE_PATH,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (m_driverHandle == INVALID_HANDLE_VALUE)
        {
            DWORD err = GetLastError();
            std::cerr << "[PhantiCheat] Could not open driver device (error " << err << "). "
                      << "Kernel anti-cheat will not be active." << std::endl;
            m_driverConnected.store(false);

            // Start background thread anyway for input tracking polling
            m_running.store(true);
            m_worker = std::thread(&AntiCheatManager::workerThread, this);
            return false;
        }

        std::cout << "[PhantiCheat] Kernel driver connected." << std::endl;
        m_driverConnected.store(true);

        // Verify driver is responding with a status check
        AC_DRIVER_STATUS status;
        DWORD bytesReturned = 0;
        BOOL ok = DeviceIoControl(
            m_driverHandle,
            IOCTL_AC_GET_STATUS,
            NULL, 0,
            &status, sizeof(status),
            &bytesReturned,
            NULL
        );

        if (ok && bytesReturned == sizeof(AC_DRIVER_STATUS))
        {
            std::cout << "[PhantiCheat] Driver v" << status.Version
                      << ", " << status.ModuleCount << " modules, "
                      << status.ScanCount << " scans, uptime "
                      << status.UptimeSeconds << "s" << std::endl;
        }

        // Start background polling thread
        m_running.store(true);
        m_worker = std::thread(&AntiCheatManager::workerThread, this);

        return true;
    }

    void AntiCheatManager::shutdown()
    {
        m_inputTracker.shutdown();

        if (m_running.load())
        {
            m_running.store(false);
            if (m_worker.joinable())
                m_worker.join();
        }

        if (m_driverHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_driverHandle);
            m_driverHandle = INVALID_HANDLE_VALUE;
        }

        m_driverConnected.store(false);
    }

    bool AntiCheatManager::hasPendingTelemetry() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return !m_telemetryQueue.empty();
    }

    AC_TELEMETRY_RESPONSE AntiCheatManager::popTelemetry()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        AC_TELEMETRY_RESPONSE response = m_telemetryQueue.front();
        m_telemetryQueue.pop();
        return response;
    }

    bool AntiCheatManager::isDriverConnected() const
    {
        return m_driverConnected.load();
    }

    bool AntiCheatManager::initializeInputTracking(HWND hwnd)
    {
        return m_inputTracker.initialize(hwnd);
    }

    void AntiCheatManager::setInputDebugConfig(const AC_INPUT_DEBUG_CONFIG &cfg)
    {
        m_inputTracker.setDebugConfig(cfg);
    }

    void AntiCheatManager::clearInputDebugConfig()
    {
        m_inputTracker.clearDebugConfig();
    }

    bool AntiCheatManager::isInputDebugActive() const
    {
        return m_inputTracker.isDebugActive();
    }

    void AntiCheatManager::workerThread()
    {
        while (m_running.load())
        {
            if (m_driverHandle != INVALID_HANDLE_VALUE)
            {
                AC_TELEMETRY_RESPONSE response;
                DWORD bytesReturned = 0;

                BOOL ok = DeviceIoControl(
                    m_driverHandle,
                    IOCTL_AC_GET_TELEMETRY,
                    NULL, 0,
                    &response, sizeof(response),
                    &bytesReturned,
                    NULL
                );

                if (ok && bytesReturned == sizeof(AC_TELEMETRY_RESPONSE) && response.EntryCount > 0)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);

                    // Drop oldest if queue is full
                    while (m_telemetryQueue.size() >= AC_MAX_QUEUE_SIZE)
                        m_telemetryQueue.pop();

                    m_telemetryQueue.push(response);
                }
                else if (!ok)
                {
                    DWORD err = GetLastError();
                    // If device went away, mark disconnected
                    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_DEV_NOT_EXIST)
                    {
                        std::cerr << "[PhantiCheat] Driver disconnected." << std::endl;
                        m_driverConnected.store(false);
                        CloseHandle(m_driverHandle);
                        m_driverHandle = INVALID_HANDLE_VALUE;
                    }
                }
            }

            // Poll kernel key state before taking the snapshot.
            // This samples GetAsyncKeyState for all VKs so the snapshot
            // has fresh transition counts from the kernel layer.
            if (m_inputTracker.isActive())
                m_inputTracker.pollKeyState();

            // Poll input tracker for anomalies (runs regardless of driver state)
            if (m_inputTracker.isActive())
            {
                AC_INPUT_ANOMALY_DATA anomaly;
                if (m_inputTracker.snapshot(&anomaly))
                {
                    // Package as a single-entry telemetry response
                    AC_TELEMETRY_RESPONSE inputResponse;
                    memset(&inputResponse, 0, sizeof(inputResponse));
                    inputResponse.EntryCount = 1;
                    inputResponse.Entries[0].Type = AC_TELEMETRY_INPUT_ANOMALY;
                    inputResponse.Entries[0].DataLength = sizeof(AC_INPUT_ANOMALY_DATA);
                    memcpy(inputResponse.Entries[0].Data, &anomaly, sizeof(anomaly));

                    std::lock_guard<std::mutex> lock(m_mutex);
                    while (m_telemetryQueue.size() >= AC_MAX_QUEUE_SIZE)
                        m_telemetryQueue.pop();
                    m_telemetryQueue.push(inputResponse);
                }
            }

            // Sleep in small increments so we can respond to shutdown quickly
            for (int i = 0; i < AC_POLL_INTERVAL_MS / 100 && m_running.load(); i++)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

} // namespace PhantiCheat

#endif // WIN32
