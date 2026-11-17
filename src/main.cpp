// ============================================================
//  HotKey Manager — C++ / Win32 / Direct2D
//  Компилятор: MSVC (cl.exe)
//  Сборка: cl /std:c++17 /EHsc /O2 /DUNICODE /D_UNICODE main.cpp
//          /link /SUBSYSTEM:WINDOWS /OUT:HotKeyManager.exe
//          user32.lib gdi32.lib shell32.lib ole32.lib
//          d2d1.lib dwrite.lib comdlg32.lib
// ============================================================
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")

// ---------------- Модель ----------------
enum class ActionType { Url = 0, App = 1, Command = 2 };

struct HotkeyItem {
    int          id = 0;
    std::wstring name;
    std::wstring combo;
    UINT         mods = 0;
    UINT         vk = 0;
    ActionType   type = ActionType::Url;
    std::wstring value;
    bool         enabled = true;
};

// ---------------- Глобальные ----------------
static std::vector<HotkeyItem> g_hotkeys;
static int  g_nextId = 1;
static HWND g_hwnd = nullptr;
static std::wstring g_configPath;

// D2D / DWrite
static ID2D1Factory*          g_pD2DFactory = nullptr;
static IDWriteFactory*        g_pDWriteFactory = nullptr;
static ID2D1HwndRenderTarget* g_pRT = nullptr;
static IDWriteTextFormat*     g_fmtTitle = nullptr;
static IDWriteTextFormat*     g_fmtSub = nullptr;
static IDWriteTextFormat*     g_fmtBody = nullptr;
static IDWriteTextFormat*     g_fmtBadge = nullptr;
static IDWriteTextFormat*     g_fmtBtn = nullptr;
static IDWriteTextFormat*     g_fmtIcon = nullptr;

// UI-состояние
static int  g_hoverIndex = -1;
static int  g_selectedIndex = -1;
static int  g_scrollOffset = 0;
static int  g_maxScroll = 0;
static NOTIFYICONDATAW g_nid = {};
static bool g_trayAdded = false;
static bool g_reallyExit = false;
static RECT g_addRect{}, g_editRect{}, g_delRect{};
static bool g_addHover = false, g_editHover = false, g_delHover = false;

// ---------------- Утилиты ----------------
static std::wstring GetConfigPath() {
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    std::wstring p = (n > 0) ? buf : L".";
    p += L"\\.hotkey_manager.ini";
    return p;
}

// Парсер комбинации: "Ctrl+Alt+Shift+Y" -> mods, vk
static bool ParseCombo(const std::wstring& combo, UINT& mods, UINT& vk) {
    mods = MOD_NOREPEAT;
    vk = 0;

    std::wstring token;
    std::wstringstream ss(combo);
    std::vector<std::wstring> tokens;
    while (std::getline(ss, token, L'+')) {
        while (!token.empty() && token.front() == L' ') token.erase(token.begin());
        while (!token.empty() && token.back()  == L' ') token.pop_back();
        if (!token.empty()) tokens.push_back(token);
    }
    if (tokens.empty()) return false;

    for (auto& t : tokens) {
        std::wstring low = t;
        for (auto& c : low) c = towlower(c);

        if      (low == L"ctrl" || low == L"control") mods |= MOD_CONTROL;
        else if (low == L"alt")                        mods |= MOD_ALT;
        else if (low == L"shift")                      mods |= MOD_SHIFT;
        else if (low == L"win" || low == L"windows")   mods |= MOD_WIN;
        else {
            if (low.size() == 1) {
                wchar_t c = towupper(low[0]);
                if (c >= L'A' && c <= L'Z') { vk = c; continue; }
                if (c >= L'0' && c <= L'9') { vk = c; continue; }
            }
            if (low.size() >= 2 && low[0] == L'f') {
                int num = _wtoi(low.c_str() + 1);
                if (num >= 1 && num <= 24) { vk = 0x70 + num - 1; continue; }
            }
            if      (low == L"space")     vk = VK_SPACE;
            else if (low == L"enter")     vk = VK_RETURN;
            else if (low == L"tab")       vk = VK_TAB;
            else if (low == L"esc" || low == L"escape") vk = VK_ESCAPE;
            else if (low == L"backspace") vk = VK_BACK;
            else if (low == L"delete" || low == L"del") vk = VK_DELETE;
            else if (low == L"insert" || low == L"ins") vk = VK_INSERT;
            else if (low == L"home")      vk = VK_HOME;
            else if (low == L"end")       vk = VK_END;
            else if (low == L"pageup")    vk = VK_PRIOR;
            else if (low == L"pagedown")  vk = VK_NEXT;
            else if (low == L"up")        vk = VK_UP;
            else if (low == L"down")      vk = VK_DOWN;
            else if (low == L"left")      vk = VK_LEFT;
            else if (low == L"right")     vk = VK_RIGHT;
            else return false;
        }
    }
    return vk != 0;
}

