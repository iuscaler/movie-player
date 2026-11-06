#include <windows.h>

#include <objbase.h>
#include <shellapi.h>

#include <exception>
#include <string>

#include "FileDialogs.h"
#include "MainWindow.h"

namespace {

std::wstring PathFromCommandLine() {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::wstring path;
    if (arguments && count > 1) {
        path = arguments[1];
    }
    LocalFree(arguments);
    return path;
}

int RunMessageLoop() {
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    int exitCode = 0;
    std::wstring path = PathFromCommandLine();
    if (path.empty()) {
        path = PickVideoFile(nullptr);
    }
    if (!path.empty()) {
        try {
            MainWindow window(instance);
            window.Show(showCommand);
            window.Open(path);
            exitCode = RunMessageLoop();
        } catch (const std::exception& error) {
            MessageBoxA(nullptr, error.what(), "Movie Player", MB_OK | MB_ICONERROR);
            exitCode = 1;
        }
    }

    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
    return exitCode;
}
