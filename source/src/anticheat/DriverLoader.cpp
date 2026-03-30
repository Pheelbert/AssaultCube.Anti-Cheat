#ifdef WIN32

#include "DriverLoader.h"
#include <iostream>

namespace PhantiCheat {

    const char* DriverLoader::SERVICE_NAME = "PhantiCheat";
    const char* DriverLoader::DRIVER_FILENAME = "phanticheat.sys";

    DriverLoader::DriverLoader()
        : m_scm(NULL)
        , m_service(NULL)
        , m_loaded(false)
    {
    }

    DriverLoader::~DriverLoader()
    {
        unload();
    }

    bool DriverLoader::isRunningAsAdmin()
    {
        HANDLE token = NULL;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return false;

        TOKEN_ELEVATION elevation;
        DWORD size = sizeof(elevation);
        BOOL result = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
        CloseHandle(token);

        return result && elevation.TokenIsElevated;
    }

    bool DriverLoader::resolveDriverPath(std::string &outPath, std::string &errorMsg)
    {
        char exePath[MAX_PATH];
        DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
        {
            errorMsg = "Could not determine executable path.";
            return false;
        }

        // Strip executable filename to get directory
        char *lastSlash = strrchr(exePath, '\\');
        if (!lastSlash)
        {
            errorMsg = "Could not determine executable directory.";
            return false;
        }
        *(lastSlash + 1) = '\0';

        outPath = std::string(exePath) + DRIVER_FILENAME;

        if (GetFileAttributesA(outPath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            errorMsg = "Driver file not found: " + outPath;
            return false;
        }

        return true;
    }

    bool DriverLoader::cleanupExistingService(std::string &errorMsg)
    {
        SC_HANDLE existing = OpenServiceA(m_scm, SERVICE_NAME, SERVICE_ALL_ACCESS);
        if (!existing)
        {
            DWORD err = GetLastError();
            if (err == ERROR_SERVICE_DOES_NOT_EXIST)
                return true;
            errorMsg = "Failed to query existing service (error " + std::to_string(err) + ").";
            return false;
        }

        // Stop it if running
        SERVICE_STATUS status;
        if (QueryServiceStatus(existing, &status) && status.dwCurrentState != SERVICE_STOPPED)
        {
            ControlService(existing, SERVICE_CONTROL_STOP, &status);

            // Wait up to 5 seconds for it to stop
            for (int i = 0; i < 50; i++)
            {
                if (QueryServiceStatus(existing, &status) && status.dwCurrentState == SERVICE_STOPPED)
                    break;
                Sleep(100);
            }
        }

        BOOL deleted = DeleteService(existing);
        CloseServiceHandle(existing);

        if (!deleted)
        {
            DWORD err = GetLastError();
            if (err != ERROR_SERVICE_MARKED_FOR_DELETE)
            {
                errorMsg = "Failed to remove stale service entry (error " + std::to_string(err) + ").";
                return false;
            }
        }

        return true;
    }

    bool DriverLoader::load(std::string &errorMsg)
    {
        if (m_loaded)
            return true;

        if (!isRunningAsAdmin())
        {
            errorMsg = "Administrator privileges required. Please run the game as Administrator.";
            return false;
        }

        std::string driverPath;
        if (!resolveDriverPath(driverPath, errorMsg))
            return false;

        m_scm = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (!m_scm)
        {
            errorMsg = "Could not open Service Control Manager (error " + std::to_string(GetLastError()) + ").";
            return false;
        }

        if (!cleanupExistingService(errorMsg))
            return false;

        m_service = CreateServiceA(
            m_scm,
            SERVICE_NAME,
            "PhantiCheat Anti-Cheat Driver",
            SERVICE_ALL_ACCESS,
            SERVICE_KERNEL_DRIVER,
            SERVICE_DEMAND_START,
            SERVICE_ERROR_NORMAL,
            driverPath.c_str(),
            NULL, NULL, NULL, NULL, NULL
        );

        if (!m_service)
        {
            DWORD err = GetLastError();
            errorMsg = "Failed to install driver service (error " + std::to_string(err) + ").";
            return false;
        }

        if (!StartServiceA(m_service, 0, NULL))
        {
            DWORD err = GetLastError();
            if (err != ERROR_SERVICE_ALREADY_RUNNING)
            {
                errorMsg = "Failed to start driver (error " + std::to_string(err) + "). Is test signing enabled?";
                DeleteService(m_service);
                CloseServiceHandle(m_service);
                m_service = NULL;
                return false;
            }
        }

        // Wait up to 10 seconds for the driver to reach RUNNING state
        SERVICE_STATUS status;
        for (int i = 0; i < 100; i++)
        {
            if (QueryServiceStatus(m_service, &status) && status.dwCurrentState == SERVICE_RUNNING)
            {
                m_loaded = true;
                std::cout << "[PhantiCheat] Driver loaded successfully." << std::endl;
                return true;
            }
            Sleep(100);
        }

        errorMsg = "Driver service did not start in time.";
        ControlService(m_service, SERVICE_CONTROL_STOP, &status);
        DeleteService(m_service);
        CloseServiceHandle(m_service);
        m_service = NULL;
        return false;
    }

    void DriverLoader::unload()
    {
        if (m_service)
        {
            // Do NOT stop the service here. The game's CloseHandle already
            // triggered CLEANUP in the kernel which detached input filters,
            // leaving the driver loaded but inert. Unloading the driver
            // image while pending keyboard/mouse IRPs still hold completion
            // routine pointers into our code causes a BSOD seconds later
            // when the user presses a key.
            //
            // Mark the service for deletion so it doesn't persist across
            // reboots. cleanupExistingService() on next launch will stop
            // and fully remove the stale driver.
            DeleteService(m_service);
            CloseServiceHandle(m_service);
            m_service = NULL;
        }

        if (m_scm)
        {
            CloseServiceHandle(m_scm);
            m_scm = NULL;
        }

        m_loaded = false;
    }

} // namespace PhantiCheat

#endif // WIN32
