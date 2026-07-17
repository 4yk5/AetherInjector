#include "ui.hpp"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "style.hpp"
#include "injector/injector.hpp"
#include "injector/utils.hpp"

#include <commdlg.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <algorithm>

namespace Aether {
    HWND UI::hwnd = nullptr;

    static bool g_Close = false;
    static std::string g_Dll = "";
    static DWORD g_Pid = 0;
    static std::wstring g_ProcName = L"";
    static int g_Method = 3;

    static bool g_PE = true;
    static bool g_Mod = false;
    static bool g_Th = true;
    static bool g_Sec = true;
    static bool g_Exp = true;

    static char g_Filter[128] = "";
    static bool g_ShowSelector = false;
    static char g_Exe[MAX_PATH] = "C:\\Windows\\notepad.exe";

    static std::vector<std::string> g_Logs;
    static void AddLog(const std::string& log) {
        g_Logs.push_back(log);
        if (g_Logs.size() > 50) g_Logs.erase(g_Logs.begin());
    }

    static float g_Mem = 0.0f;
    static float g_MaxMem = 100.0f;
    static int g_Ticks = 0;
    static int g_Tab = 0;

    static void DrawTop();
    static void DrawInj();
    static void DrawStealth();
    static void DrawConsole();
    static void DrawSelector();
    static void FindDll();
    static void FindExe();
    static void UpdateMem();

    void UI::Init() {
        if (hwnd) {
            LONG style = GetWindowLong(hwnd, GWL_STYLE);
            style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
            SetWindowLong(hwnd, GWL_STYLE, style);

            RECT rect;
            GetWindowRect(hwnd, &rect);
            HRGN region = CreateRoundRectRgn(0, 0, rect.right - rect.left, rect.bottom - rect.top, 15, 15);
            SetWindowRgn(hwnd, region, TRUE);
        }

        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->AddFontFromFileTTF("inter.ttf", 16.0f) || 
        io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f);

        SetTheme();

        ImGuiStyle& style = ImGui::GetStyle();
        style.ChildBorderSize = 1.0f;
        style.FrameBorderSize = 1.0f;
        style.WindowBorderSize = 0.0f;
        style.WindowRounding = 12.0f;
        style.ChildRounding = 12.0f;
        style.FrameRounding = 6.0f;
        style.PopupRounding = 12.0f;
        style.ScrollbarRounding = 8.0f;
        style.GrabRounding = 6.0f;
        style.WindowPadding = ImVec2(20, 20);
        style.FramePadding = ImVec2(12, 10);
        style.ItemSpacing = ImVec2(10, 10);