// ---------------- Хоткеи ----------------
static void UnregisterAll() {
    for (auto& hk : g_hotkeys) UnregisterHotKey(g_hwnd, hk.id);
}
static void RegisterAll() {
    UnregisterAll();
    for (auto& hk : g_hotkeys) {
        if (!hk.enabled) continue;
        if (!ParseCombo(hk.combo, hk.mods, hk.vk)) continue;
        RegisterHotKey(g_hwnd, hk.id, hk.mods, hk.vk);
    }
}

// ---------------- Действия ----------------
static void ExecuteAction(const HotkeyItem& hk) {
    if (hk.type == ActionType::Command) {
        STARTUPINFOW si{ sizeof(si) };
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"cmd.exe /c " + hk.value;
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);
        if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    } else {
        ShellExecuteW(nullptr, L"open", hk.value.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

// ---------------- Конфиг (INI) ----------------
static std::wstring ReadIni(const std::wstring& sec, const std::wstring& key) {
    wchar_t buf[4096] = {};
    GetPrivateProfileStringW(sec.c_str(), key.c_str(), L"", buf, 4096, g_configPath.c_str());
    return buf;
}

static void SaveConfig() {
    // Сначала очищаем файл
    std::wofstream clear(g_configPath, std::ios::trunc);
    clear.close();

    WritePrivateProfileStringW(L"hotkeys", L"count",
        std::to_wstring(g_hotkeys.size()).c_str(), g_configPath.c_str());

    for (size_t i = 0; i < g_hotkeys.size(); ++i) {
        auto& hk = g_hotkeys[i];
        std::wstring s = L"hk" + std::to_wstring(i);
        WritePrivateProfileStringW(s.c_str(), L"name",  hk.name.c_str(),  g_configPath.c_str());
        WritePrivateProfileStringW(s.c_str(), L"combo", hk.combo.c_str(), g_configPath.c_str());
        WritePrivateProfileStringW(s.c_str(), L"type",
            std::to_wstring((int)hk.type).c_str(), g_configPath.c_str());
        WritePrivateProfileStringW(s.c_str(), L"value", hk.value.c_str(), g_configPath.c_str());
        WritePrivateProfileStringW(s.c_str(), L"enabled",
            hk.enabled ? L"1" : L"0", g_configPath.c_str());
    }
}

static void LoadConfig() {
    g_hotkeys.clear();
    g_nextId = 1;
    int count = _wtoi(ReadIni(L"hotkeys", L"count").c_str());
    for (int i = 0; i < count; ++i) {
        std::wstring s = L"hk" + std::to_wstring(i);
        HotkeyItem hk;
        hk.id      = g_nextId++;
        hk.name    = ReadIni(s, L"name");
        hk.combo   = ReadIni(s, L"combo");
        hk.type    = (ActionType)_wtoi(ReadIni(s, L"type").c_str());
        hk.value   = ReadIni(s, L"value");
        hk.enabled = _wtoi(ReadIni(s, L"enabled").c_str()) != 0;
        if (!hk.name.empty()) g_hotkeys.push_back(hk);
    }
}

// ---------------- Direct2D ----------------
static HRESULT CreateDeviceResources(HWND hwnd) {
    if (g_pRT) return S_OK;
    RECT rc; GetClientRect(hwnd, &rc);
    return g_pD2DFactory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(hwnd,
            D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
        &g_pRT);
}

static void DiscardDeviceResources() {
    if (g_pRT) { g_pRT->Release(); g_pRT = nullptr; }
}

static void InitD2D(HWND hwnd) {
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_pD2DFactory);
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(&g_pDWriteFactory));

    auto mk = [](const wchar_t* font, DWRITE_FONT_WEIGHT w, float size,
                 IDWriteTextFormat** out) {
        g_pDWriteFactory->CreateTextFormat(font, nullptr, w,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            size, L"ru-ru", out);
    };
    mk(L"Segoe UI", DWRITE_FONT_WEIGHT_BOLD,       26.0f, &g_fmtTitle);
    mk(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL,     13.0f, &g_fmtSub);
    mk(L"Segoe UI", DWRITE_FONT_WEIGHT_NORMAL,     14.0f, &g_fmtBody);
    mk(L"Consolas", DWRITE_FONT_WEIGHT_BOLD,       13.0f, &g_fmtBadge);
    mk(L"Segoe UI", DWRITE_FONT_WEIGHT_SEMI_BOLD,  14.0f, &g_fmtBtn);
    mk(L"Segoe UI Emoji", DWRITE_FONT_WEIGHT_NORMAL, 22.0f, &g_fmtIcon);

    CreateDeviceResources(hwnd);
}

