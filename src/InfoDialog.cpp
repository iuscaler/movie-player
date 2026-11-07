#include "InfoDialog.h"

#include <commctrl.h>

#include <string>
#include <utility>
#include <vector>

#include "PlaybackInfo.h"
#include "Resource.h"

namespace {

constexpr UINT WM_DPI_CHANGE_DONE = WM_APP + 1;
constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kRefreshMs = 1000;

class InfoDialog {
public:
    InfoDialog(HWND owner, const MpvPlayer& player, const PlaybackState& state, KeyChord closeKey)
        : owner_(owner), player_(player), state_(state), closeKey_(closeKey) {}

    void Run() {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&controls);
        // A dialog's own message loop handles the keyboard, and this is the way into it.
        active_ = this;
        HHOOK hook = SetWindowsHookExW(WH_MSGFILTER, &KeyFilter, nullptr, GetCurrentThreadId());
        DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_INFO), owner_, &Proc, reinterpret_cast<LPARAM>(this));
        if (hook) {
            UnhookWindowsHookEx(hook);
        }
        active_ = nullptr;
    }

private:
    static LRESULT CALLBACK KeyFilter(int code, WPARAM wParam, LPARAM lParam) {
        const auto* message = reinterpret_cast<const MSG*>(lParam);
        if (code == MSGF_DIALOGBOX && active_ && active_->closeKey_ != kNoKey &&
            (message->message == WM_KEYDOWN || message->message == WM_SYSKEYDOWN) &&
            ChordFromKey(message->wParam) == active_->closeKey_) {
            PostMessageW(active_->dialog_, WM_COMMAND, IDCANCEL, 0);
            return 1;
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    static INT_PTR CALLBACK Proc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_INITDIALOG) {
            SetWindowLongPtrW(dialog, DWLP_USER, lParam);
            auto* self = reinterpret_cast<InfoDialog*>(lParam);
            self->dialog_ = dialog;
            self->Initialize();
            return FALSE;  // Focus has been set.
        }
        auto* self = reinterpret_cast<InfoDialog*>(GetWindowLongPtrW(dialog, DWLP_USER));
        return self ? self->Handle(message, wParam, lParam) : FALSE;
    }

    void Initialize();
    INT_PTR Handle(UINT message, WPARAM wParam, LPARAM lParam);
    void FitColumns();
    void Refresh();
    void ShowRows(std::vector<InfoRow> rows);

    HWND owner_;
    const MpvPlayer& player_;
    const PlaybackState& state_;
    KeyChord closeKey_;
    HWND dialog_ = nullptr;
    HWND tab_ = nullptr;
    HWND list_ = nullptr;
    std::vector<InfoRow> shown_;

    static inline InfoDialog* active_ = nullptr;
};

void InfoDialog::Initialize() {
    tab_ = GetDlgItem(dialog_, IDC_INFO_TAB);
    list_ = GetDlgItem(dialog_, IDC_INFO_LIST);

    wchar_t playback[] = L"재생 정보";
    wchar_t file[] = L"파일 정보";
    wchar_t system[] = L"시스템 정보";
    TCITEMW tabItem{};
    tabItem.mask = TCIF_TEXT;
    tabItem.pszText = playback;
    TabCtrl_InsertItem(tab_, 0, &tabItem);
    tabItem.pszText = file;
    TabCtrl_InsertItem(tab_, 1, &tabItem);
    tabItem.pszText = system;
    TabCtrl_InsertItem(tab_, 2, &tabItem);

    // The label tip shows a value in full when it's too long for its column.
    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    wchar_t labelColumn[] = L"항목";
    wchar_t valueColumn[] = L"값";
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT;
    column.pszText = labelColumn;
    ListView_InsertColumn(list_, 0, &column);
    column.pszText = valueColumn;
    ListView_InsertColumn(list_, 1, &column);
    ListView_EnableGroupView(list_, TRUE);
    FitColumns();

    Refresh();
    SetTimer(dialog_, kRefreshTimer, kRefreshMs, nullptr);
    SetFocus(tab_);
}

INT_PTR InfoDialog::Handle(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog_, IDCANCEL);
        }
        return TRUE;
    case WM_NOTIFY: {
        const auto* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->hwndFrom == tab_ && header->code == TCN_SELCHANGE) {
            Refresh();
            ListView_EnsureVisible(list_, 0, FALSE);
        }
        return TRUE;
    }
    case WM_TIMER:
        if (wParam == kRefreshTimer) {
            Refresh();
        }
        return TRUE;
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORBTN:
        return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
    case WM_DPICHANGED:
        // Windows rescales the dialog and its controls after this. Then the rest can follow.
        PostMessageW(dialog_, WM_DPI_CHANGE_DONE, 0, 0);
        return FALSE;
    case WM_DPI_CHANGE_DONE:
        FitColumns();
        return TRUE;
    case WM_DESTROY:
        KillTimer(dialog_, kRefreshTimer);
        return FALSE;
    default:
        return FALSE;
    }
}

void InfoDialog::FitColumns() {
    RECT client{};
    GetClientRect(list_, &client);
    const int width = client.right - GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(dialog_));
    ListView_SetColumnWidth(list_, 0, width * 3 / 10);
    ListView_SetColumnWidth(list_, 1, width - width * 3 / 10);
}

void InfoDialog::Refresh() {
    switch (TabCtrl_GetCurSel(tab_)) {
    case 1:
        ShowRows(BuildFileRows(player_, state_));
        break;
    case 2:
        ShowRows(BuildSystemRows(player_, owner_));
        break;
    default:
        ShowRows(BuildPlaybackRows(player_, state_));
        break;
    }
}

void InfoDialog::ShowRows(std::vector<InfoRow> rows) {
    // Usually only values change. Updating just those keeps the scroll position and selection.
    bool sameLayout = rows.size() == shown_.size();
    for (size_t i = 0; sameLayout && i < rows.size(); ++i) {
        sameLayout = rows[i].group == shown_[i].group && rows[i].label == shown_[i].label;
    }
    if (sameLayout) {
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].value != shown_[i].value) {
                ListView_SetItemText(list_, static_cast<int>(i), 1, rows[i].value.data());
            }
        }
    } else {
        SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
        ListView_DeleteAllItems(list_);
        ListView_RemoveAllGroups(list_);
        int groupId = 0;
        std::wstring group;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (i == 0 || rows[i].group != group) {
                group = rows[i].group;
                LVGROUP heading{};
                heading.cbSize = sizeof(heading);
                heading.mask = LVGF_HEADER | LVGF_GROUPID;
                heading.pszHeader = group.data();
                heading.iGroupId = ++groupId;
                ListView_InsertGroup(list_, -1, &heading);
            }
            LVITEMW item{};
            item.mask = LVIF_TEXT | LVIF_GROUPID;
            item.iItem = static_cast<int>(i);
            item.iGroupId = groupId;
            item.pszText = rows[i].label.data();
            ListView_InsertItem(list_, &item);
            ListView_SetItemText(list_, static_cast<int>(i), 1, rows[i].value.data());
        }
        SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(list_, nullptr, TRUE);
    }
    shown_ = std::move(rows);
}

}  // namespace

void ShowInfoDialog(HWND owner, const MpvPlayer& player, const PlaybackState& state, KeyChord closeKey) {
    InfoDialog(owner, player, state, closeKey).Run();
}