        AddLog("system: initialized.");
    }

    void UI::Shutdown() {}
    bool UI::ShouldClose() { return g_Close; }

    void UI::Render() {
        g_Ticks++;
        if (g_Ticks >= 30) {
            UpdateMem();
            g_Ticks = 0;
        }

        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);

        ImGui::Begin("AetherMain", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

        DrawTop();
        ImGui::Dummy(ImVec2(0, 8.0f));

        float footerHeight = 22.0f;
        float contentHeight = ImGui::GetContentRegionAvail().y - footerHeight - ImGui::GetStyle().ItemSpacing.y;

        ImGui::BeginChild("ContentArea", ImVec2(0, contentHeight), false, ImGuiWindowFlags_NoScrollbar);
        if (g_Tab == 0) DrawInj();
        else if (g_Tab == 1) DrawStealth();
        else if (g_Tab == 2) DrawConsole();
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 2.0f));
        ImGui::TextDisabled("Aether Injector v1.0.0");

        float rx = ImGui::GetWindowWidth() - ImGui::CalcTextSize("owner: 4yk5").x - ImGui::GetStyle().WindowPadding.x;
        ImGui::SameLine(rx);
        ImGui::TextDisabled("owner: 4yk5");

        ImGui::End();
        DrawSelector();
    }

    static void DrawTop() {
        ImGui::BeginChild("TopBar", ImVec2(0, 50), false, ImGuiWindowFlags_NoScrollbar);
        bool hovered = false;

        auto DrawTabBtn = [&](const char* label, bool active) {
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.35f, 0.85f, 0.15f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.40f, 0.70f, 1.00f, 1.00f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            }
            bool clicked = ImGui::Button(label, ImVec2(100, 36));
            if (ImGui::IsItemHovered()) hovered = true;
            ImGui::PopStyleColor(2);
            return clicked;
        };

        ImGui::SetCursorPosY(7);
        if (DrawTabBtn("injector##tab0", g_Tab == 0)) g_Tab = 0; ImGui::SameLine(0, 10);
        if (DrawTabBtn("stealth##tab1", g_Tab == 1)) g_Tab = 1; ImGui::SameLine(0, 10);
        if (DrawTabBtn("console##tab2", g_Tab == 2)) g_Tab = 2;

        ImGui::SameLine(ImGui::GetWindowWidth() - 42);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.2f, 0.2f, 0.3f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        if (ImGui::Button("X##close", ImVec2(36, 36))) g_Close = true;
        if (ImGui::IsItemHovered()) hovered = true;
        ImGui::PopStyleColor(3);

        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.MousePos.y >= 0.0f && io.MousePos.y < 50.0f && !hovered) {
            ReleaseCapture();
            SendMessage(UI::hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }

        ImGui::EndChild();
    }

    static void DrawInj() {
        float halfWidth = (ImGui::GetContentRegionAvail().x / 2.0f) - (ImGui::GetStyle().ItemSpacing.x / 2.0f);

        ImGui::BeginChild("LeftCol", ImVec2(halfWidth, ImGui::GetContentRegionAvail().y), false, ImGuiWindowFlags_NoScrollbar);
        float cardHeight = ImGui::GetContentRegionAvail().y - 55.0f - 12.0f - ImGui::GetStyle().ItemSpacing.y * 2.0f;
        ImGui::BeginChild("CardConfig", ImVec2(0, cardHeight), true, ImGuiWindowFlags_NoScrollbar);

        float btnWidth = 95.0f;
        float inputWidth = ImGui::GetContentRegionAvail().x - btnWidth - ImGui::GetStyle().ItemSpacing.x;

        ImGui::TextDisabled("target binary");
        char dllPathBuf[512];
        strcpy_s(dllPathBuf, g_Dll.c_str());

        ImGui::SetNextItemWidth(inputWidth);
        if (ImGui::InputText("##dll_input", dllPathBuf, sizeof(dllPathBuf))) g_Dll = dllPathBuf;
        ImGui::SameLine();
        if (ImGui::Button("browse##dll", ImVec2(btnWidth, 36))) FindDll();

        ImGui::Dummy(ImVec2(0, 12.0f));

        ImGui::TextDisabled("injection method");
        const char* methods[] = {
            "standard thread", "context hijack", "queueuserapc",
            "manual mapping", "ntcreatethreadex", "ldrloaddll",
            "early bird apc", "setwindowshookex"
        };
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##method_combo", &g_Method, methods, IM_ARRAYSIZE(methods));

        ImGui::Dummy(ImVec2(0, 12.0f));

        if (g_Method == 6) {
            ImGui::TextDisabled("suspended executable");
            ImGui::SetNextItemWidth(inputWidth);
            ImGui::InputText("##exe_input", g_Exe, sizeof(g_Exe));
            ImGui::SameLine();
            if (ImGui::Button("browse##exe", ImVec2(btnWidth, 36))) FindExe();
        } else {
            ImGui::TextDisabled("active process");
            std::string procNameStr = Utils::W2S(g_ProcName);
            if (g_Pid != 0) procNameStr += " [" + std::to_string(g_Pid) + "]";

            ImGui::SetNextItemWidth(inputWidth);
            ImGui::InputText("##proc_input", (char*)procNameStr.c_str(), procNameStr.size(), ImGuiInputTextFlags_ReadOnly);
            ImGui::SameLine();
            if (ImGui::Button("select##proc", ImVec2(btnWidth, 36))) g_ShowSelector = true;
        }
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0, 12.0f));

        if (ImGui::Button("INITIALIZE INJECTION##inject_btn", ImVec2(-1, 55))) {
            if (g_Dll.empty()) AddLog("error: dll path missing.");
            else if (g_Method != 6 && g_Pid == 0) AddLog("error: target process not selected.");
            else {
                AddLog("initializing injection protocol...");
                std::wstring wdll = Utils::S2W(g_Dll);
                InjOpts opts = { g_PE, g_Mod, g_Th, g_Sec, g_Exp };
                std::wstring res = (g_Method == 6) ?
                    Injector::Launch(Utils::S2W(g_Exe), wdll, static_cast<InjMethod>(g_Method), opts) :
                    Injector::Inject(g_Pid, wdll, static_cast<InjMethod>(g_Method), opts);

                if (res.empty()) AddLog("status: successfully injected.");
                else AddLog("error: " + Utils::W2S(res));
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("RightCol", ImVec2(0, ImGui::GetContentRegionAvail().y), false, ImGuiWindowFlags_NoScrollbar);

        ImGui::BeginChild("CardMemory", ImVec2(0, 120), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::TextDisabled("memory allocation stream");
        ImGui::Dummy(ImVec2(0, 8.0f));

        char overlayLabel[64];
        if (g_Pid != 0 && g_Method != 6) {
            sprintf_s(overlayLabel, "%.1f mb allocated", g_Mem);
            ImGui::ProgressBar(g_Mem / g_MaxMem, ImVec2(-1, 32), overlayLabel);
        } else {
            ImGui::ProgressBar(0.0f, ImVec2(-1, 32), "awaiting target");
        }
        ImGui::EndChild();

        ImGui::Dummy(ImVec2(0, 12.0f));

        ImGui::BeginChild("CardMiniConsole", ImVec2(0, 0), true);
        ImGui::TextDisabled("recent events");
        ImGui::Dummy(ImVec2(0, 5.0f));
        for (auto it = g_Logs.rbegin(); it != g_Logs.rend(); ++it) {
            if (it->find("error:") != std::string::npos) ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), it->c_str());
            else ImGui::TextDisabled(it->c_str());
        }
        ImGui::EndChild();

        ImGui::EndChild();
    }

    static void DrawStealth() {
        ImGui::BeginChild("CardStealth", ImVec2(0, 0), true);
        ImGui::TextDisabled("memory evasion parameters");
        ImGui::Dummy(ImVec2(0, 12.0f));

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));
        ImGui::Checkbox("erase pe headers##cb1", &g_PE); ImGui::Dummy(ImVec2(0, 4.0f));
        ImGui::Checkbox("hide from peb (ldr)##cb2", &g_Mod); ImGui::Dummy(ImVec2(0, 4.0f));
        ImGui::Checkbox("hide thread from debugger##cb3", &g_Th); ImGui::Dummy(ImVec2(0, 4.0f));
        ImGui::Checkbox("clear section metadata##cb4", &g_Sec); ImGui::Dummy(ImVec2(0, 4.0f));
        ImGui::Checkbox("erase export tables##cb5", &g_Exp);
        ImGui::PopStyleVar();

        ImGui::EndChild();
    }

    static void DrawConsole() {
        ImGui::BeginChild("CardConsole", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& log : g_Logs) {
            if (log.find("error:") != std::string::npos)
                ImGui::TextColored(ImVec4(1.00f, 0.35f, 0.35f, 1.0f), log.c_str());
            else if (log.find("success:") != std::string::npos)
                ImGui::TextColored(ImVec4(0.30f, 0.85f, 0.45f, 1.0f), log.c_str());
            else if (log.find("status:") != std::string::npos || log.find("initializing") != std::string::npos)
                ImGui::TextColored(ImVec4(0.20f, 0.60f, 1.00f, 1.0f), log.c_str());
            else
                ImGui::TextDisabled(log.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
    }

    static void DrawSelector() {
        if (!g_ShowSelector) return;

        ImGui::OpenPopup("process selector##modal");
        ImGui::SetNextWindowSize(ImVec2(650, 520), ImGuiCond_Appearing);

        if (ImGui::BeginPopupModal("process selector##modal", &g_ShowSelector, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar)) {
            ImGui::TextDisabled("target application selection");
            ImGui::Dummy(ImVec2(0, 8.0f));

            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##filter_search", "search process...", g_Filter, sizeof(g_Filter));
            ImGui::Dummy(ImVec2(0, 8.0f));

            float reserveHeight = 35.0f + ImGui::GetStyle().ItemSpacing.y + 4.0f;
            ImGui::BeginChild("ProcContainer", ImVec2(0, ImGui::GetContentRegionAvail().y - reserveHeight), true);
            std::vector<Proc> list = Utils::GetProcs();
            std::sort(list.begin(), list.end(), [](const Proc& a, const Proc& b) { return a.name < b.name; });

            std::string filterStr = g_Filter;
            std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

            for (const auto& proc : list) {
                std::string name = Utils::W2S(proc.name);
                std::string nameL = name;
                std::transform(nameL.begin(), nameL.end(), nameL.begin(), ::tolower);

                if (filterStr.empty() || nameL.find(filterStr) != std::string::npos) {
                    std::string label = name + " [" + std::to_string(proc.pid) + "]";
                    std::transform(label.begin(), label.end(), label.begin(), ::tolower);

                    std::string id = label + "##" + std::to_string(proc.pid);
                    if (ImGui::Selectable(id.c_str(), g_Pid == proc.pid)) {
                        g_Pid = proc.pid;
                        g_ProcName = proc.name;
                        g_ShowSelector = false;
                        AddLog("target locked: " + name);
                        g_Mem = 0.0f;
                    }
                }
            }
            ImGui::EndChild();
            ImGui::Dummy(ImVec2(0, 4.0f));

            if (ImGui::Button("close##close_modal", ImVec2(-1, 35))) g_ShowSelector = false;
            ImGui::EndPopup();
        }
    }

    static void FindDll() {
        OPENFILENAMEA ofn;
        char file[MAX_PATH] = { 0 };
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = UI::hwnd;
        ofn.lpstrFile = file;
        ofn.nMaxFile = sizeof(file);
        ofn.lpstrFilter = "dll files (*.dll)\0*.dll\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        if (GetOpenFileNameA(&ofn)) g_Dll = file;
    }

    static void FindExe() {
        OPENFILENAMEA ofn;
        char file[MAX_PATH] = { 0 };
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = UI::hwnd;
        ofn.lpstrFile = file;
        ofn.nMaxFile = sizeof(file);
        ofn.lpstrFilter = "executable files (*.exe)\0*.exe\0";
        ofn.nFilterIndex = 1;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
        if (GetOpenFileNameA(&ofn)) strcpy_s(g_Exe, sizeof(g_Exe), file);
    }

    static void UpdateMem() {
        if (g_Pid == 0 || g_Method == 6) {
            g_Mem = 0.0f;
            return;
        }
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, g_Pid);
        if (h) {
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc))) {
                g_Mem = static_cast<float>(pmc.WorkingSetSize) / (1024.0f * 1024.0f);
                if (g_Mem > g_MaxMem) g_MaxMem = g_Mem * 1.5f;
            }
            CloseHandle(h);
        }
    }
}