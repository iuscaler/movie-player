#include "SettingsDialog.h"

#include <commctrl.h>
#include <imm.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <optional>
#include <set>
#include <string>

#include "FileAssociation.h"
#include "Resource.h"

// Themed controls. Without this, Windows draws the dialog in its oldest style.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

constexpr UINT WM_DPI_CHANGE_DONE = WM_APP + 1;
constexpr int kPreviewPointSize = 18;
constexpr wchar_t kPreviewText[] = L"가나다라 ABC 123";
constexpr wchar_t kPreviewDefaultFont[] = L"Segoe UI";
constexpr wchar_t kDefaultFontLabel[] = L"기본 글꼴";
constexpr wchar_t kShortcutHint[] = L"항목을 더블클릭하거나 변경을 누른 뒤 새 키를 누르세요.";

constexpr int kGeneralPage[] = {IDC_MOVE_GROUP, IDC_SEEK_LABEL, IDC_SEEK_EDIT, IDC_SEEK_SPIN, IDC_SEEK_UNIT,
                                IDC_ASSOC_GROUP, IDC_ASSOC_STATUS, IDC_ASSOC_HINT, IDC_ASSOC_REGISTER,
                                IDC_ASSOC_UNREGISTER, IDC_ASSOC_DEFAULTS};
constexpr int kSubtitlePage[] = {IDC_FONT_LABEL, IDC_FONT_COMBO, IDC_SIZE_LABEL, IDC_SIZE_EDIT, IDC_SIZE_SPIN,
                                 IDC_SIZE_UNIT, IDC_OUTLINE_LABEL, IDC_OUTLINE_COMBO, IDC_PREVIEW_LABEL, IDC_PREVIEW, IDC_SUBTITLE_HINT};
constexpr int kShortcutPage[] = {IDC_SHORTCUT_LIST, IDC_SHORTCUT_STATUS, IDC_SHORTCUT_CHANGE, IDC_SHORTCUT_CLEAR,
                                 IDC_SHORTCUT_DEFAULTS};

int CALLBACK AddFontName(const LOGFONTW* font, const TEXTMETRICW* metric, DWORD type, LPARAM names) {
    const auto* details = reinterpret_cast<const NEWTEXTMETRICW*>(metric);
    // Subtitles can only use outline fonts. Names starting with '@' are the vertical versions.
    const bool outline = (type & TRUETYPE_FONTTYPE) != 0 || (details->ntmFlags & (NTM_PS_OPENTYPE | NTM_TT_OPENTYPE)) != 0;
    if (outline && font->lfFaceName[0] != L'@') {
        reinterpret_cast<std::set<std::wstring>*>(names)->insert(font->lfFaceName);
    }
    return 1;
}

bool IsMouseDown(UINT message) {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
    case WM_NCLBUTTONDOWN:
    case WM_NCRBUTTONDOWN:
    case WM_NCMBUTTONDOWN:
    case WM_NCXBUTTONDOWN:
        return true;
    default:
        return false;
    }
}

class SettingsDialog {
public:
    SettingsDialog(const Preferences& current, const std::function<void(const Preferences&)>& apply)
        : working_(current), applied_(current), apply_(apply) {}

    void Run(HWND owner) {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_UPDOWN_CLASS};
        InitCommonControlsEx(&controls);
        DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), owner, &Proc, reinterpret_cast<LPARAM>(this));
    }

private:
    static INT_PTR CALLBACK Proc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_INITDIALOG) {
            SetWindowLongPtrW(dialog, DWLP_USER, lParam);
            auto* self = reinterpret_cast<SettingsDialog*>(lParam);
            self->dialog_ = dialog;
            self->Initialize();
            return FALSE;  // Focus has been set.
        }
        auto* self = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(dialog, DWLP_USER));
        return self ? self->Handle(message, wParam, lParam) : FALSE;
    }

    HWND Item(int id) const { return GetDlgItem(dialog_, id); }

    void Initialize();
    INT_PTR Handle(UINT message, WPARAM wParam, LPARAM lParam);
    void HandleCommand(int id, int code);
    void HandleNotify(const NMHDR& header);

    void ShowPage(int page);
    void FillFontList();
    void FillOutlineList();
    void FitListColumns();
    void UpdatePreview();
    void UpdateApplyButton();
    void ReadNumberFields();
    void WriteNumberFields();
    void Apply();

    void UpdateAssociationState();
    void MoveFocusIfDisabled(HWND control);

    void FillShortcutList();
    void ShowShortcutRow(int row);
    int SelectedRow() const;
    void UpdateShortcutButtons();
    void ChangeShortcut(int row);
    std::optional<KeyChord> WaitForKey();
    void WaitForKeysUp(WPARAM key);
    void SetStatus(const std::wstring& text) { SetDlgItemTextW(dialog_, IDC_SHORTCUT_STATUS, text.c_str()); }

    Preferences working_;
    Preferences applied_;
    const std::function<void(const Preferences&)>& apply_;

    HWND dialog_ = nullptr;
    HWND tab_ = nullptr;
    HWND list_ = nullptr;
    HFONT previewFont_ = nullptr;
    bool loading_ = true;  // Filling in the controls, so their change notifications aren't the user's.
    bool capturing_ = false;
};