static void DrawStr(const std::wstring& s, IDWriteTextFormat* fmt,
                    ID2D1Brush* brush, float x, float y, float w, float h) {
    g_pRT->DrawTextW(s.c_str(), (UINT32)s.size(), fmt,
        D2D1::RectF(x, y, x + w, y + h), brush);
}

// ---------------- Рендер ----------------
static void OnRender(HWND hwnd) {
    if (FAILED(CreateDeviceResources(hwnd))) return;
    auto rt = g_pRT;

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0x0E1017));

    ID2D1SolidColorBrush* bWhite = nullptr;   rt->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF), &bWhite);
    ID2D1SolidColorBrush* bMuted = nullptr;   rt->CreateSolidColorBrush(D2D1::ColorF(0x8A90A6), &bMuted);
    ID2D1SolidColorBrush* bGreen = nullptr;   rt->CreateSolidColorBrush(D2D1::ColorF(0x4ADE80), &bGreen);
    ID2D1SolidColorBrush* bStatus= nullptr;   rt->CreateSolidColorBrush(D2D1::ColorF(0x1A2A1F), &bStatus);
    ID2D1SolidColorBrush* bAccent= nullptr;   rt->CreateSolidColorBrush(D2D1::ColorF(g_addHover?0x8D70FF:0x7C5CFF), &bAccent);
    ID2D1SolidColorBrush* bNeut  = nullptr;   rt->CreateSolidColorBrush(D2D1::ColorF(g_editHover?0x232838:0x1B1F2E), &bNeut);
    ID2D1SolidColorBrush* bDang  = nullptr;   rt->CreateSolidColorBrush(D2D1::ColorF(g_delHover?0x2F1A22:0x1F1420), &bDang);
    ID2D1SolidColorBrush* bDangT = nullptr;   rt->CreateSolidColorBrush(D2D1::ColorF(0xF87171), &bDangT);
    ID2D1SolidColorBrush* bCard  = nullptr;   rt->CreateSolidColorBrush(D2D1::ColorF(0x131725), &bCard);
    ID2D1SolidColorBrush* bName  = nullptr;   rt->CreateSolidColorBrush(D2D1::ColorF(0xF1F3F9), &bName);
    ID2D1SolidColorBrush* bBadge = nullptr;   rt->CreateSolidColorBrush(D2D1::ColorF(0x7C5CFF), &bBadge);
    ID2D1SolidColorBrush* bOutline=nullptr;   rt->CreateSolidColorBrush(D2D1::ColorF(0x7C5CFF), &bOutline);

    // Header
    DrawStr(L"⚡  HotKey Manager", g_fmtTitle, bWhite, 28, 20, 600, 40);
    DrawStr(L"Управляйте ПК одной комбинацией клавиш", g_fmtSub, bMuted, 28, 60, 500, 22);

    // Статус
    rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(800, 34, 932, 66), 16, 16), bStatus);
    DrawStr(L"● активно", g_fmtBtn, bGreen, 812, 40, 120, 22);

    // Кнопки
    g_addRect  = { 28, 108, 168, 152 };
    g_editRect = { 180, 108, 320, 152 };
    g_delRect  = { 332, 108, 472, 152 };

    rt->FillRoundedRectangle(D2D1::RoundedRect(
        D2D1::RectF(28, 108, 168, 152), 10, 10), bAccent);
    DrawStr(L"＋  Добавить", g_fmtBtn, bWhite, 46, 118, 140, 24);

    rt->FillRoundedRectangle(D2D1::RoundedRect(
        D2D1::RectF(180, 108, 320, 152), 10, 10), bNeut);
    DrawStr(L"✎  Изменить", g_fmtBtn, bWhite, 200, 118, 140, 24);

    rt->FillRoundedRectangle(D2D1::RoundedRect(
        D2D1::RectF(332, 108, 472, 152), 10, 10), bDang);
    DrawStr(L"🗑  Удалить", g_fmtBtn, bDangT, 354, 118, 140, 24);

    // Карточка
    rt->FillRoundedRectangle(D2D1::RoundedRect(
        D2D1::RectF(28, 168, 932, 620), 18, 18), bCard);

    // Строки хоткеев (со скроллом)
    const float rowTop = 184;
    const float rowH = 72;
    const float rowGap = 8;
    const float cardBottom = 620;
    const float visibleH = cardBottom - rowTop - 16;

    int totalH = (int)(g_hotkeys.size() * (rowH + rowGap));
    g_maxScroll = totalH - (int)visibleH;
    if (g_maxScroll < 0) g_maxScroll = 0;
    if (g_scrollOffset > g_maxScroll) g_scrollOffset = g_maxScroll;
    if (g_scrollOffset < 0) g_scrollOffset = 0;

    D2D1_RECT_F clipRect = D2D1::RectF(40, rowTop, 920, cardBottom);
    rt->PushAxisAlignedClip(clipRect, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    float y = rowTop - (float)g_scrollOffset;
    for (size_t i = 0; i < g_hotkeys.size(); ++i) {
        if (y + rowH < rowTop) { y += rowH + rowGap; continue; }
        if (y > cardBottom) break;
        auto& hk = g_hotkeys[i];
        float rowH = 72;
        bool sel = (int)i == g_selectedIndex;
        bool hov = (int)i == g_hoverIndex;

        D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            D2D1::RectF(40, y, 920, y + rowH), 12, 12);

        ID2D1SolidColorBrush* bRow = nullptr;
        D2D1_COLOR_F rc = D2D1::ColorF(0x161A28);
        if (sel)      rc = D2D1::ColorF(0x1D2233);
        else if (hov) rc = D2D1::ColorF(0x1B2030);
        rt->CreateSolidColorBrush(rc, &bRow);
        rt->FillRoundedRectangle(rr, bRow);

        if (sel) rt->DrawRoundedRectangle(rr, bOutline, 1.5f);

        // Иконка
        std::wstring icon = L"🌐";
        if (hk.type == ActionType::App) icon = L"🚀";
        else if (hk.type == ActionType::Command) icon = L"⌨";
        DrawStr(icon, g_fmtIcon, bWhite, 54, y + 20, 44, 40);

        // Имя
        DrawStr(hk.name, g_fmtBody, bName, 108, y + 14, 480, 22);

        // Значение
        std::wstring desc = hk.value;
        if (desc.size() > 60) desc = desc.substr(0, 58) + L"…";
        DrawStr(desc, g_fmtSub, bMuted, 108, y + 38, 480, 20);

        // Бейдж
        std::wstring comboUp = hk.combo;
        for (auto& c : comboUp) c = towupper(c);
        rt->FillRoundedRectangle(D2D1::RoundedRect(
            D2D1::RectF(730, y + 18, 900, y + 54), 8, 8), bBadge);
        DrawStr(comboUp, g_fmtBadge, bWhite, 740, y + 26, 160, 22);

        bRow->Release();
        y += rowH + rowGap;
    }

    rt->PopAxisAlignedClip();

    if (g_hotkeys.empty()) {
        ID2D1SolidColorBrush* bEmpty = nullptr;
        rt->CreateSolidColorBrush(D2D1::ColorF(0x6B7189), &bEmpty);
        DrawStr(L"Пока нет ни одной горячей клавиши. Нажмите «＋ Добавить».",
                g_fmtBody, bEmpty, 60, 380, 860, 30);
        bEmpty->Release();
    }

    // Подвал
    DrawStr(std::to_wstring(g_hotkeys.size()) + L" горячих клавиш",
            g_fmtSub, bMuted, 28, 632, 300, 20);
    DrawStr(L"Двойной клик — редактировать", g_fmtSub, bMuted, 700, 632, 260, 20);

    // Cleanup brushes
    bWhite->Release(); bMuted->Release(); bGreen->Release(); bStatus->Release();
    bAccent->Release(); bNeut->Release(); bDang->Release(); bDangT->Release();
    bCard->Release(); bName->Release(); bBadge->Release(); bOutline->Release();

    HRESULT hr = rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) DiscardDeviceResources();
}

