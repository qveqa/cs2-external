// memory.h - read-only access to another process; write access is never requested
#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class Memory {
public:
    ~Memory() { Detach(); }

    bool  Attach(const wchar_t* exe_name);
    void  Detach();
    bool  attached() const { return handle_ != nullptr; }
    bool  alive() const;
    DWORD pid() const { return pid_; }

    // module base by name (case-insensitive); resolved once and cached
    uintptr_t module_base(const wchar_t* module_name);
    uintptr_t module_end(const wchar_t* module_name);

    template <typename T>
    std::optional<T> read(uintptr_t address) const {
        T value{};
        if (!read_bytes(address, &value, sizeof(T))) return std::nullopt;
        return value;
    }

    template <typename T>
    T read_or(uintptr_t address, T fallback) const {
        return read<T>(address).value_or(fallback);
    }

    bool read_bytes(uintptr_t address, void* out, size_t size) const;

private:
    struct ModuleEntry {
        std::wstring name;
        uintptr_t    base = 0;
        uintptr_t    end  = 0;
    };

    const ModuleEntry* resolve_module(const wchar_t* module_name);

    HANDLE                   handle_ = nullptr;
    DWORD                    pid_    = 0;
    std::vector<ModuleEntry> modules_;
};