void SettingsDialog::Initialize() {
    tab_ = Item(IDC_TAB);
    list_ = Item(IDC_SHORTCUT_LIST);

    wchar_t general[] = L"일반";
    wchar_t subtitle[] = L"자막";
    wchar_t shortcuts[] = L"단축키";
    TCITEMW tabItem{};
    tabItem.mask = TCIF_TEXT;
    tabItem.pszText = general;
    TabCtrl_InsertItem(tab_, 0, &tabItem);
    tabItem.pszText = subtitle;
    TabCtrl_InsertItem(tab_, 1, &tabItem);
    tabItem.pszText = shortcuts;
    TabCtrl_InsertItem(tab_, 2, &tabItem);

    SendDlgItemMessageW(dialog_, IDC_SEEK_SPIN, UDM_SETRANGE32, kMinSeekSeconds, kMaxSeekSeconds);
    SendDlgItemMessageW(dialog_, IDC_SIZE_SPIN, UDM_SETRANGE32, kMinSubtitleFontSize, kMaxSubtitleFontSize);
    WriteNumberFields();

    FillFontList();
    FillOutlineList();
    UpdatePreview();
    UpdateAssociationState();

    ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    // With a Korean input mode on, the key being assigned would arrive as VK_PROCESSKEY.
    ImmAssociateContextEx(list_, nullptr, 0);
    wchar_t actionColumn[] = L"동작";
    wchar_t keyColumn[] = L"단축키";
    LVCOLUMNW column{};
    column.mask = LVCF_TEXT;
    column.pszText = actionColumn;
    ListView_InsertColumn(list_, 0, &column);
    column.pszText = keyColumn;
    ListView_InsertColumn(list_, 1, &column);
    FillShortcutList();
    FitListColumns();
    ListView_SetItemState(list_, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    SetStatus(kShortcutHint);
    UpdateShortcutButtons();

    ShowPage(0);
    UpdateApplyButton();
    SetFocus(tab_);
    loading_ = false;
}

INT_PTR SettingsDialog::Handle(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND:
        HandleCommand(LOWORD(wParam), HIWORD(wParam));
        return TRUE;
    case WM_NOTIFY:
        HandleNotify(*reinterpret_cast<const NMHDR*>(lParam));
        return TRUE;
    // White, so the pages look like one surface under the strip of tabs.
    case WM_CTLCOLORDLG:
        return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOW));
        SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOWTEXT));
        return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_WINDOW));
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE && capturing_) {
            capturing_ = false;
            PostMessageW(dialog_, WM_NULL, 0, 0);  // Wakes the loop waiting for a key.
        }
        return FALSE;
    case WM_DPICHANGED:
        // Windows rescales the dialog and its controls after this. Then the rest can follow.
        PostMessageW(dialog_, WM_DPI_CHANGE_DONE, 0, 0);
        return FALSE;
    case WM_DPI_CHANGE_DONE:
        UpdatePreview();
        FitListColumns();
        return TRUE;
    case WM_DESTROY:
        if (previewFont_) {
            DeleteObject(previewFont_);
            previewFont_ = nullptr;
        }
        return FALSE;
    default:
        return FALSE;
    }
}

