#pragma once
#include <windows.h>
#include <string>

namespace Aether {
    enum class InjMethod {
        CRT = 0,
        Hijack,
        APC,
        MMap,
        NtEx,
        Ldr,
        EB,
        Hook
    };

    struct InjOpts {
        bool erasePE = false;
        bool hideMod = false;
        bool hideTh = false;
        bool clearSec = false;
        bool eraseExp = false;
    };

    class Injector {
    public:
        static std::wstring Inject(DWORD pid, const std::wstring& dll, InjMethod method, const InjOpts& opts);
        static std::wstring Launch(const std::wstring& exe, const std::wstring& dll, InjMethod method, const InjOpts& opts);

    private:
        static std::wstring InjectCRT(DWORD pid, const std::wstring& dll, const InjOpts& opts);
        static std::wstring InjectHijack(DWORD pid, const std::wstring& dll, const InjOpts& opts);
        static std::wstring InjectAPC(DWORD pid, const std::wstring& dll, const InjOpts& opts);
        static std::wstring InjectMMap(DWORD pid, const std::wstring& dll, const InjOpts& opts);
        static std::wstring InjectNtEx(DWORD pid, const std::wstring& dll, const InjOpts& opts);
        static std::wstring InjectLdr(DWORD pid, const std::wstring& dll, const InjOpts& opts);
        static std::wstring InjectEB(DWORD pid, const std::wstring& dll, const InjOpts& opts);
        static std::wstring InjectHook(DWORD pid, const std::wstring& dll, const InjOpts& opts);
    };
}
