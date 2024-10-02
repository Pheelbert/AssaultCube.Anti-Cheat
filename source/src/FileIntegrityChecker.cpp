#include "cube.h"
#include "FileIntegrityChecker.h"
#include "HashUtil.h"
#include <iostream>

namespace PhantiCheat {

    FileIntegrityChecker::FileIntegrityChecker() {
    }

    std::unordered_map<std::string, std::string> FileIntegrityChecker::checkFileIntegrity(const std::vector<std::string>& filePaths) {
        std::unordered_map<std::string, std::string> computedHashes;

        for (const auto& filePath : filePaths) {
            try {
                std::string currentHash = HashUtil::calculateHash(filePath);
                computedHashes[filePath] = currentHash;
            } catch (const std::exception& ex) {
                std::cerr << "Error checking file: " << filePath << " - " << ex.what() << std::endl;
            }
        }

        return computedHashes;
    }

}
