#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>
// Block winsock v1 (same _WINSOCKAPI_ idiom as pch.h): core headers pull in
// platform_compat.h, which includes winsock2.h — if v1 gets compiled first
// here, the two socket APIs clash in this TU.
#define _WINSOCKAPI_
#include <windows.h>
#include <shellapi.h>

namespace AgentRedactor {

struct MenuItem {
    std::wstring text;
    UINT id = 0;
    std::function<void()> callback;
    bool enabled = true;
    bool checked = false;
    bool isSeparator = false;
    bool isSubmenu = false;
    std::vector<MenuItem> submenu;

    static MenuItem Separator() {
        MenuItem item;
        item.isSeparator = true;
        return item;
    }
    static MenuItem Item(const std::wstring& text, UINT id, std::function<void()> cb = nullptr, bool enabled = true, bool checked = false) {
        MenuItem item;
        item.text = text;
        item.id = id;
        item.callback = cb;
        item.enabled = enabled;
        item.checked = checked;
        return item;
    }
    static MenuItem Submenu(const std::wstring& text, std::vector<MenuItem> items) {
        MenuItem item;
        item.text = text;
        item.isSubmenu = true;
        item.submenu = std::move(items);
        return item;
    }
};

class SystemTray {
public:
    explicit SystemTray(HWND hwnd);
    ~SystemTray();
    SystemTray(const SystemTray&) = delete;
    SystemTray& operator=(const SystemTray&) = delete;

    bool Create(HICON icon, const std::wstring& tooltip);
    void Destroy();
    void ShowMenu(const std::vector<MenuItem>& items);
    void SetOnLeftClick(std::function<void()> callback);
    void SetOnRightClick(std::function<void()> callback);
    void ShowNotification(const std::wstring& title, const std::wstring& message, DWORD iconType = NIIF_INFO);
    void UpdateIcon(HICON icon);
    void UpdateTooltip(const std::wstring& tooltip);
    LRESULT HandleTrayMessage(LPARAM lParam);
    void HandleCommand(UINT commandId);
    static HICON CreateGradientIcon(int size = 256);
    static HICON LoadIconFromFile(const std::wstring& filename, int size = 256);

private:
    HMENU BuildMenu(const std::vector<MenuItem>& items);
    HWND hwnd_ = nullptr;
    NOTIFYICONDATAW notifyIconData_ = {};
    std::function<void()> onLeftClick_;
    std::function<void()> onRightClick_;
    std::unordered_map<UINT, std::function<void()>> commandCallbacks_;
    bool created_ = false;
    bool showingMenu_ = false;
    static constexpr UINT TRAY_ICON_ID = 1;
};

} // namespace AgentRedactor