// ============================================================
//                  ДИАЛОГ ДОБАВЛЕНИЯ / РЕДАКТИРОВАНИЯ
// ============================================================
#define IDC_NAME    1001
#define IDC_COMBO   1002
#define IDC_TYPE    1003
#define IDC_VALUE   1004
#define IDC_BROWSE  1005
#define IDC_OK      1006
#define IDC_CANCEL  1007
#define IDC_RECORD  1008

// Трей
#define WM_TRAYICON     (WM_USER + 1)
#define IDM_TRAY_OPEN   2001
#define IDM_TRAY_EXIT   2002
#define IDM_TRAY_ADD    2003
#define TRAY_ICON_ID    1

struct DialogState {
    std::wstring name;
    std::wstring combo;
    ActionType   type = ActionType::Url;
    std::wstring value;
};

static DialogState g_dlgState;
static bool g_dlgOk = false;
static bool g_recording = false;
static std::vector<std::wstring> g_recordedKeys;
static HHOOK g_kbHook = nullptr;
static HWND  g_dlgHwnd = nullptr;

static const wchar_t* VkToName(UINT vk) {
    static wchar_t buf[32];
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        buf[0] = (wchar_t)vk; buf[1] = 0; return buf;
    }
    if (vk >= VK_F1 && vk <= VK_F24) {
        wsprintfW(buf, L"F%d", vk - VK_F1 + 1); return buf;
    }
    switch (vk) {
        case VK_SPACE:    return L"Space";
        case VK_RETURN:   return L"Enter";
        case VK_TAB:      return L"Tab";
        case VK_ESCAPE:   return L"Esc";
        case VK_BACK:     return L"Backspace";
        case VK_DELETE:   return L"Delete";
        case VK_INSERT:   return L"Insert";
        case VK_HOME:     return L"Home";
        case VK_END:      return L"End";
        case VK_PRIOR:    return L"PageUp";
        case VK_NEXT:     return L"PageDown";
        case VK_UP:       return L"Up";
        case VK_DOWN:     return L"Down";
        case VK_LEFT:     return L"Left";
        case VK_RIGHT:    return L"Right";
    }
    wsprintfW(buf, L"0x%02X", vk);
    return buf;
}

