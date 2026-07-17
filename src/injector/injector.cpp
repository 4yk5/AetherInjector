#include "injector.hpp"
#include "utils.hpp"
#include <vector>
#include <fstream>
#include <tlhelp32.h>

#define TH_HIDE_DEBUG 0x00000004

namespace Aether {
    typedef NTSTATUS(NTAPI* pfnNtCreateThreadEx)(
        OUT PHANDLE, ACCESS_MASK, LPVOID, HANDLE, LPTHREAD_START_ROUTINE, LPVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, LPVOID
    );

    typedef struct _UNICODE_STRING {
        USHORT Length;
        USHORT MaximumLength;
        PWSTR  Buffer;
    } UNICODE_STRING, *PUNICODE_STRING;

    typedef NTSTATUS(NTAPI* pfnLdrLoadDll)(PWSTR, PULONG, PUNICODE_STRING, HMODULE*);

    struct LdrData {
        pfnLdrLoadDll fnLdrLoadDll;
        UNICODE_STRING unicodeStr;
        wchar_t path[260];
        HMODULE hMod;
        NTSTATUS status;
    };

#pragma runtime_checks("", off)
    static DWORD __stdcall LdrLoader(LPVOID param) {
        LdrData* data = reinterpret_cast<LdrData*>(param);
        if (!data || !data->fnLdrLoadDll) return 0;
        ULONG flags = 0;
        data->status = data->fnLdrLoadDll(nullptr, &flags, &data->unicodeStr, &data->hMod);
        return 0;
    }
    static void __stdcall LdrEnd() {}
#pragma runtime_checks("", restore)

    struct MapData {
        LPVOID base;
        HMODULE(WINAPI* pLoadLib)(LPCSTR);
        FARPROC(WINAPI* pGetProc)(HMODULE, LPCSTR);
        DWORD status;
    };

#pragma runtime_checks("", off)
    static DWORD __stdcall MapLoader(LPVOID param) {
        MapData* data = reinterpret_cast<MapData*>(param);
        if (!data || !data->base) return 0;

        BYTE* base = reinterpret_cast<BYTE*>(data->base);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);

        auto& importDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (importDir.Size > 0) {
            auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + importDir.VirtualAddress);
            while (desc->Name) {
                char* name = reinterpret_cast<char*>(base + desc->Name);
                HMODULE mod = data->pLoadLib(name);
                if (!mod) {
                    data->status = 1;
                    return 0;
                }

                auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
                auto* orig = reinterpret_cast<IMAGE_THUNK_DATA*>(base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));

                while (orig->u1.AddressOfData) {
                    if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) {
                        FARPROC addr = data->pGetProc(mod, reinterpret_cast<LPCSTR>(IMAGE_ORDINAL(orig->u1.Ordinal)));
                        if (!addr) {
                            data->status = 1;
                            return 0;
                        }
                        thunk->u1.Function = reinterpret_cast<ULONGLONG>(addr);
                    } else {
                        auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + orig->u1.AddressOfData);
                        FARPROC addr = data->pGetProc(mod, reinterpret_cast<LPCSTR>(ibn->Name));
                        if (!addr) {
                            data->status = 1;
                            return 0;
                        }
                        thunk->u1.Function = reinterpret_cast<ULONGLONG>(addr);
                    }
                    thunk++;
                    orig++;
                }
                desc++;
            }
        }

        auto& tlsDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
        if (tlsDir.Size > 0) {
            auto* tls = reinterpret_cast<IMAGE_TLS_DIRECTORY*>(base + tlsDir.VirtualAddress);
            if (tls->AddressOfCallBacks) {
                auto** cb = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(tls->AddressOfCallBacks);
                while (*cb) {
                    (*cb)(base, DLL_PROCESS_ATTACH, nullptr);
                    cb++;
                }
            }
        }

        if (nt->OptionalHeader.AddressOfEntryPoint) {
            using f_entry = BOOL(WINAPI*)(void*, DWORD, void*);
            auto* entry = reinterpret_cast<f_entry>(base + nt->OptionalHeader.AddressOfEntryPoint);
            if (!entry(base, DLL_PROCESS_ATTACH, nullptr)) {
                data->status = 2;
                return 0;
            }
        }

        data->status = 0;
        return 1;
    }
    static void __stdcall MapEnd() {}
