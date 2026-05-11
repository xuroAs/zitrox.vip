#include "Memory.hpp"
#include <iostream>

Memory::~Memory() {
    if (processHandle) {
        CloseHandle(processHandle);
    }
}

bool Memory::Attach(std::string_view processName) {
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

    if (Process32First(snapshot, &entry) == TRUE) {
        while (Process32Next(snapshot, &entry) == TRUE) {
            if (!processName.compare(entry.szExeFile)) {
                processId = entry.th32ProcessID;
                processHandle = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, processId);
                CloseHandle(snapshot);
                return true;
            }
        }
    }

    CloseHandle(snapshot);
    return false;
}

uintptr_t Memory::GetModuleAddress(std::string_view moduleName) {
    MODULEENTRY32 entry;
    entry.dwSize = sizeof(MODULEENTRY32);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);

    uintptr_t moduleBase = 0;
    if (Module32First(snapshot, &entry) == TRUE) {
        do {
            if (!moduleName.compare(entry.szModule)) {
                moduleBase = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                break;
            }
        } while (Module32Next(snapshot, &entry) == TRUE);
    }

    CloseHandle(snapshot);
    return moduleBase;
}
