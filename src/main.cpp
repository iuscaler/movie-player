#include <windows.h>

#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>

#include <exception>
#include <string>
#include <vector>

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

std::wstring PickVideoFile() {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter =
        L"동영상 파일\0*.mp4;*.mkv;*.avi;*.mov;*.wmv;*.webm;*.ts;*.m4v\0"
        L"모든 파일\0*.*\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrTitle = L"재생할 동영상 선택";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    return GetOpenFileNameW(&dialog) ? std::wstring(buffer.data()) : std::wstring();
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
        path = PickVideoFile();
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