static bool IsModifierVk(UINT vk) {
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_MENU    || vk == VK_LMENU    || vk == VK_RMENU    ||
           vk == VK_SHIFT   || vk == VK_LSHIFT   || vk == VK_RSHIFT   ||
           vk == VK_LWIN    || vk == VK_RWIN;
}

static void UpdateComboField() {
    std::wstring combo;
    for (size_t i = 0; i < g_recordedKeys.size(); ++i) {
        if (i) combo += L"+";
        combo += g_recordedKeys[i];
    }
    SetDlgItemTextW(g_dlgHwnd, IDC_COMBO, combo.c_str());
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wp, LPARAM lp) {
    if (nCode == HC_ACTION && g_recording) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        UINT vk = kb->vkCode;

        if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) {
            // Esc отменяет запись
            if (vk == VK_ESCAPE) {
                g_recording = false;
                g_recordedKeys.clear();
                SetDlgItemTextW(g_dlgHwnd, IDC_COMBO, L"");
                return 1;
            }

            std::wstring name;
            if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL) name = L"Ctrl";
            else if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU)     name = L"Alt";
            else if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT)  name = L"Shift";
            else if (vk == VK_LWIN || vk == VK_RWIN)                        name = L"Win";
            else name = VkToName(vk);

            bool exists = false;
            for (auto& k : g_recordedKeys) if (k == name) { exists = true; break; }
            if (!exists) g_recordedKeys.push_back(name);

            UpdateComboField();
            return 1;
        }
        if (wp == WM_KEYUP || wp == WM_SYSKEYUP) return 1;
    }
    return CallNextHookEx(g_kbHook, nCode, wp, lp);
}

