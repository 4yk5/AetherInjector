#pragma once
#include <windows.h>
#include <d3d11.h>
#include <string>

namespace Aether {
    class UI {
    public:
        static void Init();
        static void Render();
        static void Shutdown();
        static bool ShouldClose();
        static HWND hwnd;
    };
}
