#include <windows.h>

#include <objbase.h>
#include <shellapi.h>

#include <exception>
#include <string>

#include "FileDialogs.h"
#include "MainWindow.h"

namespace {

constexpr wchar_t kSingleInstanceMutex[] = L"Local\\MoviePlayer.SingleInstance";
constexpr DWORD kFindWindowTimeoutMs = 3000;
constexpr UINT kSendTimeoutMs = 5000;

std::wstring PathFromCommandLine() {
    int count = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::wstring path;
    if (arguments && count > 1) {
        path = arguments[1];
    }
    LocalFree(arguments);

    // A relative path means nothing to another copy of the app, which has its own
    // working directory. URLs and the like are left alone.
    if (!path.empty() && path.find(L"://") == std::wstring::npos) {
        std::wstring full(32768, L'\0');
        const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (length > 0 && length < full.size()) {
            full.resize(length);
            path = std::move(full);
        }
    }
    return path;
}

// Hands the path to a copy of the app that's already running. Returns false if
// there isn't one to hand it to, in which case this copy should carry on.
bool ForwardToRunningInstance(const std::wstring& path) {
    HWND window = nullptr;
    const ULONGLONG deadline = GetTickCount64() + kFindWindowTimeoutMs;
    // The other copy may have only just started, and not have its window yet.
    while (!(window = FindWindowW(MainWindow::kClassName, nullptr)) && GetTickCount64() < deadline) {
        Sleep(50);
    }
    if (!window) {
        return false;
    }

    // Only a process the user just started may take the foreground, so pass that on.
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    AllowSetForegroundWindow(processId);

    COPYDATASTRUCT data{};
    data.dwData = MainWindow::kOpenPathMessageId;
    data.cbData = static_cast<DWORD>(path.size() * sizeof(wchar_t));
    data.lpData = const_cast<wchar_t*>(path.data());
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG,
                               kSendTimeoutMs, &result) != 0 && result != 0;
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

    // Kept open until the process ends: its existence is what marks a copy as running.
    HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, kSingleInstanceMutex);
    if (GetLastError() == ERROR_ALREADY_EXISTS && ForwardToRunningInstance(path)) {
        CloseHandle(instanceMutex);
        if (SUCCEEDED(comResult)) {
            CoUninitialize();
        }
        return 0;
    }

    if (path.empty()) {
        path = PickVideoFile(nullptr);
    }
    if (!path.empty()) {
        try {
            MainWindow window(instance, LoadSettings());
            window.Show(showCommand);
            window.Open(path);
            exitCode = RunMessageLoop();
        } catch (const std::exception& error) {
            MessageBoxA(nullptr, error.what(), "Movie Player", MB_OK | MB_ICONERROR);
            exitCode = 1;
        }
    }

    if (instanceMutex) {
        CloseHandle(instanceMutex);
    }
    if (SUCCEEDED(comResult)) {
        CoUninitialize();
    }
    return exitCode;
}