void SettingsDialog::HandleCommand(int id, int code) {
    switch (id) {
    case IDOK:
        Apply();
        EndDialog(dialog_, IDOK);
        break;
    case IDCANCEL:
        capturing_ = false;
        EndDialog(dialog_, IDCANCEL);
        break;
    case IDC_APPLY: {
        Apply();
        MoveFocusIfDisabled(Item(IDC_APPLY));
        break;
    }
    case IDC_SEEK_EDIT:
    case IDC_SIZE_EDIT:
        if (code == EN_CHANGE && !loading_) {
            ReadNumberFields();
            UpdateApplyButton();
        }
        break;
    case IDC_FONT_COMBO:
        if (code == CBN_SELCHANGE) {
            const int selected = static_cast<int>(SendDlgItemMessageW(dialog_, IDC_FONT_COMBO, CB_GETCURSEL, 0, 0));
            if (selected == 0) {
                working_.subtitleStyle.font.clear();
            } else if (selected > 0) {
                std::wstring name(static_cast<size_t>(SendDlgItemMessageW(dialog_, IDC_FONT_COMBO, CB_GETLBTEXTLEN, selected, 0)), L'\0');
                SendDlgItemMessageW(dialog_, IDC_FONT_COMBO, CB_GETLBTEXT, selected, reinterpret_cast<LPARAM>(name.data()));
                working_.subtitleStyle.font = name;
            }
            UpdatePreview();
            UpdateApplyButton();
        }
        break;
    case IDC_OUTLINE_COMBO:
        if (code == CBN_SELCHANGE) {
            const int selected = static_cast<int>(SendDlgItemMessageW(dialog_, IDC_OUTLINE_COMBO, CB_GETCURSEL, 0, 0));
            if (selected >= 0) {
                working_.subtitleStyle.outlineSize = kSubtitleOutlineSizes[static_cast<size_t>(selected)];
            }
            UpdateApplyButton();
        }
        break;
    case IDC_ASSOC_REGISTER:
        if (!RegisterFileAssociation()) {
            SetDlgItemTextW(dialog_, IDC_ASSOC_STATUS, L"상태: 등록하지 못했습니다");
            break;
        }
        UpdateAssociationState();
        MoveFocusIfDisabled(Item(IDC_ASSOC_REGISTER));
        break;
    case IDC_ASSOC_UNREGISTER:
        UnregisterFileAssociation();
        UpdateAssociationState();
        MoveFocusIfDisabled(Item(IDC_ASSOC_UNREGISTER));
        break;
    case IDC_ASSOC_DEFAULTS:
        OpenDefaultAppsSettings();
        break;
    case IDC_SHORTCUT_CHANGE:
        if (const int row = SelectedRow(); row >= 0) {
            ChangeShortcut(row);
        }
        break;
    case IDC_SHORTCUT_CLEAR:
        if (const int row = SelectedRow(); row >= 0) {
            working_.shortcuts[static_cast<size_t>(row)] = kNoKey;
            ShowShortcutRow(row);
            SetStatus(kShortcutHint);
            UpdateShortcutButtons();
            UpdateApplyButton();
        }
        break;
    case IDC_SHORTCUT_DEFAULTS:
        working_.shortcuts = DefaultShortcuts();
        FillShortcutList();
        SetStatus(L"모든 단축키를 기본값으로 되돌렸습니다. 적용하거나 확인을 눌러야 반영됩니다.");
        UpdateShortcutButtons();
        UpdateApplyButton();
        break;
    default:
        break;
    }
}

void SettingsDialog::HandleNotify(const NMHDR& header) {
    if (header.hwndFrom == tab_ && header.code == TCN_SELCHANGE) {
        ShowPage(TabCtrl_GetCurSel(tab_));
    } else if (header.hwndFrom == list_) {
        if (header.code == LVN_ITEMCHANGED) {
            UpdateShortcutButtons();
        } else if (header.code == NM_DBLCLK) {
            const int row = reinterpret_cast<const NMITEMACTIVATE&>(header).iItem;
            if (row >= 0) {
                ChangeShortcut(row);
            }
        }
    }
}

void SettingsDialog::ShowPage(int page) {
    const auto show = [this](const auto& ids, bool visible) {
        for (const int id : ids) {
            ShowWindow(Item(id), visible ? SW_SHOW : SW_HIDE);
        }
    };
    show(kGeneralPage, page == 0);
    show(kSubtitlePage, page == 1);
    show(kShortcutPage, page == 2);
}

void SettingsDialog::FillFontList() {
    std::set<std::wstring> names;
    LOGFONTW filter{};
    filter.lfCharSet = DEFAULT_CHARSET;
    HDC screen = GetDC(nullptr);
    EnumFontFamiliesExW(screen, &filter, &AddFontName, reinterpret_cast<LPARAM>(&names), 0);
    ReleaseDC(nullptr, screen);
    // A saved font that's gone stays listed, so opening this window doesn't change the setting.
    if (!working_.subtitleStyle.font.empty()) {
        names.insert(working_.subtitleStyle.font);
    }

    SendDlgItemMessageW(dialog_, IDC_FONT_COMBO, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kDefaultFontLabel));
    int selected = 0;
    for (const std::wstring& name : names) {
        const LRESULT index = SendDlgItemMessageW(dialog_, IDC_FONT_COMBO, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        if (name == working_.subtitleStyle.font) {
            selected = static_cast<int>(index);
        }
    }
    SendDlgItemMessageW(dialog_, IDC_FONT_COMBO, CB_SETCURSEL, selected, 0);
}