static LRESULT CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_dlgHwnd = hDlg;

        CreateWindowExW(0, L"STATIC", L"Название:",
            WS_CHILD|WS_VISIBLE, 20, 16, 460, 18, hDlg, nullptr, nullptr, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_dlgState.name.c_str(),
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 20, 38, 460, 26,
            hDlg, (HMENU)IDC_NAME, nullptr, nullptr);

        CreateWindowExW(0, L"STATIC", L"Комбинация клавиш:",
            WS_CHILD|WS_VISIBLE, 20, 76, 460, 18, hDlg, nullptr, nullptr, nullptr);

        HWND hCombo = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_dlgState.combo.c_str(),
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 20, 98, 340, 26,
            hDlg, (HMENU)IDC_COMBO, nullptr, nullptr);
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        SendMessageW(hCombo, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hRec = CreateWindowExW(0, L"BUTTON", L"🎙 Записать",
            WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON, 370, 98, 110, 26,
            hDlg, (HMENU)IDC_RECORD, nullptr, nullptr);

        CreateWindowExW(0, L"STATIC",
            L"Нажмите «Записать» и нажмите нужные клавиши (Esc — отмена)",
            WS_CHILD|WS_VISIBLE, 20, 128, 460, 18, hDlg, nullptr, nullptr, nullptr);

        CreateWindowExW(0, L"STATIC", L"Тип действия:",
            WS_CHILD|WS_VISIBLE, 20, 156, 460, 18, hDlg, nullptr, nullptr, nullptr);
        HWND hType = CreateWindowExW(0, L"COMBOBOX", L"",
            WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,
            20, 178, 460, 200, hDlg, (HMENU)IDC_TYPE, nullptr, nullptr);
        SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"🌐  Открыть сайт (URL)");
        SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"🚀  Запустить приложение / файл");
        SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"⌨  Выполнить команду (cmd)");
        SendMessageW(hType, CB_SETCURSEL, (WPARAM)(int)g_dlgState.type, 0);

        CreateWindowExW(0, L"STATIC", L"Значение:",
            WS_CHILD|WS_VISIBLE, 20, 218, 460, 18, hDlg, nullptr, nullptr, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_dlgState.value.c_str(),
            WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, 20, 240, 350, 26,
            hDlg, (HMENU)IDC_VALUE, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"📂 Обзор",
            WS_CHILD|WS_VISIBLE, 380, 240, 100, 26,
            hDlg, (HMENU)IDC_BROWSE, nullptr, nullptr);

        CreateWindowExW(0, L"BUTTON", L"Отмена",
            WS_CHILD|WS_VISIBLE, 250, 300, 110, 34,
            hDlg, (HMENU)IDC_CANCEL, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"✓ Сохранить",
            WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, 370, 300, 110, 34,
            hDlg, (HMENU)IDC_OK, nullptr, nullptr);
        return 0;
    }

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        if (!g_recording) break;

        UINT vk = (UINT)wp;

        if (vk == VK_ESCAPE) {
            g_recording = false;
            g_recordedKeys.clear();
            SetDlgItemTextW(hDlg, IDC_COMBO, L"");
            SetWindowTextW(GetDlgItem(hDlg, IDC_RECORD), L"🎙 Записать");
            return 0;
        }

        std::wstring name;
        switch (vk) {
            case VK_LCONTROL: case VK_RCONTROL: case VK_CONTROL: name = L"Ctrl";  break;
            case VK_LMENU:    case VK_RMENU:    case VK_MENU:    name = L"Alt";   break;
            case VK_LSHIFT:   case VK_RSHIFT:   case VK_SHIFT:   name = L"Shift"; break;
            case VK_LWIN:     case VK_RWIN:                      name = L"Win";   break;
            default: name = VkToName(vk); break;
        }

        bool isModOnly = (name == L"Ctrl" || name == L"Alt" ||
                          name == L"Shift" || name == L"Win");

        bool exists = false;
        for (auto& k : g_recordedKeys) if (k == name) { exists = true; break; }
        if (!exists) {
            if (isModOnly) {
                g_recordedKeys.insert(g_recordedKeys.begin(), name);
            } else {
                g_recordedKeys.push_back(name);
            }
        }

        std::wstring combo;
        for (size_t i = 0; i < g_recordedKeys.size(); ++i) {
            if (i) combo += L"+";
            combo += g_recordedKeys[i];
        }
        SetDlgItemTextW(hDlg, IDC_COMBO, combo.c_str());

        if (!isModOnly && g_recordedKeys.size() >= 2) {
            g_recording = false;
            SetWindowTextW(GetDlgItem(hDlg, IDC_RECORD), L"🎙 Записать");
        }

        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDC_OK) {
            wchar_t buf[512];
            GetDlgItemTextW(hDlg, IDC_NAME, buf, 512);  g_dlgState.name = buf;
            GetDlgItemTextW(hDlg, IDC_COMBO, buf, 512); g_dlgState.combo = buf;
            GetDlgItemTextW(hDlg, IDC_VALUE, buf, 512); g_dlgState.value = buf;
            HWND hC = GetDlgItem(hDlg, IDC_TYPE);
            g_dlgState.type = (ActionType)SendMessageW(hC, CB_GETCURSEL, 0, 0);

            if (g_dlgState.name.empty() || g_dlgState.combo.empty() || g_dlgState.value.empty()) {
                MessageBoxW(hDlg, L"Заполните все поля.", L"Ошибка", MB_ICONWARNING);
                return 0;
            }
            UINT m, v;
            if (!ParseCombo(g_dlgState.combo, m, v)) {
                MessageBoxW(hDlg, L"Не удалось распознать комбинацию.",
                            L"Ошибка", MB_ICONWARNING);
                return 0;
            }
            g_dlgOk = true;
            DestroyWindow(hDlg);
        }
        else if (id == IDC_CANCEL) {
            g_dlgOk = false;
            DestroyWindow(hDlg);
        }
        else if (id == IDC_BROWSE) {
            wchar_t file[MAX_PATH] = {};
            OPENFILENAMEW ofn = { sizeof(ofn) };
            ofn.hwndOwner = hDlg;
            ofn.lpstrFile = file;
            ofn.nMaxFile  = MAX_PATH;
            ofn.lpstrFilter = L"Все файлы\0*.*\0Исполняемые\0*.exe\0";
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn))
                SetDlgItemTextW(hDlg, IDC_VALUE, file);
        }
        else if (id == IDC_RECORD) {
            if (!g_recording) {
                g_recording = true;
                g_recordedKeys.clear();
                SetDlgItemTextW(hDlg, IDC_COMBO, L"...");
                SetWindowTextW(GetDlgItem(hDlg, IDC_RECORD), L"⏺ Слушаю...");
                SetFocus(GetDlgItem(hDlg, IDC_COMBO));
            } else {
                g_recording = false;
                SetWindowTextW(GetDlgItem(hDlg, IDC_RECORD), L"🎙 Записать");
            }
        }
        return 0;
    }

    case WM_CLOSE:
        g_dlgOk = false;
        DestroyWindow(hDlg);
        return 0;

    case WM_DESTROY:
        g_dlgHwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hDlg, msg, wp, lp);
}

