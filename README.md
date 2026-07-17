<div align="center">
  <h1>🌌 Aether Injector v1.0.0</h1>
  <p><strong>Advanced Windows DLL Injection with Stealth Evasion</strong></p>
  
  <br>
  <img src="assets/demo.gif" alt="Aether Injector Preview" width="700">
  
  <br><br>
  
  <p>
    <a href="https://github.com/4yk5/AetherInjector/issues">Report Bug</a> ·
    <a href="https://github.com/4yk5/AetherInjector/issues">Request Feature</a>
  </p>
</div>

<hr>
<img width="816" height="635" alt="Screenshot_1" src="https://github.com/user-attachments/assets/045c7836-9b27-40e8-a8e3-82789e936f6b" />


A modern, high-performance, and stealth-focused Windows DLL Injector built with **C++20**, **DirectX 11**, and **Dear ImGui**. Featuring a premium glassmorphic Slate Sapphire UI, this tool is designed for advanced memory manipulation and testing.

---

### 👑 Owner & Lead Developer
This project is fully maintained and owned by **[4yk5](https://github.com/4yk5)**. 
> *"Developing stealth and security tools for the modern age."* 🛡️

---

## ⚡ Key Features

### 🚀 1. 8 Advanced Injection Methods
*   **Standard Thread**: Classic `CreateRemoteThread` DLL loading.
*   **Context Hijack**: Suspends, redirects, and resumes target threads.
*   **QueueUserAPC**: Enqueues asynchronous procedure calls to trigger loading.
*   **Manual Mapping**: Completely parses and maps DLL headers, sections, and imports manually in memory (the ultimate bypass).
*   **NtCreateThreadEx**: Employs direct system calls to create threads.
*   **LdrLoadDll**: Native loader API injection bypassing `LoadLibrary` API hooks.
*   **Early Bird APC**: Spawns a target suspended, schedules APC, and runs before entrypoint executes.
*   **SetWindowsHookEx**: Exploits window message hooks on GUI apps.

### 🕵️ 2. 5 Stealth Evasion Parameters
*   **Erase PE Headers**: Overwrites the PE signatures in memory to evade scanners.
*   **Hide from PEB (Ldr)**: Unlinks module links from the target's loader list.
*   **Hide Thread from Debugger**: Hides injected threads from active debugger attach commands.
*   **Clear Section Metadata**: Clears section names (`.text`, `.data`) to bypass static heuristic engines.
*   **Erase Export Tables**: Erases dll export structures for manual mapped images.

### 🎨 3. UI/UX & Real-time Telemetry
*   **Memory Allocation Stream**: A graphical progress bar displaying target RAM allocation.
*   **Smart Process Selector**: Fast modal process selector list equipped with lowercase regex filtering.
*   **Logging Console**: Divided and colored log screen mapping system status, warnings, and errors.
*   **Aesthetics**: Sleek glassmorphic Sapphire theme with glowing Electric Blue highlights.

---

## 🛠️ Build Instructions

### Prerequisites
*   Windows 10 / 11 🪟
*   Visual Studio 2022 (with "Desktop development with C++" workload) 💻
*   CMake 3.15 or newer ⚙️

### Building from Source
1. Clone the repository:
   ```bash
   git clone https://github.com/4yk5/AetherInjector.git
   ```
2. Configure using CMake:
   ```bash
   cmake -B build
   ```
3. Compile in Release mode:
   ```bash
   cmake --build build --config Release
   ```
4. Find the binary at `build/Release/AetherInjector.exe`. Run as Administrator! 🔑
