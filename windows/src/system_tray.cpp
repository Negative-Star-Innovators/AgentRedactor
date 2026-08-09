#include "system_tray.h"
#include "utils.h"
#include "constants.h"
#include "resource.h"
#include <commctrl.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <vector>
#include <filesystem>

namespace AgentRedactor {

static bool IsWindowsDarkModeEnabled() {
    HKEY hKey;
    DWORD value = 1; // default to light mode
    DWORD size = sizeof(value);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
            reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(hKey);
    }
    return value == 0; // 0 = dark mode
}

static void ApplyMenuTheme(HWND hwnd) {
    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!hUxtheme) return;

    using fnSetPreferredAppMode = int (WINAPI*)(int);
    using fnAllowDarkModeForWindow = BOOL (WINAPI*)(HWND, BOOL);
    using fnFlushMenuThemes = void (WINAPI*)();

    auto SetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135)));
    auto AllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133)));
    auto FlushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136)));

    if (SetPreferredAppMode && AllowDarkModeForWindow && FlushMenuThemes) {
        bool dark = IsWindowsDarkModeEnabled();
        // PreferredAppMode: 0=Default, 1=AllowDark, 2=ForceDark, 3=ForceLight
        SetPreferredAppMode(dark ? 2 : 3);
        AllowDarkModeForWindow(hwnd, dark ? TRUE : FALSE);
        FlushMenuThemes();
    }

    FreeLibrary(hUxtheme);
}

SystemTray::SystemTray(HWND hwnd) : hwnd_(hwnd) {
    notifyIconData_.cbSize = sizeof(NOTIFYICONDATAW);
    notifyIconData_.hWnd = hwnd;
    notifyIconData_.uID = TRAY_ICON_ID;
    notifyIconData_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    notifyIconData_.uCallbackMessage = WM_TRAYICON;
    notifyIconData_.hIcon = nullptr;
    notifyIconData_.szTip[0] = L'\0';
}

SystemTray::~SystemTray() {
    Destroy();
}

bool SystemTray::Create(HICON icon, const std::wstring& tooltip) {
    if (created_) return true;
    if (!icon) return false;
    notifyIconData_.hIcon = icon;
    wcsncpy_s(notifyIconData_.szTip, tooltip.c_str(), _TRUNCATE);
    BOOL result = Shell_NotifyIconW(NIM_ADD, &notifyIconData_);
    if (!result) return false;
    created_ = true;
    return true;
}

void SystemTray::Destroy() {
    if (!created_) return;
    Shell_NotifyIconW(NIM_DELETE, &notifyIconData_);
    created_ = false;
    if (notifyIconData_.hIcon) {
        DestroyIcon(notifyIconData_.hIcon);
        notifyIconData_.hIcon = nullptr;
    }
}

void SystemTray::ShowMenu(const std::vector<MenuItem>& items) {
    if (!hwnd_ || showingMenu_) return;
    struct ScopeGuard {
        SystemTray* tray;
        ~ScopeGuard() { if (tray) tray->showingMenu_ = false; }
    };
    showingMenu_ = true;
    ScopeGuard guard{this};
    commandCallbacks_.clear();
    HMENU hMenu = BuildMenu(items);
    if (!hMenu) return;
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd_);
    ApplyMenuTheme(hwnd_);
    UINT cmd = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_LEFTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(hMenu);
    if (cmd != 0) HandleCommand(cmd);
}

void SystemTray::SetOnLeftClick(std::function<void()> callback) {
    onLeftClick_ = std::move(callback);
}

void SystemTray::SetOnRightClick(std::function<void()> callback) {
    onRightClick_ = std::move(callback);
}

void SystemTray::ShowNotification(const std::wstring& title, const std::wstring& message, DWORD iconType) {
    if (!created_) return;
    NOTIFYICONDATAW nid = notifyIconData_;
    nid.uFlags = NIF_INFO;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, message.c_str(), _TRUNCATE);
    nid.dwInfoFlags = NIIF_USER;
    nid.hIcon = notifyIconData_.hIcon;
    nid.uTimeout = 3000;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void SystemTray::UpdateIcon(HICON icon) {
    if (!created_) return;
    if (notifyIconData_.hIcon) DestroyIcon(notifyIconData_.hIcon);
    notifyIconData_.hIcon = icon;
    notifyIconData_.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &notifyIconData_);
}

