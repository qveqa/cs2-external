// memory.cpp - only PROCESS_VM_READ; the game process is never written to
#include "memory.h"

#include <tlhelp32.h>

bool Memory::Attach(const wchar_t* exe_name) {
    if (handle_) return true;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    DWORD pid = 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, exe_name) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    if (pid == 0) return false;

    // read + query only: no PROCESS_VM_WRITE, no PROCESS_VM_OPERATION
    handle_ = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!handle_) return false;

    pid_ = pid;
    modules_.clear();
    return true;
}

void Memory::Detach() {
    if (handle_) {
        CloseHandle(handle_);
        handle_ = nullptr;
    }
    pid_ = 0;
    modules_.clear();
}

bool Memory::alive() const {
    if (!handle_) return false;
    DWORD exit_code = 0;
    if (!GetExitCodeProcess(handle_, &exit_code)) return false;
    return exit_code == STILL_ACTIVE;
}

bool Memory::read_bytes(uintptr_t address, void* out, size_t size) const {
    if (!handle_ || address == 0 || size == 0 || out == nullptr) return false;

    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(address), out, size, &bytes_read))
        return false;
    return bytes_read == size;
}

const Memory::ModuleEntry* Memory::resolve_module(const wchar_t* module_name) {
    if (!handle_) return nullptr;

    for (const ModuleEntry& entry : modules_) {
        if (_wcsicmp(entry.name.c_str(), module_name) == 0) return &entry;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid_);
    if (snapshot == INVALID_HANDLE_VALUE) return nullptr;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, module_name) == 0) {
                ModuleEntry found;
                found.name = entry.szModule;
                found.base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
                found.end  = found.base + entry.modBaseSize;
                modules_.push_back(found);
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    for (const ModuleEntry& stored : modules_) {
        if (_wcsicmp(stored.name.c_str(), module_name) == 0) return &stored;
    }
    return nullptr;
}

uintptr_t Memory::module_base(const wchar_t* module_name) {
    const ModuleEntry* entry = resolve_module(module_name);
    return entry ? entry->base : 0;
}

uintptr_t Memory::module_end(const wchar_t* module_name) {
    const ModuleEntry* entry = resolve_module(module_name);
    return entry ? entry->end : 0;
}
