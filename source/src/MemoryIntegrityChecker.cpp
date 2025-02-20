#include "cube.h"

#include "MemoryIntegrityChecker.h"
#include "HashUtil.h"
#include <windows.h>
#include <iostream>
#include <psapi.h>

namespace PhantiCheat {

    MemoryIntegrityChecker::MemoryIntegrityChecker() {
        // Constructor implementation if needed
    }

    std::string MemoryIntegrityChecker::hashModuleSection(const std::string& moduleName, const std::string& section) {
        HMODULE hModule = GetModuleHandle(moduleName.c_str());
        if (!hModule) {
            std::cerr << "Failed to get module handle." << std::endl;
            return "";
        }

        // Get the DOS header
        PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hModule;
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            std::cerr << "Invalid DOS header signature." << std::endl;
            return "";
        }

        // Get the NT headers (PE headers)
        PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hModule + pDosHeader->e_lfanew);
        if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) {
            std::cerr << "Invalid NT header signature." << std::endl;
            return "";
        }

        // Locate the .text section
        PIMAGE_SECTION_HEADER pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);
        for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; ++i) {
            if (strncmp((char*)pSectionHeader->Name, section.c_str(), section.length()) == 0) {
                // Get the size and pointer to the .text section
                BYTE* textSectionData = (BYTE*)hModule + pSectionHeader->VirtualAddress;
                DWORD textSectionSize = pSectionHeader->SizeOfRawData;

                // Use HashUtil to calculate the hash of the .text section
                std::string textSectionHash = PhantiCheat::HashUtil::calculateHash(textSectionData, textSectionSize);
                
                return textSectionHash;  // Return the hash as a string
            }
            pSectionHeader++;
        }

        std::cerr << "Could not find section " << section << " in the " << moduleName << "module." << std::endl;
        return "";
    }

    std::vector<std::string> MemoryIntegrityChecker::listModuleSections(const std::string& moduleName) {
        std::vector<std::string> sections;

        // Get the base address of the module
        HMODULE hModule = GetModuleHandle(moduleName.c_str());
        if (!hModule) {
            std::cerr << "Failed to get module handle." << std::endl;
            return sections;  // Return empty if failed
        }

        // Get the DOS header and validate it
        PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hModule;
        if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            std::cerr << "Invalid DOS header signature." << std::endl;
            return sections;
        }

        // Get the NT headers (PE header)
        PIMAGE_NT_HEADERS pNtHeaders = (PIMAGE_NT_HEADERS)((BYTE*)hModule + pDosHeader->e_lfanew);
        if (pNtHeaders->Signature != IMAGE_NT_SIGNATURE) {
            std::cerr << "Invalid NT header signature." << std::endl;
            return sections;
        }

        // Locate the first section header
        PIMAGE_SECTION_HEADER pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);

        // Iterate through the section headers and collect the section names
        for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; ++i) {
            char sectionName[9] = { 0 };  // Section names are 8 characters max, so we need an extra space for null terminator
            strncpy_s(sectionName, (char*)pSectionHeader->Name, 8);

            sections.push_back(std::string(sectionName));

            // Move to the next section
            pSectionHeader++;
        }

        return sections;
    }

    std::vector<std::string> MemoryIntegrityChecker::listCurrentExecutableModules() {
        std::vector<std::string> modulesList;

        // Get a handle to the current process
        HANDLE hProcess = GetCurrentProcess();

        // Array to hold the module handles
        HMODULE hMods[1024];
        DWORD cbNeeded;

        // Get the list of all loaded modules in the current process
        if (EnumProcessModules(hProcess, hMods, sizeof(hMods), &cbNeeded)) {
            for (unsigned int i = 0; i < (cbNeeded / sizeof(HMODULE)); i++) {
                char moduleName[MAX_PATH];

                // Get the full path to the module's file
                if (GetModuleFileNameExA(hProcess, hMods[i], moduleName, sizeof(moduleName) / sizeof(char))) {
                    // Add the module name to the list
                    modulesList.push_back(std::string(moduleName));
                }
            }
        } else {
            std::cerr << "Failed to enumerate process modules." << std::endl;
        }

        return modulesList;
    }

}