void SystemTray::UpdateTooltip(const std::wstring& tooltip) {
    if (!created_) return;
    wcsncpy_s(notifyIconData_.szTip, tooltip.c_str(), _TRUNCATE);
    notifyIconData_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &notifyIconData_);
}

LRESULT SystemTray::HandleTrayMessage(LPARAM lParam) {
    switch (lParam) {
        case WM_MOUSEMOVE: return 0;
        case WM_LBUTTONDOWN:
            if (onLeftClick_) onLeftClick_();
            return 0;
        case WM_LBUTTONUP: return 0;
        case WM_LBUTTONDBLCLK:
            if (onLeftClick_) onLeftClick_();
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            if (onRightClick_) onRightClick_();
            return 0;
        case WM_RBUTTONDOWN:
            return 0;
        default: return 0;
    }
}

void SystemTray::HandleCommand(UINT commandId) {
    auto it = commandCallbacks_.find(commandId);
    if (it != commandCallbacks_.end() && it->second) {
        try { it->second(); } catch (...) {}
    }
}

HMENU SystemTray::BuildMenu(const std::vector<MenuItem>& items) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return nullptr;
    for (const auto& item : items) {
        if (item.isSeparator) {
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        } else if (item.isSubmenu) {
            HMENU hSubmenu = BuildMenu(item.submenu);
            if (hSubmenu) AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSubmenu), item.text.c_str());
        } else {
            UINT flags = MF_STRING;
            if (!item.enabled) flags |= MF_GRAYED;
            if (item.checked) flags |= MF_CHECKED;
            if (item.callback) commandCallbacks_[item.id] = item.callback;
            AppendMenuW(hMenu, flags, item.id, item.text.c_str());
        }
    }
    return hMenu;
}

HICON SystemTray::CreateGradientIcon(int size) {
    HDC hdc = GetDC(nullptr);
    if (!hdc) return nullptr;
    HDC hdcMem = CreateCompatibleDC(hdc);
    if (!hdcMem) { ReleaseDC(nullptr, hdc); return nullptr; }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits;
    HBITMAP hColorBitmap = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hColorBitmap) { DeleteDC(hdcMem); ReleaseDC(nullptr, hdc); return nullptr; }

    ZeroMemory(bits, size * size * 4);
    unsigned char* pixels = static_cast<unsigned char*>(bits);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int idx = (y * size + x) * 4;
            pixels[idx + 0] = static_cast<unsigned char>(43 + (18 - 43) * y / size);
            pixels[idx + 1] = static_cast<unsigned char>(57 + (156 - 57) * y / size);
            pixels[idx + 2] = static_cast<unsigned char>(192 + (243 - 192) * y / size);
            pixels[idx + 3] = 255;
        }
    }

    std::vector<BYTE> maskBits(size * size / 8, 0x00);
    HBITMAP hMaskBitmap = CreateBitmap(size, size, 1, 1, maskBits.data());
    if (!hMaskBitmap) { DeleteObject(hColorBitmap); DeleteDC(hdcMem); ReleaseDC(nullptr, hdc); return nullptr; }

    ICONINFO iconInfo = {};
    iconInfo.fIcon = TRUE;
    iconInfo.hbmColor = hColorBitmap;
    iconInfo.hbmMask = hMaskBitmap;
    HICON hResultIcon = CreateIconIndirect(&iconInfo);

    DeleteObject(hColorBitmap);
    DeleteObject(hMaskBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdc);
    return hResultIcon;
}

HICON SystemTray::LoadIconFromFile(const std::wstring& filename, int size) {
    std::vector<std::filesystem::path> searchPaths;
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    searchPaths.push_back(exeDir / filename);
    wchar_t cwdPath[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwdPath);
    searchPaths.push_back(std::filesystem::path(cwdPath) / filename);
    wchar_t appDataPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appDataPath))) {
        searchPaths.push_back(std::filesystem::path(appDataPath) / L"AgentRedactor" / filename);
    }
    for (const auto& iconPath : searchPaths) {
        if (std::filesystem::exists(iconPath)) {
            HICON hIcon = (HICON)LoadImageW(nullptr, iconPath.c_str(), IMAGE_ICON, size, size, LR_LOADFROMFILE | LR_DEFAULTSIZE | LR_DEFAULTCOLOR);
            if (hIcon) return hIcon;
        }
    }
    return CreateGradientIcon(size);
}

} // namespace AgentRedactor