static bool ShowHotkeyDialog(HWND parent, DialogState& state) {
    g_dlgState = state;
    g_dlgOk = false;
    g_recording = false;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.lpfnWndProc   = DlgProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"HotkeyDlgClass";
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        registered = true;
    }

    int W = 520, H = 400;
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"HotkeyDlgClass", L"Настройка горячей клавиши",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        (sw-W)/2, (sh-H)/2, W, H,
        parent, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (!hDlg) return false;

    ShowWindow(hDlg, SW_SHOW);
    EnableWindow(parent, FALSE);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.hwnd == hDlg || IsChild(hDlg, msg.hwnd)) {
            if (!IsDialogMessageW(hDlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    if (g_dlgOk) state = g_dlgState;
    return g_dlgOk;
}

// ---------------- Трей ----------------
static void AddTrayIcon(HWND hwnd) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = TRAY_ICON_ID;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon            = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"HotKey Manager — двойной клик для открытия");

    if (Shell_NotifyIconW(NIM_ADD, &g_nid)) {
        g_trayAdded = true;
    }
}

// ---------------- UI-обработчики ----------------
static void AddHotkey(HWND hwnd) {
    DialogState st;
    if (ShowHotkeyDialog(hwnd, st)) {
        HotkeyItem hk;
        hk.id    = g_nextId++;
        hk.name  = st.name;
        hk.combo = st.combo;
        hk.type  = st.type;
        hk.value = st.value;
        g_hotkeys.push_back(hk);
        SaveConfig();
        RegisterAll();
        g_scrollOffset = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

static void EditHotkey(HWND hwnd) {
    if (g_selectedIndex < 0 || g_selectedIndex >= (int)g_hotkeys.size()) {
        MessageBoxW(hwnd, L"Выберите запись для редактирования.",
                    L"HotKey Manager", MB_ICONINFORMATION);
        return;
    }
    auto& hk = g_hotkeys[g_selectedIndex];
    DialogState st{ hk.name, hk.combo, hk.type, hk.value };
    if (ShowHotkeyDialog(hwnd, st)) {
        hk.name  = st.name;
        hk.combo = st.combo;
        hk.type  = st.type;
        hk.value = st.value;
        SaveConfig();
        RegisterAll();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

static void DeleteHotkey(HWND hwnd) {
    if (g_selectedIndex < 0 || g_selectedIndex >= (int)g_hotkeys.size()) {
        MessageBoxW(hwnd, L"Выберите запись для удаления.",
                    L"HotKey Manager", MB_ICONINFORMATION);
        return;
    }
    auto& hk = g_hotkeys[g_selectedIndex];
    std::wstring q = L"Удалить «" + hk.name + L"»?";
    if (MessageBoxW(hwnd, q.c_str(), L"Удаление",
                    MB_YESNO | MB_ICONQUESTION) == IDYES) {
        g_hotkeys.erase(g_hotkeys.begin() + g_selectedIndex);
        g_selectedIndex = -1;
        SaveConfig();
        RegisterAll();
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

static int HitTestRow(int mouseY) {
    const float rowTop = 184;
    const float rowH = 72;
    const float rowGap = 8;

    // Учитываем скролл
    float y = rowTop - (float)g_scrollOffset;
    for (size_t i = 0; i < g_hotkeys.size(); ++i) {
        if (mouseY >= y && mouseY <= y + rowH) return (int)i;
        y += rowH + rowGap;
    }
    return -1;
}

static bool PointIn(const RECT& r, int x, int y) {
    return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
}

// ---------------- WndProc ----------------
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        InitD2D(hwnd);
        LoadConfig();
        RegisterAll();
        AddTrayIcon(hwnd);
        return 0;

    case WM_HOTKEY: {
        int id = (int)wp;
        for (auto& hk : g_hotkeys) {
            if (hk.id == id) { ExecuteAction(hk); break; }
        }
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        OnRender(hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        if (g_pRT) g_pRT->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
        return 0;

    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        int steps = delta / WHEEL_DELTA;
        g_scrollOffset -= steps * 60;
        if (g_scrollOffset < 0) g_scrollOffset = 0;
        if (g_scrollOffset > g_maxScroll) g_scrollOffset = g_maxScroll;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        bool needRepaint = false;

        bool ah = PointIn(g_addRect, x, y);
        bool eh = PointIn(g_editRect, x, y);
        bool dh = PointIn(g_delRect, x, y);
        if (ah != g_addHover || eh != g_editHover || dh != g_delHover) {
            g_addHover = ah; g_editHover = eh; g_delHover = dh;
            needRepaint = true;
        }

        int hov = HitTestRow(y);
        if (hov != g_hoverIndex) { g_hoverIndex = hov; needRepaint = true; }

        if (needRepaint) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        if (PointIn(g_addRect, x, y))  { AddHotkey(hwnd); return 0; }
        if (PointIn(g_editRect, x, y)) { EditHotkey(hwnd); return 0; }
        if (PointIn(g_delRect, x, y))  { DeleteHotkey(hwnd); return 0; }

        int row = HitTestRow(y);
        if (row >= 0) {
            g_selectedIndex = row;
            InvalidateRect(hwnd, nullptr, FALSE);
        } else {
            g_selectedIndex = -1;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        int y = GET_Y_LPARAM(lp);
        int row = HitTestRow(y);
        if (row >= 0) { g_selectedIndex = row; EditHotkey(hwnd); }
        return 0;
    }

    case WM_CLOSE:
        // Сворачиваем в трей вместо закрытия
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_TRAYICON: {
        if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) {
            // Правый клик — контекстное меню
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN, L"Открыть");
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_ADD,  L"Добавить хоткей");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Выход");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
        }
        else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
            // Двойной клик — показать окно
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == IDM_TRAY_OPEN) {
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        else if (id == IDM_TRAY_ADD) {
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            AddHotkey(hwnd);
        }
        else if (id == IDM_TRAY_EXIT) {
            g_reallyExit = true;
            // Убираем иконку из трея
            if (g_trayAdded) {
                Shell_NotifyIconW(NIM_DELETE, &g_nid);
                g_trayAdded = false;
            }
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_DESTROY:
        UnregisterAll();
        if (g_trayAdded) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            g_trayAdded = false;
        }
        DiscardDeviceResources();
        if (g_pD2DFactory) g_pD2DFactory->Release();
        if (g_pDWriteFactory) g_pDWriteFactory->Release();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------- Точка входа ----------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    // --- Запрет нескольких копий ---
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"HotKeyManager_SingleInstance_Mutex_v1");
    if (!hMutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        // Уже запущено — показываем уже существующее окно и выходим
        HWND existing = FindWindowW(L"HotKeyManagerClass", nullptr);
        if (existing) {
            ShowWindow(existing, SW_SHOW);
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        CloseHandle(hMutex);
        return 0;
    }
    // --- /Запрет нескольких копий ---

    g_configPath = GetConfigPath();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"HotKeyManagerClass";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int W = 960, H = 680;

    HWND hwnd = CreateWindowExW(0, L"HotKeyManagerClass", L"HotKey Manager",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        (sw - W)/2, (sh - H)/2, W, H,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return (int)msg.wParam;
}