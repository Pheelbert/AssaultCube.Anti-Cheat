#ifndef ANTI_TAMPER_FILEINTEGRITYCHECKER_H
#define ANTI_TAMPER_FILEINTEGRITYCHECKER_H

#include <string>
#include <vector>
#include <unordered_map>

namespace PhantiCheat {

    class FileIntegrityChecker {
    public:
        FileIntegrityChecker();
        std::unordered_map<std::string, std::string> checkFileIntegrity(const std::vector<std::string>& filePaths);
    };

}

#endif // ANTI_TAMPER_FILEINTEGRITYCHECKER_H
