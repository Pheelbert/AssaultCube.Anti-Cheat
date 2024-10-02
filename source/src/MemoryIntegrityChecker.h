#ifndef ANTI_TAMPER_MEMORYINTEGRITYCHECKER_H
#define ANTI_TAMPER_MEMORYINTEGRITYCHECKER_H

#include <string>
#include <vector>

namespace PhantiCheat {

    class MemoryIntegrityChecker {
    public:
        MemoryIntegrityChecker();
        std::string hashModuleSection(const std::string& moduleName, const std::string& section);
        std::vector<std::string> listModuleSections(const std::string& moduleName);
        std::vector<std::string> listCurrentExecutableModules();
    };

}

#endif // ANTI_TAMPER_MEMORYINTEGRITYCHECKER_H
