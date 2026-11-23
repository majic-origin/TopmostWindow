#include "app.h"
#include <objbase.h>

// 运行时开启 Per-Monitor V2 DPI 感知（不依赖清单文件），保证覆盖层坐标与真实像素一致
static void EnableDpiAwareness()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    typedef BOOL (WINAPI* SetCtxFn)(HANDLE);
    auto setCtx = (SetCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (setCtx) {
        if (setCtx((HANDLE)-4)) return;   // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    }
    typedef BOOL (WINAPI* SetFn)(void);
    if (auto setLegacy = (SetFn)GetProcAddress(user32, "SetProcessDPIAware"))
        setLegacy();
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmdLine, int)
{
    EnableDpiAwareness();

    // 单实例
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\TopMostDesk_SingleInstance_v1");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"TopMostDesk 已在运行，请查看系统托盘图标。",
                    L"TopMostDesk", MB_OK | MB_ICONINFORMATION);
        CloseHandle(mutex);
        return 0;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    (void)hr;

    int code = 0;
    {
        int testMode = 0;
        if (cmdLine && wcsstr(cmdLine, L"--test-hotkey")) testMode = 1;
        else if (cmdLine && wcsstr(cmdLine, L"--test-input")) testMode = 2;
        else if (cmdLine && wcsstr(cmdLine, L"--test-color")) testMode = 4;
        App app(inst, testMode);
        if (!app.Init()) {
            MessageBoxW(nullptr, L"初始化失败：无法创建主窗口。", L"TopMostDesk", MB_OK | MB_ICONERROR);
            code = 1;
        } else {
            code = app.Run();
        }
    }

    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return code;
}