#pragma runtime_checks("", restore)

    struct HookData {
        DWORD pid;
        DWORD tid;
        HWND hwnd;
    };

    static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lParam) {
        auto* data = reinterpret_cast<HookData*>(lParam);
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
        if (pid == data->pid) {
            data->tid = tid;
            data->hwnd = hwnd;
            if (IsWindowVisible(hwnd)) return FALSE;
        }
        return TRUE;
    }

    std::wstring Injector::Inject(DWORD pid, const std::wstring& dll, InjMethod method, const InjOpts& opts) {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return L"target process error: " + std::to_wstring(GetLastError());
        CloseHandle(h);

        Utils::SetPriv();

        switch (method) {
        case InjMethod::CRT: return InjectCRT(pid, dll, opts);
        case InjMethod::Hijack: return InjectHijack(pid, dll, opts);
        case InjMethod::APC: return InjectAPC(pid, dll, opts);
        case InjMethod::MMap: return InjectMMap(pid, dll, opts);
        case InjMethod::NtEx: return InjectNtEx(pid, dll, opts);
        case InjMethod::Ldr: return InjectLdr(pid, dll, opts);
        case InjMethod::EB: return InjectEB(pid, dll, opts);
        case InjMethod::Hook: return InjectHook(pid, dll, opts);
        default: return L"unknown method";
        }
    }

    std::wstring Injector::Launch(const std::wstring& exe, const std::wstring& dll, InjMethod method, const InjOpts& opts) {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = { 0 };

        if (!CreateProcessW(exe.c_str(), NULL, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
            return L"create process failed: " + std::to_wstring(GetLastError());
        }

        std::wstring err = L"";
        if (method == InjMethod::EB) {
            size_t size = (dll.size() + 1) * sizeof(wchar_t);
            LPVOID addr = VirtualAllocEx(pi.hProcess, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!addr) {
                err = L"alloc failed";
            } else {
                WriteProcessMemory(pi.hProcess, addr, dll.c_str(), size, NULL);
                LPVOID loadLib = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
                if (!QueueUserAPC((PAPCFUNC)loadLib, pi.hThread, (ULONG_PTR)addr)) {
                    err = L"apc failed";
                }
            }
        } else {
            err = Inject(pi.dwProcessId, dll, method, opts);
        }

        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return err;
    }

    std::wstring Injector::InjectCRT(DWORD pid, const std::wstring& dll, const InjOpts& opts) {
        HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProc) return L"open process failed";

        size_t size = (dll.size() + 1) * sizeof(wchar_t);
        LPVOID addr = VirtualAllocEx(hProc, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!addr) {
            CloseHandle(hProc);
            return L"alloc failed";
        }

        WriteProcessMemory(hProc, addr, dll.c_str(), size, NULL);
        LPVOID loadLib = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        HANDLE hTh = CreateRemoteThread(hProc, NULL, 0, (LPTHREAD_START_ROUTINE)loadLib, addr, 0, NULL);
        if (!hTh) {
            VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"thread failed";
        }

        WaitForSingleObject(hTh, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeThread(hTh, &exitCode);
        CloseHandle(hTh);

        if (opts.erasePE && exitCode != 0) {
            std::vector<BYTE> zero(1024, 0);
            WriteProcessMemory(hProc, (LPVOID)(uintptr_t)exitCode, zero.data(), zero.size(), NULL);
        }

        VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return exitCode == 0 ? L"load library failed" : L"";
    }

    std::wstring Injector::InjectNtEx(DWORD pid, const std::wstring& dll, const InjOpts& opts) {
        HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProc) return L"open process failed";

        size_t size = (dll.size() + 1) * sizeof(wchar_t);
        LPVOID addr = VirtualAllocEx(hProc, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!addr) {
            CloseHandle(hProc);
            return L"alloc failed";
        }

        WriteProcessMemory(hProc, addr, dll.c_str(), size, NULL);
        LPVOID loadLib = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");

        auto fnNtCreateThreadEx = (pfnNtCreateThreadEx)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx");
        if (!fnNtCreateThreadEx) {
            VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"fn missing";
        }

        HANDLE hTh = nullptr;
        ULONG flags = opts.hideTh ? TH_HIDE_DEBUG : 0;
        NTSTATUS status = fnNtCreateThreadEx(&hTh, THREAD_ALL_ACCESS, nullptr, hProc, (LPTHREAD_START_ROUTINE)loadLib, addr, flags, 0, 0, 0, nullptr);
        if (status != 0 || !hTh) {
            VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"thread failed status: " + std::to_wstring(status);
        }

        WaitForSingleObject(hTh, INFINITE);
        DWORD exitCode = 0;
        GetExitCodeThread(hTh, &exitCode);
        CloseHandle(hTh);

        if (opts.erasePE && exitCode != 0) {
            std::vector<BYTE> zero(1024, 0);
            WriteProcessMemory(hProc, (LPVOID)(uintptr_t)exitCode, zero.data(), zero.size(), NULL);
        }

        VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
        CloseHandle(hProc);
        return exitCode == 0 ? L"load library failed" : L"";
    }

    std::wstring Injector::InjectLdr(DWORD pid, const std::wstring& dll, const InjOpts& opts) {
        HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProc) return L"open process failed";

        auto fnLdrLoadDll = (pfnLdrLoadDll)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "LdrLoadDll");
        if (!fnLdrLoadDll) {
            CloseHandle(hProc);
            return L"fn missing";
        }

        LdrData lData = { 0 };
        lData.fnLdrLoadDll = fnLdrLoadDll;
        lData.unicodeStr.Length = (USHORT)(dll.size() * sizeof(wchar_t));
        lData.unicodeStr.MaximumLength = (USHORT)((dll.size() + 1) * sizeof(wchar_t));
        wcscpy_s(lData.path, dll.c_str());

        LPVOID pData = VirtualAllocEx(hProc, NULL, sizeof(LdrData), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!pData) {
            CloseHandle(hProc);
            return L"alloc failed";
        }

        lData.unicodeStr.Buffer = (PWSTR)((BYTE*)pData + offsetof(LdrData, path));
        WriteProcessMemory(hProc, pData, &lData, sizeof(LdrData), NULL);

        size_t funcSize = (uintptr_t)LdrEnd - (uintptr_t)LdrLoader;
        LPVOID pFunc = VirtualAllocEx(hProc, NULL, funcSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!pFunc) {
            VirtualFreeEx(hProc, pData, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"alloc failed";
        }

        WriteProcessMemory(hProc, pFunc, (LPVOID)LdrLoader, funcSize, NULL);

        HANDLE hTh = nullptr;
        auto fnNtCreateThreadEx = (pfnNtCreateThreadEx)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx");
        if (fnNtCreateThreadEx) {
            ULONG flags = opts.hideTh ? TH_HIDE_DEBUG : 0;
            fnNtCreateThreadEx(&hTh, THREAD_ALL_ACCESS, nullptr, hProc, (LPTHREAD_START_ROUTINE)pFunc, pData, flags, 0, 0, 0, nullptr);
        } else {
            hTh = CreateRemoteThread(hProc, NULL, 0, (LPTHREAD_START_ROUTINE)pFunc, pData, 0, NULL);
        }

        if (!hTh) {
            VirtualFreeEx(hProc, pFunc, 0, MEM_RELEASE);
            VirtualFreeEx(hProc, pData, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"thread failed";
        }

        WaitForSingleObject(hTh, INFINITE);
        CloseHandle(hTh);

        LdrData res = { 0 };
        ReadProcessMemory(hProc, pData, &res, sizeof(LdrData), NULL);

        VirtualFreeEx(hProc, pFunc, 0, MEM_RELEASE);
        VirtualFreeEx(hProc, pData, 0, MEM_RELEASE);

        if (opts.erasePE && res.hMod != nullptr) {
            std::vector<BYTE> zero(1024, 0);
            WriteProcessMemory(hProc, (LPVOID)res.hMod, zero.data(), zero.size(), NULL);
        }

        CloseHandle(hProc);
        if (res.status != 0 || res.hMod == nullptr) {
            return L"ldr failed status: " + std::to_wstring(res.status);
        }
        return L"";
    }

    std::wstring Injector::InjectEB(DWORD pid, const std::wstring& dll, const InjOpts& opts) {
        return L"early bird requires launch";
    }

    std::wstring Injector::InjectHook(DWORD pid, const std::wstring& dll, const InjOpts& opts) {
        HookData data = { pid, 0, nullptr };
        EnumWindows(EnumProc, (LPARAM)&data);

        if (data.tid == 0 || data.hwnd == nullptr) return L"gui thread missing";

        HMODULE hLocal = LoadLibraryW(dll.c_str());
        if (!hLocal) return L"local load failed";

        HOOKPROC pHook = (HOOKPROC)GetProcAddress(hLocal, "NextHook");
        if (!pHook) {
            FreeLibrary(hLocal);
            return L"export missing";
        }

        HHOOK hHook = SetWindowsHookExW(WH_CALLWNDPROC, pHook, hLocal, data.tid);
        if (!hHook) {
            FreeLibrary(hLocal);
            return L"hook failed: " + std::to_wstring(GetLastError());
        }

        SendMessageW(data.hwnd, WM_NULL, 0, 0);
        UnhookWindowsHookEx(hHook);
        FreeLibrary(hLocal);
        return L"";
    }

    std::wstring Injector::InjectAPC(DWORD pid, const std::wstring& dll, const InjOpts& opts) {
        HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProc) return L"open process failed";

        size_t size = (dll.size() + 1) * sizeof(wchar_t);
        LPVOID addr = VirtualAllocEx(hProc, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!addr) {
            CloseHandle(hProc);
            return L"alloc failed";
        }

        WriteProcessMemory(hProc, addr, dll.c_str(), size, NULL);
        LPVOID loadLib = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");

        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        bool queued = false;
        if (hSnap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te = { sizeof(te) };
            if (Thread32First(hSnap, &te)) {
                do {
                    if (te.th32OwnerProcessID == pid) {
                        HANDLE hTh = OpenThread(THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
                        if (hTh) {
                            if (QueueUserAPC((PAPCFUNC)loadLib, hTh, (ULONG_PTR)addr)) queued = true;
                            CloseHandle(hTh);
                        }
                    }
                } while (Thread32Next(hSnap, &te));
            }
            CloseHandle(hSnap);
        }

        if (!queued) {
            VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"no threads";
        }

        CloseHandle(hProc);
        return L"";
    }

    std::wstring Injector::InjectHijack(DWORD pid, const std::wstring& dll, const InjOpts& opts) {
        HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProc) return L"open process failed";

        size_t size = (dll.size() + 1) * sizeof(wchar_t);
        LPVOID addr = VirtualAllocEx(hProc, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!addr) {
            CloseHandle(hProc);
            return L"alloc failed";
        }
        WriteProcessMemory(hProc, addr, dll.c_str(), size, NULL);

        DWORD tid = 0;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te = { sizeof(te) };
            if (Thread32First(hSnap, &te)) {
                do {
                    if (te.th32OwnerProcessID == pid) {
                        tid = te.th32ThreadID;
                        break;
                    }
                } while (Thread32Next(hSnap, &te));
            }
            CloseHandle(hSnap);
        }

        if (tid == 0) {
            VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"no threads";
        }

        HANDLE hTh = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, tid);
        if (!hTh) {
            VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"open thread failed";
        }

        SuspendThread(hTh);

        CONTEXT ctx = { 0 };
        ctx.ContextFlags = CONTEXT_CONTROL;
        if (!GetThreadContext(hTh, &ctx)) {
            ResumeThread(hTh);
            CloseHandle(hTh);
            VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"context failed";
        }

