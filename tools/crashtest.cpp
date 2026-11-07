// crashtest.cpp - verifies the crash-handler pattern used by the overlay: writes a
// text stack trace (module + offset per frame) and a minidump next to the exe, then
// crashes on purpose. the text trace is readable without any debugger.
// build: g++ tools/crashtest.cpp -std=c++20 -O0 -o %TEMP%/opencode/crashtest.exe -ldbghelp
#include <windows.h>
#include <dbghelp.h>

#include <cstdio>

namespace {

void WriteStackText(const wchar_t* path) {
    void* frames[64] = {};
    const USHORT count = RtlCaptureStackBackTrace(1, 64, frames, nullptr);

    FILE* file = _wfopen(path, L"w");
    if (!file) return;

    std::fprintf(file, "frames: %u\n", count);
    for (USHORT i = 0; i < count; ++i) {
        HMODULE module = nullptr;
        wchar_t module_path[MAX_PATH] = L"?";
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(frames[i]), &module)) {
            GetModuleFileNameW(module, module_path, MAX_PATH);
        }
        const unsigned long long offset = reinterpret_cast<unsigned long long>(frames[i]) -
                                          reinterpret_cast<unsigned long long>(module);
        std::fprintf(file, "  %ls + 0x%llX\n", module_path, offset);
    }
    std::fclose(file);
}

LONG WINAPI CrashHandler(EXCEPTION_POINTERS* info) {
    wchar_t base[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, base, MAX_PATH);
    wchar_t* dot = wcsrchr(base, L'.');
    if (dot) *dot = L'\0';

    wchar_t path[MAX_PATH] = {};
    wcscpy_s(path, base);
    wcscat_s(path, L"_crash.txt");
    WriteStackText(path);

    wcscpy_s(path, base);
    wcscat_s(path, L"_crash.dmp");
    const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei = {};
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers    = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          MiniDumpNormal, &mei, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

void DeepCrash(int depth) {
    if (depth > 0)
        DeepCrash(depth - 1);

    volatile int* boom = nullptr;
    *boom = 42;   // deliberate null write
}

}  // namespace

int main() {
    SetUnhandledExceptionFilter(CrashHandler);
    DeepCrash(3);
    return 0;
}
