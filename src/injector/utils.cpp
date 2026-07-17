#include "utils.hpp"
#include <tlhelp32.h>
#include <locale>
#include <codecvt>

namespace Aether {
    bool Utils::SetPriv() {
        HANDLE token;
        TOKEN_PRIVILEGES tp;
        LUID luid;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) 
            return false;

        if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
            CloseHandle(token);
            return false;
        }

        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        bool ok = AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), NULL, NULL);
        CloseHandle(token);
        return ok && (GetLastError() == ERROR_SUCCESS);
    }

    DWORD Utils::GetPid(const std::wstring& name) {
        DWORD pid = 0;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry;
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snap, &entry)) {
                do {
                    if (name == entry.szExeFile) {
                        pid = entry.th32ProcessID;
                        break;
                    }
                } while (Process32NextW(snap, &entry));
            }
            CloseHandle(snap);
        }
        return pid;
    }

    std::vector<Proc> Utils::GetProcs() {
        std::vector<Proc> list;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry;
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snap, &entry)) {
                do {
                    if (entry.th32ProcessID != 0 && entry.th32ProcessID != 4) {
                        list.push_back({ entry.th32ProcessID, entry.szExeFile });
                    }
                } while (Process32NextW(snap, &entry));
            }
            CloseHandle(snap);
        }
        return list;
    }

    std::string Utils::W2S(const std::wstring& wstr) {
        if (wstr.empty()) return "";
        int len = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        std::string str(len, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], len, NULL, NULL);
        return str;
    }

    std::wstring Utils::S2W(const std::string& str) {
        if (str.empty()) return L"";
        int len = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
        std::wstring wstr(len, 0);
        MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], len);
        return wstr;
    }
}
