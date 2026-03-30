#ifndef DRIVERLOADER_H
#define DRIVERLOADER_H

#ifdef WIN32

#include <windows.h>
#include <string>

namespace PhantiCheat {

    class DriverLoader {
    public:
        DriverLoader();
        ~DriverLoader();

        // Loads and starts the kernel driver. Returns true on success.
        // On failure, populates errorMsg with a human-readable description.
        bool load(std::string &errorMsg);

        // Stops the driver and removes the service entry.
        // Safe to call if the driver was never loaded or already unloaded.
        void unload();

        bool isLoaded() const { return m_loaded; }

    private:
        static const char* SERVICE_NAME;
        static const char* DRIVER_FILENAME;

        SC_HANDLE m_scm;
        SC_HANDLE m_service;
        bool m_loaded;

        bool resolveDriverPath(std::string &outPath, std::string &errorMsg);
        bool cleanupExistingService(std::string &errorMsg);
        static bool isRunningAsAdmin();

        DriverLoader(const DriverLoader&) = delete;
        DriverLoader& operator=(const DriverLoader&) = delete;
    };

} // namespace PhantiCheat

#endif // WIN32
#endif // DRIVERLOADER_H