#ifdef _WIN64
        DWORD64 originalRip = ctx.Rip;
        BYTE shellcode[] = {
            0x50, 0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52, 0x41, 0x53,
            0x48, 0x83, 0xEC, 0x28,
            0x48, 0xB9, 0,0,0,0,0,0,0,0,
            0x48, 0xB8, 0,0,0,0,0,0,0,0,
            0xFF, 0xD0,
            0x48, 0x83, 0xC4, 0x28,
            0x41, 0x5B, 0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59,
            0x48, 0x83, 0xEC, 0x08,
            0x48, 0x89, 0x04, 0x24,
            0x48, 0xB8, 0,0,0,0,0,0,0,0,
            0x48, 0x87, 0x04, 0x24,
            0xC3
        };
        LPVOID pLoadLibrary = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        *reinterpret_cast<LPVOID*>(&shellcode[17]) = addr;
        *reinterpret_cast<LPVOID*>(&shellcode[27]) = pLoadLibrary;
        *reinterpret_cast<DWORD64*>(&shellcode[57]) = originalRip;
#else
        DWORD originalEip = ctx.Eip;
        BYTE shellcode[] = {
            0x60,
            0x68, 0,0,0,0,
            0xB8, 0,0,0,0,
            0xFF, 0xD0,
            0x61,
            0x68, 0,0,0,0,
            0xC3
        };
        LPVOID pLoadLibrary = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        *reinterpret_cast<DWORD*>(&shellcode[2]) = reinterpret_cast<DWORD>(addr);
        *reinterpret_cast<DWORD*>(&shellcode[7]) = reinterpret_cast<DWORD>(pLoadLibrary);
        *reinterpret_cast<DWORD*>(&shellcode[14]) = originalEip;