void SettingsDialog::FillOutlineList() {
    for (size_t i = 0; i < kSubtitleOutlineSizes.size(); ++i) {
        const double size = kSubtitleOutlineSizes[i];
        wchar_t label[32];
        if (size == 0.0) {
            wcscpy_s(label, L"없음");
        } else if (size == kDefaultSubtitleOutlineSize) {
            swprintf_s(label, L"%g (기본)", size);
        } else {
            swprintf_s(label, L"%g", size);
        }
        SendDlgItemMessageW(dialog_, IDC_OUTLINE_COMBO, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        if (size == working_.subtitleStyle.outlineSize) {
            SendDlgItemMessageW(dialog_, IDC_OUTLINE_COMBO, CB_SETCURSEL, i, 0);
        }
    }
}

void SettingsDialog::FitListColumns() {
    RECT client{};
    GetClientRect(list_, &client);
    const int width = client.right - GetSystemMetricsForDpi(SM_CXVSCROLL, GetDpiForWindow(dialog_));
    ListView_SetColumnWidth(list_, 0, width * 3 / 5);
    ListView_SetColumnWidth(list_, 1, width - width * 3 / 5);
}

void SettingsDialog::UpdatePreview() {
    LOGFONTW font{};
    font.lfHeight = -MulDiv(kPreviewPointSize, static_cast<int>(GetDpiForWindow(dialog_)), 72);
    font.lfCharSet = DEFAULT_CHARSET;
    const std::wstring& name = working_.subtitleStyle.font;
    wcsncpy_s(font.lfFaceName, name.empty() ? kPreviewDefaultFont : name.c_str(), _TRUNCATE);
    HFONT created = CreateFontIndirectW(&font);
    SendDlgItemMessageW(dialog_, IDC_PREVIEW, WM_SETFONT, reinterpret_cast<WPARAM>(created), TRUE);
    if (previewFont_) {
        DeleteObject(previewFont_);
    }
    previewFont_ = created;
    SetDlgItemTextW(dialog_, IDC_PREVIEW, kPreviewText);
}

void SettingsDialog::UpdateApplyButton() {
    EnableWindow(Item(IDC_APPLY), !(working_ == applied_));
}

void SettingsDialog::ReadNumberFields() {
    BOOL valid = FALSE;
    const UINT seek = GetDlgItemInt(dialog_, IDC_SEEK_EDIT, &valid, FALSE);
    if (valid) {
        working_.seekSeconds = std::clamp(static_cast<int>(std::min<UINT>(seek, kMaxSeekSeconds)), kMinSeekSeconds, kMaxSeekSeconds);
    }
    const UINT size = GetDlgItemInt(dialog_, IDC_SIZE_EDIT, &valid, FALSE);
    if (valid) {
        working_.subtitleStyle.fontSize =
            std::clamp(static_cast<int>(std::min<UINT>(size, kMaxSubtitleFontSize)), kMinSubtitleFontSize, kMaxSubtitleFontSize);
    }
}

void SettingsDialog::WriteNumberFields() {
    const bool wasLoading = loading_;
    loading_ = true;
    SetDlgItemInt(dialog_, IDC_SEEK_EDIT, static_cast<UINT>(working_.seekSeconds), FALSE);
    SetDlgItemInt(dialog_, IDC_SIZE_EDIT, static_cast<UINT>(working_.subtitleStyle.fontSize), FALSE);
    loading_ = wasLoading;
}

void SettingsDialog::Apply() {
    ReadNumberFields();
    WriteNumberFields();  // Shows the value actually used, if what was typed was out of range.
    if (!(working_ == applied_)) {
        apply_(working_);
        applied_ = working_;
    }
    UpdateApplyButton();
}

void SettingsDialog::UpdateAssociationState() {
    const bool registered = IsFileAssociationRegistered();
    SetDlgItemTextW(dialog_, IDC_ASSOC_STATUS, registered ? L"상태: 등록됨" : L"상태: 등록 안 됨");
    EnableWindow(Item(IDC_ASSOC_REGISTER), !registered);
    EnableWindow(Item(IDC_ASSOC_UNREGISTER), registered);
    EnableWindow(Item(IDC_ASSOC_DEFAULTS), registered);
}

void SettingsDialog::MoveFocusIfDisabled(HWND control) {
    if (!IsWindowEnabled(control)) {
        SendMessageW(dialog_, WM_NEXTDLGCTL, 0, FALSE);
    }
}

void SettingsDialog::FillShortcutList() {
    if (ListView_GetItemCount(list_) == 0) {
        for (size_t i = 0; i < kShortcutCount; ++i) {
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(i);
            item.pszText = const_cast<wchar_t*>(AllShortcuts()[i].label);
            ListView_InsertItem(list_, &item);
        }
    }
    for (int row = 0; row < static_cast<int>(kShortcutCount); ++row) {
        ShowShortcutRow(row);
    }
}

void SettingsDialog::ShowShortcutRow(int row) {
    const KeyChord chord = working_.shortcuts[static_cast<size_t>(row)];
    std::wstring name = chord == kNoKey ? L"없음" : KeyChordName(chord);
    ListView_SetItemText(list_, row, 1, name.data());
}

int SettingsDialog::SelectedRow() const {
    return ListView_GetNextItem(list_, -1, LVNI_SELECTED);
}

void SettingsDialog::UpdateShortcutButtons() {
    const int row = SelectedRow();
    EnableWindow(Item(IDC_SHORTCUT_CHANGE), row >= 0);
    EnableWindow(Item(IDC_SHORTCUT_CLEAR), row >= 0 && working_.shortcuts[static_cast<size_t>(row)] != kNoKey);
}

void SettingsDialog::ChangeShortcut(int row) {
    if (capturing_) {
        return;
    }
    wchar_t waiting[] = L"키를 누르세요...";
    ListView_SetItemText(list_, row, 1, waiting);
    SetStatus(L"새 키를 누르세요. 다른 곳을 클릭하면 취소됩니다.");
    SetFocus(list_);

    const std::optional<KeyChord> chord = WaitForKey();

    std::wstring status = kShortcutHint;
    if (chord) {
        const size_t index = static_cast<size_t>(row);
        const std::optional<ShortcutAction> taken = FindShortcut(working_.shortcuts, *chord);
        if (taken && static_cast<size_t>(*taken) != index) {
            // A key can only do one thing, so the newest assignment wins.
            working_.shortcuts[static_cast<size_t>(*taken)] = kNoKey;
            ShowShortcutRow(static_cast<int>(*taken));
            status = KeyChordName(*chord) + L" 키를 \"" + GetShortcutInfo(*taken).label + L"\"에서 옮겼습니다.";
        }
        working_.shortcuts[index] = *chord;
    }
    ShowShortcutRow(row);
    SetStatus(status);
    UpdateShortcutButtons();
    UpdateApplyButton();
}

// Runs its own message loop, because the dialog's would use Tab, Enter and Esc for
// navigation before anything else could see them, and those are valid shortcuts.
std::optional<KeyChord> SettingsDialog::WaitForKey() {
    capturing_ = true;
    std::optional<KeyChord> result;
    MSG message{};
    while (capturing_ && !result) {
        const BOOL status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0) {
            if (status == 0) {
                PostQuitMessage(static_cast<int>(message.wParam));  // For the main loop.
            }
            break;
        }
        if (message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST) {
            if ((message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN) && !IsModifierKey(message.wParam)) {
                const KeyChord chord = ChordFromKey(message.wParam);
                if (CanBeShortcut(chord)) {
                    result = chord;
                    WaitForKeysUp(message.wParam);
                } else {
                    SetStatus(KeyChordName(chord) + L" 키는 단축키로 쓸 수 없습니다. 다른 키를 누르세요.");
                }
            }
            continue;  // Keyboard input during this goes nowhere else.
        }
        if (IsMouseDown(message.message)) {
            capturing_ = false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    capturing_ = false;
    return result;
}

// Swallows the rest of the key press. Otherwise a held Enter would repeat and press
// OK, and releasing Alt would open the system menu.
void SettingsDialog::WaitForKeysUp(WPARAM key) {
    const auto held = [key] {
        return GetKeyState(static_cast<int>(key)) < 0 || GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_SHIFT) < 0 || GetKeyState(VK_MENU) < 0;
    };
    MSG message{};
    while (capturing_ && held()) {
        const BOOL status = GetMessageW(&message, nullptr, 0, 0);
        if (status <= 0) {
            if (status == 0) {
                PostQuitMessage(static_cast<int>(message.wParam));
            }
            return;
        }
        if (message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST) {
            continue;
        }
        if (IsMouseDown(message.message)) {
            capturing_ = false;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

}  // namespace

void ShowSettingsDialog(HWND owner, const Preferences& current, const std::function<void(const Preferences&)>& apply) {
    SettingsDialog(current, apply).Run(owner);
}
