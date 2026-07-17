#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace Aether {
    struct Proc {
        DWORD pid;
        std::wstring name;
    };

    class Utils {
    public:
        static bool SetPriv();
        static DWORD GetPid(const std::wstring& name);
        static std::vector<Proc> GetProcs();
        static std::string W2S(const std::wstring& wstr);
        static std::wstring S2W(const std::string& str);
    };
}