#endif

        LPVOID pShell = VirtualAllocEx(hProc, NULL, sizeof(shellcode), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!pShell) {
            ResumeThread(hTh);
            CloseHandle(hTh);
            VirtualFreeEx(hProc, addr, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"alloc failed";
        }

        WriteProcessMemory(hProc, pShell, shellcode, sizeof(shellcode), NULL);

#ifdef _WIN64
        ctx.Rip = reinterpret_cast<DWORD64>(pShell);
#else
        ctx.Eip = reinterpret_cast<DWORD>(pShell);
#endif

        SetThreadContext(hTh, &ctx);
        ResumeThread(hTh);
        CloseHandle(hTh);
        CloseHandle(hProc);
        return L"";
    }

    std::wstring Injector::InjectMMap(DWORD pid, const std::wstring& dll, const InjOpts& opts) {
        std::ifstream file(dll, std::ios::binary | std::ios::ate);
        if (!file.is_open()) return L"open dll failed";

        size_t size = file.tellg();
        std::vector<BYTE> bytes(size);
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        file.close();

        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return L"invalid dos signature";

        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(bytes.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return L"invalid nt signature";

#ifdef _WIN64
        if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) return L"invalid machine type";
#else
        if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return L"invalid machine type";
#endif

        HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProc) return L"open process failed";

        DWORD imgSize = nt->OptionalHeader.SizeOfImage;
        LPVOID targetBase = VirtualAllocEx(hProc, NULL, imgSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!targetBase) {
            CloseHandle(hProc);
            return L"alloc failed";
        }

        std::vector<BYTE> local(imgSize, 0);
        memcpy(local.data(), bytes.data(), nt->OptionalHeader.SizeOfHeaders);

        auto* sec = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            if (sec[i].SizeOfRawData > 0) {
                memcpy(local.data() + sec[i].VirtualAddress, bytes.data() + sec[i].PointerToRawData, sec[i].SizeOfRawData);
            }
        }

        ULONGLONG delta = reinterpret_cast<ULONGLONG>(targetBase) - nt->OptionalHeader.ImageBase;
        if (delta != 0) {
            auto& relocDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
            if (relocDir.Size > 0) {
                auto* block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(local.data() + relocDir.VirtualAddress);
                while (block->VirtualAddress != 0 && block->SizeOfBlock > 0) {
                    DWORD cnt = (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                    WORD* entry = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(block) + sizeof(IMAGE_BASE_RELOCATION));
                    for (DWORD j = 0; j < cnt; ++j) {
                        WORD type = entry[j] >> 12;
                        WORD offset = entry[j] & 0x0FFF;
#ifdef _WIN64
                        if (type == IMAGE_REL_BASED_DIR64) {
                            ULONGLONG* addr = reinterpret_cast<ULONGLONG*>(local.data() + block->VirtualAddress + offset);
                            *addr += delta;
                        }
#else
                        if (type == IMAGE_REL_BASED_HIGHLOW) {
                            DWORD* addr = reinterpret_cast<DWORD*>(local.data() + block->VirtualAddress + offset);
                            *addr += (DWORD)delta;
                        }
#endif
                    }
                    block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(reinterpret_cast<BYTE*>(block) + block->SizeOfBlock);
                }
            }
        }

        if (opts.clearSec) {
            auto* secL = IMAGE_FIRST_SECTION(reinterpret_cast<IMAGE_NT_HEADERS*>(local.data() + dos->e_lfanew));
            for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
                ZeroMemory(secL[i].Name, IMAGE_SIZEOF_SHORT_NAME);
            }
        }

        if (opts.eraseExp) {
            auto* ntL = reinterpret_cast<IMAGE_NT_HEADERS*>(local.data() + dos->e_lfanew);
            auto& exp = ntL->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
            exp.VirtualAddress = 0;
            exp.Size = 0;
        }

        if (!WriteProcessMemory(hProc, targetBase, local.data(), imgSize, NULL)) {
            VirtualFreeEx(hProc, targetBase, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"write memory failed";
        }

        MapData mData = { targetBase, LoadLibraryA, GetProcAddress, 999 };
        LPVOID pData = VirtualAllocEx(hProc, NULL, sizeof(MapData), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!pData) {
            VirtualFreeEx(hProc, targetBase, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"alloc failed";
        }
        WriteProcessMemory(hProc, pData, &mData, sizeof(MapData), NULL);

        size_t funcSize = reinterpret_cast<uintptr_t>(MapEnd) - reinterpret_cast<uintptr_t>(MapLoader);
        LPVOID pFunc = VirtualAllocEx(hProc, NULL, funcSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!pFunc) {
            VirtualFreeEx(hProc, pData, 0, MEM_RELEASE);
            VirtualFreeEx(hProc, targetBase, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"alloc failed";
        }
        WriteProcessMemory(hProc, pFunc, (LPVOID)MapLoader, funcSize, NULL);

        HANDLE hTh = nullptr;
        auto fnNtCreateThreadEx = (pfnNtCreateThreadEx)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateThreadEx");
        if (fnNtCreateThreadEx) {
            ULONG flags = opts.hideTh ? TH_HIDE_DEBUG : 0;
            fnNtCreateThreadEx(&hTh, THREAD_ALL_ACCESS, nullptr, hProc, (LPTHREAD_START_ROUTINE)pFunc, pData, flags, 0, 0, 0, nullptr);
        } else {
            hTh = CreateRemoteThread(hProc, NULL, 0, (LPTHREAD_START_ROUTINE)pFunc, pData, 0, NULL);
        }

        if (!hTh) {
            VirtualFreeEx(hProc, pFunc, 0, MEM_RELEASE);
            VirtualFreeEx(hProc, pData, 0, MEM_RELEASE);
            VirtualFreeEx(hProc, targetBase, 0, MEM_RELEASE);
            CloseHandle(hProc);
            return L"thread failed";
        }

        WaitForSingleObject(hTh, INFINITE);
        CloseHandle(hTh);

        VirtualFreeEx(hProc, pFunc, 0, MEM_RELEASE);

        MapData res = { 0 };
        ReadProcessMemory(hProc, pData, &res, sizeof(MapData), NULL);
        VirtualFreeEx(hProc, pData, 0, MEM_RELEASE);

        if (opts.erasePE) {
            std::vector<BYTE> zero(nt->OptionalHeader.SizeOfHeaders, 0);
            WriteProcessMemory(hProc, targetBase, zero.data(), zero.size(), NULL);
        }

        CloseHandle(hProc);
        if (res.status == 1) return L"manual map import error";
        if (res.status == 2) return L"manual map entry error";
        if (res.status != 0) return L"manual map unknown error";
        return L"";
    }
}
