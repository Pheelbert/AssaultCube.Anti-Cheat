#ifndef ANTI_TAMPER_FILEUTIL_H
#define ANTI_TAMPER_FILEUTIL_H

#include <string>

namespace PhantiCheat {

    class FileUtil {
    public:
        static std::string getExecutableDirectory();
        static std::string makePathRelativeToExe(const std::string& filePath, const std::string& exeDir);
        static std::string buildAbsolutePath(const std::string& directoryPath, const std::string& filePath);
    };

}

#endif // ANTI_TAMPER_FILEUTIL_H
