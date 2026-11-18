// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2026 BetterNMM Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// BetterNMM's setup and uninstaller: one small program with no DLLs of its own. The wizard is drawn in the launcher's
// colours; installing for everyone runs the file work in an elevated copy of this program that reports back to it.
//
//   setup.exe [--silent] [--dir <folder>] [--scope user|machine] [--desktop 0|1] [--startmenu 0|1] [--pin 0|1]
//             [--nxm 0|1] [--launch 0|1] [--test-root <folder>]
//   uninstall.exe [--silent]

#include <windows.h>

#include <commctrl.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Files.h"
#include "Install.h"
#include "Payload.h"
#include "VortexState.h"

using namespace Gdiplus;

namespace {

constexpr UINT WM_SETUP_PROGRESS = WM_APP + 1;  ///< wParam: permille
constexpr UINT WM_SETUP_DONE = WM_APP + 2;      ///< wParam: 1 when it worked
constexpr ULONG_PTR COPY_STATUS = 1;
constexpr ULONG_PTR COPY_ERROR = 2;
constexpr int k_logoResource = 101;

// ---------------------------------------------------------------------------------------------------------------------
// command line

struct Args {
    bool silent = false;
    bool uninstall = false;
    bool fromTemp = false;
    std::wstring worker;  ///< "install" or "uninstall": the elevated part, reporting to --hwnd
    std::wstring dir;
    std::wstring scope;
    std::wstring options;
    std::wstring testRoot;
    DWORD parent = 0;  ///< the uninstaller that started this copy of it
    HWND report = nullptr;
    std::map<std::wstring, bool> flags;
};

Args parseArgs()
{
    Args args;
    int count = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; i < count; i++) {
        const std::wstring arg = argv[i];
        auto value = [&]() { return i + 1 < count ? std::wstring(argv[++i]) : std::wstring(); };
        if (arg == L"--silent" || arg == L"/S") {
            args.silent = true;
        } else if (arg == L"--uninstall") {
            args.uninstall = true;
        } else if (arg == L"--from-temp") {
            args.fromTemp = true;
        } else if (arg == L"--worker") {
            args.worker = value();
        } else if (arg == L"--dir") {
            args.dir = value();
        } else if (arg == L"--scope") {
            args.scope = value();
        } else if (arg == L"--options") {
            args.options = value();
        } else if (arg == L"--test-root") {
            args.testRoot = value();
        } else if (arg == L"--parent") {
            args.parent = static_cast<DWORD>(wcstoul(value().c_str(), nullptr, 10));
        } else if (arg == L"--hwnd") {
            args.report = reinterpret_cast<HWND>(static_cast<INT_PTR>(wcstoll(value().c_str(), nullptr, 10)));
        } else if (arg == L"--desktop" || arg == L"--startmenu" || arg == L"--pin" || arg == L"--nxm" || arg == L"--launch") {
            args.flags[arg.substr(2)] = value() != L"0";
        }
    }
    LocalFree(argv);
    // a trailing backslash would end the quoted argument early when passed on
    while (args.dir.size() > 3 && (args.dir.back() == L'\\' || args.dir.back() == L'/')) {
        args.dir.pop_back();
    }
    return args;
}

std::wstring quote(const std::wstring& text)
{
    return L"\"" + text + L"\"";
}

std::wstring selfPath()
{
    wchar_t path[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, path, MAX_PATH * 2);
    return path;
}

Setup::Options applyFlags(Setup::Options options, const Args& args)
{
    auto pick = [&](const wchar_t* name, bool& field) {
        if (const auto it = args.flags.find(name); it != args.flags.end()) {
            field = it->second;
        }
    };
    pick(L"desktop", options.desktop);
    pick(L"startmenu", options.startMenu);
    pick(L"pin", options.pin);
    pick(L"nxm", options.nxm);
    pick(L"launch", options.launch);
    return options;
}

/// Runs this program elevated with @p params and waits for it; -1 when Windows did not give the rights
int runElevated(const std::wstring& params, HANDLE* process = nullptr)
{
    const auto self = selfPath();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = self.c_str();
    info.lpParameters = params.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess) {
        return -1;
    }
    if (process) {
        *process = info.hProcess;
        return 0;
    }
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    return static_cast<int>(code);
}

void copyTo(HWND window, ULONG_PTR kind, const std::wstring& text)
{
    if (!window) {
        return;
    }
    COPYDATASTRUCT data{ kind, static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)), const_cast<wchar_t*>(text.c_str()) };
    SendMessageW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data));
}

// ---------------------------------------------------------------------------------------------------------------------
// look

struct Palette {
    Color bg, panel, card, border, borderStrong, text, dim, accent, accentHover, accentText, danger, dangerBg, hover;
};

bool windowsUsesLightTheme()
{
    DWORD value = 0;
    DWORD size = sizeof(value);
    return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                        RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS &&
           value != 0;
}

Palette makePalette(bool light)
{
    // the launcher's Dark and Bright themes
    if (light) {
        return { Color(243, 244, 247), Color(233, 235, 240), Color(255, 255, 255), Color(211, 215, 223), Color(176, 182, 194),
                 Color(27, 30, 36),    Color(92, 99, 114),   Color(242, 161, 46),  Color(232, 146, 26),  Color(26, 18, 6),
                 Color(198, 40, 40),   Color(253, 236, 236), Color(228, 231, 237) };
    }
    return { Color(30, 34, 43),    Color(22, 25, 32),    Color(36, 41, 52),   Color(53, 59, 73),   Color(78, 86, 104),
             Color(232, 234, 240), Color(155, 162, 179), Color(242, 161, 46), Color(245, 180, 82), Color(26, 18, 6),
             Color(255, 120, 120), Color(58, 35, 40),    Color(44, 50, 63) };
}

COLORREF ref(const Color& c)
{
    return RGB(c.GetR(), c.GetG(), c.GetB());
}

void roundRect(GraphicsPath& path, REAL x, REAL y, REAL w, REAL h, REAL r)
{
    const REAL d = r * 2;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}

void fillRound(Graphics& g, const Color& color, REAL x, REAL y, REAL w, REAL h, REAL r)
{
    GraphicsPath path;
    roundRect(path, x, y, w, h, r);
    SolidBrush brush(color);
    g.FillPath(&brush, &path);
}

void strokeRound(Graphics& g, const Color& color, REAL width, REAL x, REAL y, REAL w, REAL h, REAL r)
{
    GraphicsPath path;
    roundRect(path, x + width / 2, y + width / 2, w - width, h - width, r);
    Pen pen(color, width);
    g.DrawPath(&pen, &path);
}

// ---------------------------------------------------------------------------------------------------------------------
// the wizard

enum class Page { Welcome, Location, Options, Vortex, Working, Done, Failed, Uninstall, Uninstalling, Removed };
enum class Kind { Primary, Secondary, Ghost, Check, Card };

struct Control {
    Kind kind = Kind::Secondary;
    std::wstring title;
    std::wstring detail;
    bool checked = false;
    bool hover = false;
};

class Wizard {
   public:
    Wizard(const Args& args, bool uninstall) : m_args(args), m_uninstalling(uninstall) {}
    int run();

   private:
    static LRESULT CALLBACK proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK controlProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data);
    /// The scrolling list of Vortex profiles: hands its rows' drawing and clicks to the wizard
    static LRESULT CALLBACK rowsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam);

    HWND makeControl(Kind kind, const std::wstring& title, const std::wstring& detail = std::wstring(), HWND parent = nullptr);
    /// Whether the wizard offers to bring Vortex profiles over: a new install, with profiles of games BetterNMM knows
    bool offersVortex() const { return !m_existing.found && !m_vortexRows.empty(); }
    void layoutRows();
    void scrollRows(int to);
    void scrollRowIntoView(HWND row);
    /// Leaves the chosen Vortex profiles for BetterNMM, which brings them over when it starts
    bool writeVortexRequest(std::wstring& error) const;
    void createControls();
    void makeFonts();
    void layout();
    void show(Page page);
    void paint(HDC dc);
    void paintPage(Graphics& g);
    void drawControl(const DRAWITEMSTRUCT& item);
    REAL s(REAL v) const { return v * m_scale; }
    /// The footer's top edge: the footer and its buttons stay at the bottom whatever the window's size
    REAL footerTop() const;
    int si(double v) const { return static_cast<int>(v * m_scale + 0.5); }
    REAL text(Graphics& g, const std::wstring& t, const Font& font, const Color& color, REAL x, REAL y, REAL w, REAL h = 1000) const;

    void primary();
    void back();
    void cancel();
    void startWork();
    void workDone(bool ok);
    void browse();
    void setScope(Setup::Scope scope);
    void refreshSpace();
    bool checkRunning();
    std::wstring folder() const;

    Args m_args;
    bool m_uninstalling = false;
    HWND m_window = nullptr;
    Page m_page = Page::Welcome;
    REAL m_scale = 1;
    Palette m_pal{};
    bool m_light = false;
    std::unique_ptr<Font> m_title, m_body, m_bodyBold, m_small, m_button;
    HFONT m_editFont = nullptr;
    HBRUSH m_editBrush = nullptr;
    std::unique_ptr<Bitmap> m_logo;
    std::map<HWND, Control> m_controls;

    HWND m_back = nullptr, m_next = nullptr, m_cancel = nullptr;
    HWND m_cardUser = nullptr, m_cardMachine = nullptr, m_dir = nullptr, m_browse = nullptr;
    HWND m_desktop = nullptr, m_startMenu = nullptr, m_pin = nullptr, m_nxm = nullptr, m_launch = nullptr;
    HWND m_rows = nullptr;
    std::vector<VortexState::Profile> m_vortex;  ///< the profiles Vortex has of games BetterNMM knows
    std::vector<HWND> m_vortexRows;              ///< one check per profile, in the same order
    std::vector<std::string> m_vortexChosen;     ///< ids of the profiles to bring over
    int m_rowsScroll = 0;

    Payload::Index m_index;
    Setup::Existing m_existing;
    Setup::Plan m_plan;
    bool m_dirEdited = false;
    double m_progress = 0;
    std::wstring m_status;
    std::wstring m_error;
    std::wstring m_notes;  ///< what the finished page says besides "done"
    bool m_working = false;
    std::wstring m_space;
    bool m_spaceShort = false;
};

int Wizard::run()
{
    m_plan.where.testRoot = m_args.testRoot;
    m_plan.setupExe = selfPath();
    if (m_uninstalling) {
        m_existing = Setup::findExisting(m_plan.where);
        if (!m_args.dir.empty()) {
            m_existing.dir = m_args.dir;
        }
        m_plan.dir = m_existing.dir;
        m_plan.scope = m_existing.scope;
    } else {
        std::wstring error;
        if (!Payload::read(m_plan.setupExe, m_index, error)) {
            MessageBoxW(nullptr, error.c_str(), L"BetterNMM Setup", MB_ICONERROR);
            return 1;
        }
        m_existing = Setup::findExisting(m_plan.where);
        m_plan.options = applyFlags(m_existing.found ? m_existing.options : Setup::Options{}, m_args);
        m_plan.scope = m_existing.found ? m_existing.scope : (m_args.scope == L"machine" ? Setup::Scope::Machine : Setup::Scope::User);
        m_plan.dir = m_existing.found ? m_existing.dir : (m_args.dir.empty() ? Setup::defaultDir(m_plan.scope, m_plan.where) : m_args.dir);
        m_dirEdited = !m_args.dir.empty();
        if (!m_existing.found) {
            // only read: Vortex and its files stay exactly as they are
            auto database = VortexState::defaultDatabase();
            if (m_plan.where.testing() && Files::exists(m_plan.where.testRoot + L"\\Vortex\\state.v2\\CURRENT")) {
                database = m_plan.where.testRoot + L"\\Vortex\\state.v2";
            }
            std::map<std::string, std::string> entries;
            std::string vortexError;
            if (!database.empty() && Files::exists(database + L"\\CURRENT")) {
                if (VortexState::readDatabase(database, entries, vortexError)) {
                    m_vortex = VortexState::profiles(entries);
                } else {
                    Setup::log(L"Vortex's profiles could not be read: " + Files::wide(vortexError));
                }
            }
        }
    }

    m_light = windowsUsesLightTheme();
    m_pal = makePalette(m_light);
    // the logo, a PNG among this program's resources
    if (HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(k_logoResource), RT_RCDATA)) {
        if (HGLOBAL loaded = LoadResource(nullptr, resource)) {
            const auto* data = static_cast<const BYTE*>(LockResource(loaded));
            if (IStream* stream = SHCreateMemStream(data, SizeofResource(nullptr, resource))) {
                m_logo.reset(Bitmap::FromStream(stream));
                stream->Release();
            }
        }
    }

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = &Wizard::proc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hIcon = LoadIconW(cls.hInstance, MAKEINTRESOURCEW(1));
    cls.lpszClassName = L"BetterNMMSetup";
    RegisterClassExW(&cls);

    // sized for the monitor it opens on
    POINT cursor;
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    UINT dpiX = 96, dpiY = 96;
    using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
    if (HMODULE shcore = LoadLibraryW(L"shcore.dll")) {
        if (auto fn = reinterpret_cast<GetDpiForMonitorFn>(GetProcAddress(shcore, "GetDpiForMonitor"))) {
            fn(monitor, 0, &dpiX, &dpiY);
        }
    }
    m_scale = dpiX / 96.0f;
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    RECT frame{ 0, 0, si(680), si(460) };
    AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpiX);
    MONITORINFO info{ sizeof(info) };
    GetMonitorInfoW(monitor, &info);
    const int width = frame.right - frame.left;
    const int height = frame.bottom - frame.top;
    const int x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
    const int y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
    const auto caption = m_uninstalling ? L"Uninstall BetterNMM" : L"BetterNMM Setup";
    m_window = CreateWindowExW(0, cls.lpszClassName, caption, style, x, y, width, height, nullptr, nullptr, cls.hInstance, this);
    if (!m_window) {
        return 1;
    }
    BOOL dark = m_light ? FALSE : TRUE;
    DwmSetWindowAttribute(m_window, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    COLORREF captionColor = ref(m_pal.bg);
    DwmSetWindowAttribute(m_window, 35 /* DWMWA_CAPTION_COLOR */, &captionColor, sizeof(captionColor));

    makeFonts();
    createControls();
    show(m_uninstalling ? Page::Uninstall : Page::Welcome);
    ShowWindow(m_window, SW_SHOWNORMAL);
    SetForegroundWindow(m_window);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(m_window, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

void Wizard::makeFonts()
{
    const REAL px = m_scale;
    m_title = std::make_unique<Font>(L"Segoe UI Semibold", 22 * px, FontStyleRegular, UnitPixel);
    m_body = std::make_unique<Font>(L"Segoe UI", 14 * px, FontStyleRegular, UnitPixel);
    m_bodyBold = std::make_unique<Font>(L"Segoe UI Semibold", 14 * px, FontStyleRegular, UnitPixel);
    m_small = std::make_unique<Font>(L"Segoe UI", 12.5f * px, FontStyleRegular, UnitPixel);
    m_button = std::make_unique<Font>(L"Segoe UI Semibold", 13.5f * px, FontStyleRegular, UnitPixel);
    if (m_editFont) {
        DeleteObject(m_editFont);
    }
    m_editFont = CreateFontW(-si(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (!m_editBrush) {
        m_editBrush = CreateSolidBrush(ref(m_pal.card));
    }
    if (m_dir) {
        SendMessageW(m_dir, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFont), TRUE);
    }
}

HWND Wizard::makeControl(Kind kind, const std::wstring& title, const std::wstring& detail, HWND parent)
{
    HWND control = CreateWindowExW(0, L"BUTTON", title.c_str(), WS_CHILD | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10,
                                   parent ? parent : m_window, nullptr, GetModuleHandleW(nullptr), nullptr);
    m_controls[control] = Control{ kind, title, detail, false, false };
    SetWindowSubclass(control, &Wizard::controlProc, 1, reinterpret_cast<DWORD_PTR>(this));
    return control;
}

void Wizard::createControls()
{
    m_cancel = makeControl(Kind::Ghost, L"Cancel");
    m_back = makeControl(Kind::Secondary, L"Back");
    m_next = makeControl(Kind::Primary, L"Next");
    m_cardUser = makeControl(Kind::Card, L"Just for me", L"In your user folder. No administrator rights needed.");
    m_cardMachine = makeControl(Kind::Card, L"Everyone on this PC", L"In Program Files. Windows asks for administrator rights.");
    m_dir = CreateWindowExW(0, L"EDIT", m_plan.dir.c_str(), WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, m_window, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
    SendMessageW(m_dir, WM_SETFONT, reinterpret_cast<WPARAM>(m_editFont), TRUE);
    SendMessageW(m_dir, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(si(4), si(4)));
    m_browse = makeControl(Kind::Secondary, L"Browse...");
    m_desktop = makeControl(Kind::Check, L"Desktop shortcut", L"A BetterNMM icon on your desktop.");
    m_startMenu = makeControl(Kind::Check, L"Start menu shortcut", L"BetterNMM in the Start menu and in Windows search.");
    m_pin = makeControl(Kind::Check, L"Pin to the taskbar", L"Windows asks you to confirm. Needs the Start menu shortcut.");
    m_nxm = makeControl(Kind::Check, L"Open Nexus Mods download links with BetterNMM",
                        L"The \"Mod Manager Download\" buttons on nexusmods.com go to BetterNMM instead of Vortex or Mod Organizer.");
    m_launch = makeControl(Kind::Check, L"Start BetterNMM now");
    m_controls[m_desktop].checked = m_plan.options.desktop;
    m_controls[m_startMenu].checked = m_plan.options.startMenu;
    m_controls[m_pin].checked = m_plan.options.pin;
    m_controls[m_nxm].checked = m_plan.options.nxm;
    m_controls[m_launch].checked = m_plan.options.launch;
    m_controls[m_cardUser].checked = m_plan.scope == Setup::Scope::User;
    m_controls[m_cardMachine].checked = m_plan.scope == Setup::Scope::Machine;
    if (!m_vortex.empty() && !m_existing.found) {
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.lpfnWndProc = &Wizard::rowsProc;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.lpszClassName = L"BetterNMMSetupRows";
        RegisterClassExW(&cls);
        // a control parent, so Tab moves through the rows like through the other options
        m_rows = CreateWindowExW(WS_EX_CONTROLPARENT, cls.lpszClassName, nullptr, WS_CHILD | WS_VSCROLL | WS_CLIPCHILDREN, 0, 0, 10, 10,
                                 m_window, nullptr, cls.hInstance, nullptr);
        SetWindowLongPtrW(m_rows, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        SetWindowTheme(m_rows, m_light ? L"Explorer" : L"DarkMode_Explorer", nullptr);
        std::vector<std::string> gamesTicked;
        for (const auto& profile : m_vortex) {
            const auto* game = VortexState::gameByVortexId(profile.gameId);
            std::wstring detail = std::to_wstring(profile.enabledMods) + (profile.enabledMods == 1 ? L" mod enabled" : L" mods enabled");
            if (profile.lastActivated > 0) {
                // ms since 1970 to the 100 ns ticks since 1601 Windows counts in
                const auto ticks = static_cast<ULONGLONG>(profile.lastActivated) * 10000ull + 116444736000000000ull;
                FILETIME utc{ static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32) };
                FILETIME local;
                SYSTEMTIME date;
                wchar_t text[128];
                if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &date) &&
                    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &date, nullptr, text, 128, nullptr) > 0) {
                    detail += std::wstring(L", last used ") + text;
                }
            }
            HWND row = makeControl(Kind::Check, Files::wide(game->name) + L": " + Files::wide(profile.name), detail, m_rows);
            ShowWindow(row, SW_SHOW);  // the list shows and hides with its page
            // the profile used last of each game is the likeliest one to want
            if (std::find(gamesTicked.begin(), gamesTicked.end(), profile.gameId) == gamesTicked.end()) {
                gamesTicked.push_back(profile.gameId);
                m_controls[row].checked = true;
            }
            m_vortexRows.push_back(row);
        }
    }
    layout();
}

void Wizard::layoutRows()
{
    if (!m_rows) {
        return;
    }
    const int rowHeight = si(56);
    const int content = static_cast<int>(m_vortexRows.size()) * rowHeight;
    RECT client;
    GetClientRect(m_rows, &client);
    m_rowsScroll = std::clamp(m_rowsScroll, 0, std::max(0, content - static_cast<int>(client.bottom)));
    SCROLLINFO info{ sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS, 0, content - 1, static_cast<UINT>(client.bottom), m_rowsScroll, 0 };
    SetScrollInfo(m_rows, SB_VERT, &info, TRUE);
    // the scroll bar may have come or gone, which changes the width the rows have
    GetClientRect(m_rows, &client);
    for (size_t i = 0; i < m_vortexRows.size(); i++) {
        SetWindowPos(m_vortexRows[i], nullptr, 0, static_cast<int>(i) * rowHeight - m_rowsScroll, client.right, si(54), SWP_NOZORDER);
    }
}

void Wizard::scrollRows(int to)
{
    const int before = m_rowsScroll;
    m_rowsScroll = to;
    layoutRows();
    if (m_rowsScroll != before) {
        InvalidateRect(m_rows, nullptr, TRUE);
    }
}

void Wizard::scrollRowIntoView(HWND row)
{
    RECT rowRect;
    RECT client;
    GetWindowRect(row, &rowRect);
    MapWindowPoints(nullptr, m_rows, reinterpret_cast<POINT*>(&rowRect), 2);
    GetClientRect(m_rows, &client);
    if (rowRect.top < 0) {
        scrollRows(m_rowsScroll + rowRect.top);
    } else if (rowRect.bottom > client.bottom) {
        scrollRows(m_rowsScroll + rowRect.bottom - client.bottom);
    }
}

bool Wizard::writeVortexRequest(std::wstring& error) const
{
    std::wstring dir;
    if (m_plan.where.testing()) {
        dir = m_plan.where.testRoot + L"\\BetterNMM";
    } else {
        // BetterNMM's own data folder, which an installed copy uses
        PWSTR roaming = nullptr;
        if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming) == S_OK) {
            dir = std::wstring(roaming) + L"\\BetterNMM";
        }
        CoTaskMemFree(roaming);
    }
    std::string json = "{\"profiles\":[";
    for (size_t i = 0; i < m_vortexChosen.size(); i++) {
        json += i ? ",\"" : "\"";
        for (const char c : m_vortexChosen[i]) {
            if (c == '"' || c == '\\') {
                json += '\\';
            }
            if (static_cast<unsigned char>(c) >= 0x20) {
                json += c;
            }
        }
        json += '"';
    }
    json += "]}\n";
    const auto file = dir + L"\\vortex-import.json";
    if (dir.empty() || !Files::makeDirs(dir) || !Files::writeAll(file, json)) {
        error = L"Setup could not leave the Vortex profiles you chose for BetterNMM in " + file +
                L". Bring them over in BetterNMM with File > Import from Vortex.";
        return false;
    }
    Setup::log(L"Vortex profiles to bring over: " + Files::wide(json));
    return true;
}

REAL Wizard::footerTop() const
{
    RECT client;
    GetClientRect(m_window, &client);
    return std::max<REAL>(s(394), static_cast<REAL>(client.bottom) - s(66));
}

void Wizard::layout()
{
    auto place = [&](HWND control, double x, double y, double w, double h) {
        SetWindowPos(control, nullptr, si(x), si(y), si(w), si(h), SWP_NOZORDER);
    };
    RECT client;
    GetClientRect(m_window, &client);
    const int right = std::max<int>(client.right, si(680));
    const int buttons = static_cast<int>(footerTop() + s(16));
    SetWindowPos(m_cancel, nullptr, si(24), buttons, si(96), si(34), SWP_NOZORDER);
    SetWindowPos(m_back, nullptr, right - si(32 + 132 + 10 + 104), buttons, si(104), si(34), SWP_NOZORDER);
    SetWindowPos(m_next, nullptr, right - si(32 + 132), buttons, si(132), si(34), SWP_NOZORDER);
    place(m_cardUser, 32, 120, 300, 86);
    place(m_cardMachine, 348, 120, 300, 86);
    place(m_dir, 40, 252, 500, 22);
    place(m_browse, 556, 244, 92, 38);
    place(m_desktop, 28, 106, 624, 54);
    place(m_startMenu, 28, 162, 624, 54);
    place(m_pin, 28, 218, 624, 54);
    place(m_nxm, 28, 274, 624, 72);
    // beside Finish, so however long the finished page's notes get they never run into it
    SetWindowPos(m_launch, nullptr, si(16), buttons - si(3), si(320), si(40), SWP_NOZORDER);
    if (m_rows) {
        // four rows show at once; more scroll
        place(m_rows, 28, 108, 624, 224);
        layoutRows();
    }
}

void Wizard::show(Page page)
{
    m_page = page;
    auto visible = [](HWND control, bool on) { ShowWindow(control, on ? SW_SHOW : SW_HIDE); };
    const bool update = m_existing.found && !m_uninstalling;
    visible(m_cardUser, page == Page::Location);
    visible(m_cardMachine, page == Page::Location);
    visible(m_dir, page == Page::Location);
    visible(m_browse, page == Page::Location);
    for (HWND check : { m_desktop, m_startMenu, m_pin, m_nxm }) {
        visible(check, page == Page::Options);
    }
    visible(m_launch, page == Page::Done);
    if (m_rows) {
        visible(m_rows, page == Page::Vortex);
    }
    visible(m_cancel,
            page == Page::Welcome || page == Page::Location || page == Page::Options || page == Page::Vortex || page == Page::Uninstall);
    visible(m_back, page == Page::Location || page == Page::Options || page == Page::Vortex || page == Page::Failed);
    visible(m_next, page != Page::Working && page != Page::Uninstalling);
    EnableWindow(m_cardUser, !update);
    EnableWindow(m_cardMachine, !update);
    SendMessageW(m_dir, EM_SETREADONLY, update, 0);
    EnableWindow(m_browse, !update);
    EnableWindow(m_pin, m_controls[m_startMenu].checked);

    auto& next = m_controls[m_next];
    switch (page) {
        case Page::Options:
            next.title = offersVortex() ? L"Next" : (update ? L"Update" : L"Install");
            break;
        case Page::Vortex:
            next.title = L"Install";
            break;
        case Page::Done:
            next.title = L"Finish";
            break;
        case Page::Failed:
            next.title = L"Close";
            m_controls[m_back].title = L"Try again";
            break;
        case Page::Uninstall:
            next.title = L"Uninstall";
            break;
        case Page::Removed:
            next.title = L"Close";
            break;
        default:
            next.title = L"Next";
            m_controls[m_back].title = L"Back";
            break;
    }
    SetWindowTextW(m_next, next.title.c_str());
    if (page == Page::Location) {
        refreshSpace();
    }
    InvalidateRect(m_window, nullptr, FALSE);
    for (auto& [control, data] : m_controls) {
        InvalidateRect(control, nullptr, FALSE);
    }
    if (IsWindowVisible(m_next)) {
        SetFocus(m_next);
    }
}

REAL Wizard::text(Graphics& g, const std::wstring& t, const Font& font, const Color& color, REAL x, REAL y, REAL w, REAL h) const
{
    StringFormat format;
    format.SetTrimming(StringTrimmingEllipsisWord);
    RectF box(x, y, w, h);
    RectF used;
    g.MeasureString(t.c_str(), -1, &font, box, &format, &used);
    SolidBrush brush(color);
    g.DrawString(t.c_str(), -1, &font, box, &format, &brush);
    return used.Height;
}

void Wizard::paint(HDC dc)
{
    RECT client;
    GetClientRect(m_window, &client);
    Bitmap buffer(client.right, client.bottom, PixelFormat32bppPARGB);
    Graphics g(&buffer);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.Clear(m_pal.bg);
    paintPage(g);
    Graphics screen(dc);
    screen.DrawImage(&buffer, 0, 0);
}

void Wizard::paintPage(Graphics& g)
{
    const bool update = m_existing.found && !m_uninstalling;
    const std::wstring version = m_index.version.empty() ? m_existing.version : m_index.version;
    RECT client;
    GetClientRect(m_window, &client);
    const REAL footer = footerTop();
    // the footer line
    {
        SolidBrush line(m_pal.border);
        g.FillRectangle(&line, 0.0f, footer, static_cast<REAL>(client.right), std::max<REAL>(1, s(1)));
    }
    auto hero = [&](const std::wstring& title, const std::wstring& body) {
        SolidBrush panel(m_pal.panel);
        g.FillRectangle(&panel, 0.0f, 0.0f, s(226), footer);
        if (m_logo) {
            g.DrawImage(m_logo.get(), RectF(s(113 - 56), s(96), s(112), s(112)));
        }
        StringFormat center;
        center.SetAlignment(StringAlignmentCenter);
        SolidBrush textBrush(m_pal.text);
        SolidBrush dimBrush(m_pal.dim);
        g.DrawString(L"BetterNMM", -1, m_bodyBold.get(), RectF(0, s(224), s(226), s(24)), &center, &textBrush);
        g.DrawString((L"Version " + version).c_str(), -1, m_small.get(), RectF(0, s(246), s(226), s(20)), &center, &dimBrush);
        REAL y = s(48);
        y += text(g, title, *m_title, m_pal.text, s(258), y, s(390)) + s(14);
        text(g, body, *m_body, m_pal.dim, s(258), y, s(390), footer - y - s(16));
    };
    auto header = [&](const std::wstring& title, const std::wstring& subtitle) {
        text(g, title, *m_title, m_pal.text, s(32), s(28), s(616));
        text(g, subtitle, *m_small, m_pal.dim, s(32), s(64), s(616), s(44));
    };
    auto banner = [&](const std::wstring& message, REAL y) {
        if (message.empty()) {
            return;
        }
        StringFormat format;
        RectF box(s(46), y + s(10), s(588), s(80));
        RectF used;
        g.MeasureString(message.c_str(), -1, m_small.get(), box, &format, &used);
        fillRound(g, m_pal.dangerBg, s(32), y, s(616), used.Height + s(20), s(8));
        SolidBrush brush(m_pal.danger);
        g.FillRectangle(&brush, s(32), y + s(6), s(3), used.Height + s(8));
        text(g, message, *m_small, m_pal.text, s(46), y + s(10), s(588), used.Height + s(2));
    };

    switch (m_page) {
        case Page::Welcome:
            if (update) {
                const bool same = m_existing.version == m_index.version;
                hero(same ? L"Reinstall BetterNMM" : L"Update BetterNMM",
                     L"BetterNMM " + m_existing.version + L" is installed in " + m_existing.dir + L".\n\nSetup " +
                         (same ? L"puts the same version back" : L"updates it to " + m_index.version) +
                         L". Your instances, mods, saves and settings stay as they are, and so do the shortcuts you chose.");
            } else {
                hero(L"Install BetterNMM",
                     L"BetterNMM manages Nexus Mods mods for your games, with a separate game folder, saves and settings for every "
                     L"instance.\n\nSetup puts it on this PC, makes the shortcuts you choose and adds it to Windows' list of apps, "
                     L"where you can remove it again.");
            }
            banner(m_error, s(318));
            break;
        case Page::Location: {
            header(L"Where should BetterNMM go?",
                   L"Only the program goes there. Your instances, mods and settings live in %APPDATA%\\BetterNMM, so updating or "
                   L"moving the program never touches them.");
            if (update) {
                text(g, L"Updates go where BetterNMM is installed. To move it, uninstall it first; your data stays.", *m_small, m_pal.dim,
                     s(32), s(214), s(616));
            } else {
                text(g, L"Folder", *m_bodyBold, m_pal.text, s(32), s(218), s(300));
            }
            // the folder field's frame
            const bool focused = GetFocus() == m_dir;
            fillRound(g, m_pal.card, s(32), s(244), s(516), s(38), s(6));
            strokeRound(g, focused ? m_pal.accent : m_pal.border, focused ? s(2) : s(1), s(32), s(244), s(516), s(38), s(6));
            text(g, m_space, *m_small, m_spaceShort ? m_pal.danger : m_pal.dim, s(32), s(292), s(616));
            banner(m_error, s(322));
            break;
        }
        case Page::Options:
            header(L"Shortcuts and options", L"You can change these later: shortcuts by hand, the link handler in BetterNMM's Settings.");
            banner(m_error, s(346));
            break;
        case Page::Vortex:
            header(L"Bring your mods over from Vortex",
                   L"Each profile you tick becomes a BetterNMM instance with its mods, load order, settings and copies of its saves, "
                   L"the first time BetterNMM starts. Vortex and your saves stay exactly as they are.");
            banner(m_error, s(340));
            break;
        case Page::Working:
        case Page::Uninstalling: {
            const bool removing = m_page == Page::Uninstalling;
            header(removing ? L"Removing BetterNMM..." : (update ? L"Updating BetterNMM..." : L"Installing BetterNMM..."),
                   removing ? L"Your instances, mods and settings stay."
                            : L"This takes a moment. Your instances, mods and settings are not touched.");
            fillRound(g, m_pal.card, s(32), s(196), s(616), s(10), s(5));
            if (m_progress > 0) {
                fillRound(g, m_pal.accent, s(32), s(196), std::max<REAL>(s(10), s(616) * static_cast<REAL>(m_progress)), s(10), s(5));
            }
            text(g, m_status, *m_small, m_pal.dim, s(32), s(218), s(520));
            StringFormat right;
            right.SetAlignment(StringAlignmentFar);
            SolidBrush dimBrush(m_pal.dim);
            const auto percent = std::to_wstring(static_cast<int>(m_progress * 100 + 0.5)) + L"%";
            g.DrawString(percent.c_str(), -1, m_small.get(), RectF(s(548), s(218), s(100), s(20)), &right, &dimBrush);
            break;
        }
        case Page::Done:
            hero(update ? L"BetterNMM is updated" : L"BetterNMM is ready",
                 L"It is installed in " + m_plan.dir + L"." + (m_notes.empty() ? std::wstring() : L"\n\n" + m_notes));
            break;
        case Page::Failed:
            hero(m_uninstalling ? L"BetterNMM could not be removed" : L"Setup could not finish",
                 m_error + L"\n\nNothing of your data was touched. Details are in %TEMP%\\BetterNMM-Setup.log.");
            break;
        case Page::Uninstall:
            hero(L"Uninstall BetterNMM",
                 m_existing.dir.empty()
                     ? std::wstring(L"Setup could not find where BetterNMM is installed.")
                     : L"This removes BetterNMM " + m_existing.version + L" from " + m_existing.dir +
                           L", with its shortcuts.\n\nYour instances, mods, saves and settings stay in\n%APPDATA%\\BetterNMM\nDelete that "
                           L"folder yourself if you no longer want them.");
            banner(m_error, s(318));
            break;
        case Page::Removed:
            hero(L"BetterNMM is removed",
                 L"Your instances, mods, saves and settings are still in\n%APPDATA%\\BetterNMM\nInstalling BetterNMM again picks them up.");
            break;
    }
}

void Wizard::drawControl(const DRAWITEMSTRUCT& item)
{
    auto found = m_controls.find(item.hwndItem);
    if (found == m_controls.end()) {
        return;
    }
    const Control& c = found->second;
    const REAL w = static_cast<REAL>(item.rcItem.right - item.rcItem.left);
    const REAL h = static_cast<REAL>(item.rcItem.bottom - item.rcItem.top);
    Bitmap buffer(static_cast<INT>(w), static_cast<INT>(h), PixelFormat32bppPARGB);
    Graphics g(&buffer);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    const bool on = IsWindowEnabled(item.hwndItem);
    const bool pressed = item.itemState & ODS_SELECTED;
    const bool focused = (item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT);
    g.Clear(m_pal.bg);
    StringFormat center;
    center.SetAlignment(StringAlignmentCenter);
    center.SetLineAlignment(StringAlignmentCenter);
    const Color dimmed = m_pal.dim;

    switch (c.kind) {
        case Kind::Primary: {
            const Color fill = !on ? m_pal.card : (pressed ? m_pal.accent : (c.hover ? m_pal.accentHover : m_pal.accent));
            fillRound(g, fill, s(1), s(1), w - s(2), h - s(2), s(6));
            if (focused) {
                strokeRound(g, m_pal.text, s(1.5f), 0, 0, w, h, s(7));
            }
            SolidBrush brush(on ? m_pal.accentText : dimmed);
            g.DrawString(c.title.c_str(), -1, m_button.get(), RectF(0, 0, w, h), &center, &brush);
            break;
        }
        case Kind::Secondary:
        case Kind::Ghost: {
            if (c.kind == Kind::Secondary || c.hover || pressed) {
                fillRound(g, pressed ? m_pal.border : (c.hover ? m_pal.hover : m_pal.card), s(1), s(1), w - s(2), h - s(2), s(6));
            }
            if (c.kind == Kind::Secondary) {
                strokeRound(g, m_pal.border, s(1), s(1), s(1), w - s(2), h - s(2), s(6));
            }
            if (focused) {
                strokeRound(g, m_pal.accent, s(1.5f), 0, 0, w, h, s(7));
            }
            SolidBrush brush(on ? m_pal.text : dimmed);
            g.DrawString(c.title.c_str(), -1, m_button.get(), RectF(0, 0, w, h), &center, &brush);
            break;
        }
        case Kind::Check: {
            if (c.hover && on) {
                fillRound(g, m_pal.hover, 0, 0, w, h, s(8));
            }
            const REAL box = s(18);
            const REAL bx = s(10);
            const REAL by = c.detail.empty() ? (h - box) / 2 : s(9);
            if (c.checked) {
                fillRound(g, on ? m_pal.accent : m_pal.border, bx, by, box, box, s(4));
                Pen tick(m_pal.accentText, s(2.2f));
                tick.SetLineJoin(LineJoinRound);
                tick.SetStartCap(LineCapRound);
                tick.SetEndCap(LineCapRound);
                const PointF points[] = { PointF(bx + s(4), by + s(9.5f)), PointF(bx + s(7.5f), by + s(13)),
                                          PointF(bx + s(14), by + s(5.5f)) };
                g.DrawLines(&tick, points, 3);
            } else {
                fillRound(g, m_pal.card, bx, by, box, box, s(4));
                strokeRound(g, c.hover && on ? m_pal.accent : m_pal.borderStrong, s(1), bx, by, box, box, s(4));
            }
            if (focused) {
                strokeRound(g, m_pal.accent, s(1.5f), bx - s(3), by - s(3), box + s(6), box + s(6), s(6));
            }
            const REAL tx = bx + box + s(12);
            if (c.detail.empty()) {
                StringFormat middle;
                middle.SetLineAlignment(StringAlignmentCenter);
                SolidBrush brush(on ? m_pal.text : dimmed);
                g.DrawString(c.title.c_str(), -1, m_bodyBold.get(), RectF(tx, 0, w - tx, h), &middle, &brush);
            } else {
                text(g, c.title, *m_bodyBold, on ? m_pal.text : dimmed, tx, s(6), w - tx - s(8));
                text(g, c.detail, *m_small, dimmed, tx, s(27), w - tx - s(8), h - s(27));
            }
            break;
        }
        case Kind::Card: {
            fillRound(g, c.hover && on && !c.checked ? m_pal.hover : m_pal.card, s(1), s(1), w - s(2), h - s(2), s(10));
            strokeRound(g, c.checked ? m_pal.accent : m_pal.border, c.checked ? s(2) : s(1), 0, 0, w, h, s(10));
            const REAL r = s(9);
            const REAL cx = s(18);
            const REAL cy = s(22);
            Pen ring(c.checked ? m_pal.accent : m_pal.borderStrong, s(1.5f));
            g.DrawEllipse(&ring, cx, cy - r, r * 2, r * 2);
            if (c.checked) {
                SolidBrush dot(m_pal.accent);
                g.FillEllipse(&dot, cx + s(4.5f), cy - r + s(4.5f), r * 2 - s(9), r * 2 - s(9));
            }
            if (focused) {
                strokeRound(g, m_pal.text, s(1), s(3), s(3), w - s(6), h - s(6), s(8));
            }
            text(g, c.title, *m_bodyBold, on ? m_pal.text : dimmed, s(46), s(12), w - s(58));
            text(g, c.detail, *m_small, dimmed, s(46), s(36), w - s(58), h - s(40));
            break;
        }
    }
    Graphics target(item.hDC);
    target.DrawImage(&buffer, static_cast<INT>(item.rcItem.left), static_cast<INT>(item.rcItem.top));
}

void Wizard::refreshSpace()
{
    const auto dir = folder();
    const auto free = Files::freeSpace(dir);
    const unsigned long long needed = m_index.totalSize + (64ull << 20);
    m_spaceShort = free > 0 && free < needed;
    const std::wstring drive = dir.size() >= 2 && dir[1] == L':' ? L"Drive " + dir.substr(0, 2) : std::wstring(L"That drive");
    m_space = L"Takes " + Setup::formatSize(m_index.totalSize) + L"." +
              (free > 0 ? L" " + drive + L" has " + Setup::formatSize(free) + L" free." : std::wstring());
    RECT area{ 0, si(286), si(680), si(316) };
    InvalidateRect(m_window, &area, FALSE);
}

std::wstring Wizard::folder() const
{
    const int length = GetWindowTextLengthW(m_dir);
    std::wstring out(length + 1, L'\0');
    GetWindowTextW(m_dir, out.data(), length + 1);
    out.resize(length);
    while (!out.empty() && (out.back() == L' ' || out.back() == L'\\' || out.back() == L'/')) {
        out.pop_back();
    }
    while (!out.empty() && out.front() == L' ') {
        out.erase(out.begin());
    }
    return out;
}

void Wizard::setScope(Setup::Scope scope)
{
    m_plan.scope = scope;
    m_controls[m_cardUser].checked = scope == Setup::Scope::User;
    m_controls[m_cardMachine].checked = scope == Setup::Scope::Machine;
    if (!m_dirEdited) {
        SetWindowTextW(m_dir, Setup::defaultDir(scope, m_plan.where).c_str());
    }
    InvalidateRect(m_cardUser, nullptr, FALSE);
    InvalidateRect(m_cardMachine, nullptr, FALSE);
    refreshSpace();
}

void Wizard::browse()
{
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                reinterpret_cast<void**>(&dialog)))) {
        return;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(L"Choose where to install BetterNMM");
    if (SUCCEEDED(dialog->Show(m_window))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
            wchar_t* path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                std::wstring chosen = path;
                while (chosen.size() > 3 && chosen.back() == L'\\') {
                    chosen.pop_back();
                }
                // a folder that holds other things gets BetterNMM's own folder inside it
                const auto name = Files::lower(chosen.substr(chosen.find_last_of(L'\\') + 1));
                if (name != L"betternmm" && !Files::exists(chosen + L"\\" + Setup::k_fileList)) {
                    chosen += chosen.back() == L'\\' ? L"BetterNMM" : L"\\BetterNMM";
                }
                SetWindowTextW(m_dir, chosen.c_str());
                m_dirEdited = true;
                refreshSpace();
                CoTaskMemFree(path);
            }
            item->Release();
        }
    }
    dialog->Release();
}

bool Wizard::checkRunning()
{
    const auto running = Setup::runningFrom(m_plan.dir, m_args.parent);
    if (running.empty()) {
        return true;
    }
    std::wstring names;
    for (const auto& name : running) {
        names += (names.empty() ? L"" : L", ") + name;
    }
    m_error = L"BetterNMM is running from this folder (" + names + L"). Close it, then try again.";
    InvalidateRect(m_window, nullptr, FALSE);
    return false;
}

void Wizard::primary()
{
    if (m_working) {
        return;
    }
    switch (m_page) {
        case Page::Welcome:
            m_error.clear();
            show(Page::Location);
            break;
        case Page::Location: {
            const auto dir = folder();
            m_error.clear();
            if (dir.size() < 4 || dir[1] != L':' || dir[2] != L'\\' || PathIsRelativeW(dir.c_str())) {
                m_error = L"Choose a full folder path, like C:\\Programs\\BetterNMM.";
            } else if (Files::exists(dir) && !Files::isDirectory(dir)) {
                m_error = dir + L" is a file, not a folder.";
            } else if (m_spaceShort) {
                m_error = L"There is not enough free space on that drive.";
            }
            if (!m_error.empty()) {
                InvalidateRect(m_window, nullptr, FALSE);
                return;
            }
            m_plan.dir = dir;
            show(Page::Options);
            break;
        }
        case Page::Options:
            m_plan.options.desktop = m_controls[m_desktop].checked;
            m_plan.options.startMenu = m_controls[m_startMenu].checked;
            m_plan.options.pin = m_controls[m_pin].checked && m_plan.options.startMenu;
            m_plan.options.nxm = m_controls[m_nxm].checked;
            m_error.clear();
            if (offersVortex()) {
                show(Page::Vortex);
                break;
            }
            if (checkRunning()) {
                startWork();
            }
            break;
        case Page::Vortex:
            m_vortexChosen.clear();
            for (size_t i = 0; i < m_vortexRows.size(); i++) {
                if (m_controls[m_vortexRows[i]].checked) {
                    m_vortexChosen.push_back(m_vortex[i].id);
                }
            }
            m_error.clear();
            if (checkRunning()) {
                startWork();
            }
            break;
        case Page::Uninstall:
            m_error.clear();
            if (!m_existing.dir.empty() && checkRunning()) {
                startWork();
            }
            break;
        case Page::Done:
            m_plan.options.launch = m_controls[m_launch].checked;
            if (m_plan.options.launch) {
                Setup::launch(m_plan);
            }
            PostQuitMessage(0);
            break;
        case Page::Failed:
            PostQuitMessage(1);
            break;
        case Page::Removed:
            PostQuitMessage(0);
            break;
        default:
            break;
    }
}

void Wizard::back()
{
    if (m_working) {
        return;
    }
    m_error.clear();
    if (m_page == Page::Location) {
        show(Page::Welcome);
    } else if (m_page == Page::Options) {
        show(Page::Location);
    } else if (m_page == Page::Vortex) {
        show(Page::Options);
    } else if (m_page == Page::Failed) {
        show(m_uninstalling ? Page::Uninstall : (offersVortex() ? Page::Vortex : Page::Options));
    }
}

void Wizard::cancel()
{
    if (m_working) {
        return;  // a half-placed install is never left behind: setup finishes or rolls back first
    }
    if (m_page == Page::Done || m_page == Page::Removed) {
        PostQuitMessage(0);
        return;
    }
    if (m_page == Page::Failed) {
        PostQuitMessage(1);
        return;
    }
    PostQuitMessage(2);
}

void Wizard::startWork()
{
    m_working = true;
    m_progress = 0;
    m_status = L"Starting...";
    show(m_uninstalling ? Page::Uninstalling : Page::Working);
    HWND window = m_window;
    const bool elevate = m_plan.scope == Setup::Scope::Machine && !Setup::isElevated() && !m_plan.where.testing();
    if (elevate) {
        std::wstring params = std::wstring(L"--worker ") + (m_uninstalling ? L"uninstall" : L"install") + L" --dir " + quote(m_plan.dir) +
                              L" --scope machine --options " + quote(m_plan.options.toString()) + L" --hwnd " +
                              std::to_wstring(reinterpret_cast<INT_PTR>(window));
        HANDLE process = nullptr;
        if (runElevated(params, &process) != 0 || !process) {
            m_working = false;
            m_error =
                L"Windows did not give setup administrator rights, which installing for everyone needs. Go back and choose \"Just for me\" "
                L"to install without them.";
            show(Page::Failed);
            return;
        }
        std::thread([process, window] {
            WaitForSingleObject(process, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(process, &code);
            CloseHandle(process);
            PostMessageW(window, WM_SETUP_DONE, code == 0 ? 1 : 0, 0);
        }).detach();
        return;
    }
    const Setup::Plan plan = m_plan;
    const Setup::Existing existing = m_existing;
    const bool uninstalling = m_uninstalling;
    std::thread([plan, existing, uninstalling, window] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        std::wstring error;
        DWORD lastPermille = 0;
        auto progress = [&](double fraction, const std::wstring& status) {
            const auto permille = static_cast<DWORD>(fraction * 1000);
            if (permille != lastPermille || permille == 0) {
                lastPermille = permille;
                PostMessageW(window, WM_SETUP_PROGRESS, permille, 0);
            }
            copyTo(window, COPY_STATUS, status);
        };
        const bool ok =
            uninstalling ? Setup::uninstallFiles(existing, plan.where, progress, error) : Setup::installFiles(plan, progress, error);
        if (!ok) {
            Setup::log(L"failed: " + error);
            copyTo(window, COPY_ERROR, error);
        }
        CoUninitialize();
        PostMessageW(window, WM_SETUP_DONE, ok ? 1 : 0, 0);
    }).detach();
}

void Wizard::workDone(bool ok)
{
    m_working = false;
    if (!ok) {
        if (m_error.empty()) {
            m_error = L"Setup stopped before it was done.";
        }
        show(Page::Failed);
        return;
    }
    if (m_uninstalling) {
        Setup::uninstallForUser(m_existing, m_plan.where);
        show(Page::Removed);
        return;
    }
    m_notes.clear();
    std::wstring error;
    if (!Setup::installForUser(m_plan, error)) {
        m_notes += error + L"\n";
    } else if (m_plan.options.nxm) {
        m_notes += L"Nexus Mods download links open in BetterNMM.\n";
    }
    if (m_plan.options.startMenu) {
        m_notes += L"Find it in the Start menu";
        m_notes += m_plan.options.desktop ? L" and on your desktop.\n" : L".\n";
    } else if (m_plan.options.desktop) {
        m_notes += L"Find it on your desktop.\n";
    }
    if (m_plan.options.pin) {
        m_status = L"Asking Windows to pin BetterNMM to the taskbar...";
        m_progress = 1;
        InvalidateRect(m_window, nullptr, FALSE);
        UpdateWindow(m_window);
        std::wstring outcome;
        Setup::requestPin(m_plan, outcome);
        m_notes += outcome + L"\n";
    }
    if (!m_vortexChosen.empty()) {
        std::wstring vortexError;
        m_notes += writeVortexRequest(vortexError)
                       ? (m_vortexChosen.size() == 1 ? L"BetterNMM brings over the Vortex profile you chose when it starts.\n"
                                                     : L"BetterNMM brings over the Vortex profiles you chose when it starts.\n")
                       : vortexError + L"\n";
    }
    while (!m_notes.empty() && m_notes.back() == L'\n') {
        m_notes.pop_back();
    }
    show(Page::Done);
}

LRESULT CALLBACK Wizard::controlProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data)
{
    auto* self = reinterpret_cast<Wizard*>(data);
    auto it = self->m_controls.find(window);
    if (it != self->m_controls.end()) {
        if (message == WM_MOUSEMOVE && !it->second.hover) {
            it->second.hover = true;
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, window, 0 };
            TrackMouseEvent(&track);
            InvalidateRect(window, nullptr, FALSE);
        } else if (message == WM_MOUSELEAVE) {
            it->second.hover = false;
            InvalidateRect(window, nullptr, FALSE);
        } else if (message == WM_ERASEBKGND) {
            return 1;
        } else if (message == WM_SETFOCUS && self->m_rows && GetParent(window) == self->m_rows) {
            self->scrollRowIntoView(window);
        }
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

LRESULT CALLBACK Wizard::rowsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* self = reinterpret_cast<Wizard*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!self) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    switch (message) {
        case WM_DRAWITEM:
        case WM_COMMAND:
            return SendMessageW(self->m_window, message, wParam, lParam);
        case WM_ERASEBKGND: {
            RECT client;
            GetClientRect(window, &client);
            HBRUSH brush = CreateSolidBrush(ref(self->m_pal.bg));
            FillRect(reinterpret_cast<HDC>(wParam), &client, brush);
            DeleteObject(brush);
            return 1;
        }
        case WM_MOUSEWHEEL:
            self->scrollRows(self->m_rowsScroll - GET_WHEEL_DELTA_WPARAM(wParam) * self->si(56) / WHEEL_DELTA);
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO info{ sizeof(info), SIF_ALL, 0, 0, 0, 0, 0 };
            GetScrollInfo(window, SB_VERT, &info);
            int to = info.nPos;
            switch (LOWORD(wParam)) {
                case SB_LINEUP:
                    to -= self->si(56);
                    break;
                case SB_LINEDOWN:
                    to += self->si(56);
                    break;
                case SB_PAGEUP:
                    to -= static_cast<int>(info.nPage);
                    break;
                case SB_PAGEDOWN:
                    to += static_cast<int>(info.nPage);
                    break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION:
                    to = info.nTrackPos;
                    break;
                case SB_TOP:
                    to = 0;
                    break;
                case SB_BOTTOM:
                    to = info.nMax;
                    break;
                default:
                    break;
            }
            self->scrollRows(to);
            return 0;
        }
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

LRESULT CALLBACK Wizard::proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    Wizard* self = nullptr;
    if (message == WM_NCCREATE) {
        self = static_cast<Wizard*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_window = window;
    } else {
        self = reinterpret_cast<Wizard*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    }
    return self ? self->handle(message, wParam, lParam) : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Wizard::handle(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(m_window, &ps);
            paint(dc);
            EndPaint(m_window, &ps);
            return 0;
        }
        case WM_DRAWITEM:
            drawControl(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, ref(m_pal.text));
            SetBkColor(dc, ref(m_pal.card));
            return reinterpret_cast<LRESULT>(m_editBrush);
        }
        case WM_COMMAND: {
            const HWND from = reinterpret_cast<HWND>(lParam);
            const WORD code = HIWORD(wParam);
            if (LOWORD(wParam) == IDOK && !from) {
                primary();
            } else if (LOWORD(wParam) == IDCANCEL && !from) {
                cancel();
            } else if (from == m_dir) {
                if (code == EN_CHANGE && GetFocus() == m_dir) {
                    m_dirEdited = true;
                    refreshSpace();
                } else if (code == EN_SETFOCUS || code == EN_KILLFOCUS) {
                    RECT area{ si(28), si(240), si(552), si(286) };
                    InvalidateRect(m_window, &area, FALSE);
                }
            } else if (code == BN_CLICKED) {
                if (from == m_next) {
                    primary();
                } else if (from == m_back) {
                    back();
                } else if (from == m_cancel) {
                    cancel();
                } else if (from == m_browse) {
                    browse();
                } else if (from == m_cardUser) {
                    setScope(Setup::Scope::User);
                } else if (from == m_cardMachine) {
                    setScope(Setup::Scope::Machine);
                } else if (auto it = m_controls.find(from); it != m_controls.end() && it->second.kind == Kind::Check) {
                    it->second.checked = !it->second.checked;
                    InvalidateRect(from, nullptr, FALSE);
                    if (from == m_startMenu) {
                        // Windows pins what the Start menu entry starts
                        EnableWindow(m_pin, it->second.checked);
                        InvalidateRect(m_pin, nullptr, FALSE);
                    }
                }
            }
            return 0;
        }
        case WM_COPYDATA: {
            const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
            const std::wstring textValue(static_cast<const wchar_t*>(data->lpData), data->cbData / sizeof(wchar_t) - 1);
            if (data->dwData == COPY_STATUS) {
                m_status = textValue;
            } else if (data->dwData == COPY_ERROR) {
                m_error = textValue;
            }
            InvalidateRect(m_window, nullptr, FALSE);
            return TRUE;
        }
        case WM_SETUP_PROGRESS:
            m_progress = std::min(1.0, wParam / 1000.0);
            InvalidateRect(m_window, nullptr, FALSE);
            return 0;
        case WM_SETUP_DONE:
            workDone(wParam == 1);
            return 0;
        case WM_SIZE:
            if (m_next) {
                layout();
                InvalidateRect(m_window, nullptr, FALSE);
            }
            return 0;
        case WM_DPICHANGED: {
            m_scale = HIWORD(wParam) / 96.0f;
            const auto* rect = reinterpret_cast<const RECT*>(lParam);
            SetWindowPos(m_window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            makeFonts();
            layout();
            InvalidateRect(m_window, nullptr, FALSE);
            return 0;
        }
        case WM_CLOSE:
            cancel();
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            break;
    }
    return DefWindowProcW(m_window, message, wParam, lParam);
}

// ---------------------------------------------------------------------------------------------------------------------
// without a window: silent runs and the elevated worker

int runWorker(const Args& args)
{
    Setup::Plan plan;
    plan.dir = args.dir;
    plan.scope = args.scope == L"machine" ? Setup::Scope::Machine : Setup::Scope::User;
    plan.options = Setup::Options::fromString(args.options, Setup::Options{});
    plan.where.testRoot = args.testRoot;
    plan.setupExe = selfPath();
    DWORD lastPermille = 1001;
    auto progress = [&](double fraction, const std::wstring& status) {
        const auto permille = static_cast<DWORD>(fraction * 1000);
        if (permille != lastPermille) {
            lastPermille = permille;
            if (args.report) {
                PostMessageW(args.report, WM_SETUP_PROGRESS, permille, 0);
            }
        }
        copyTo(args.report, COPY_STATUS, status);
    };
    std::wstring error;
    bool ok = false;
    if (args.worker == L"uninstall") {
        Setup::Existing existing = Setup::findExisting(plan.where);
        existing.dir = plan.dir;
        existing.scope = plan.scope;
        ok = Setup::uninstallFiles(existing, plan.where, progress, error);
    } else {
        ok = Setup::installFiles(plan, progress, error);
    }
    if (!ok) {
        Setup::log(L"failed: " + error);
        copyTo(args.report, COPY_ERROR, error);
    }
    return ok ? 0 : 1;
}

int runSilent(const Args& args, bool uninstall)
{
    Setup::Plan plan;
    plan.where.testRoot = args.testRoot;
    plan.setupExe = selfPath();
    const auto existing = Setup::findExisting(plan.where);
    if (uninstall) {
        Setup::Existing target = existing;
        if (!args.dir.empty()) {
            target.dir = args.dir;
        }
        if (target.dir.empty()) {
            Setup::log(L"silent uninstall: BetterNMM is not installed");
            return 1;
        }
        if (!Setup::runningFrom(target.dir, args.parent).empty()) {
            Setup::log(L"silent uninstall: BetterNMM is running");
            return 3;
        }
        if (target.scope == Setup::Scope::Machine && !Setup::isElevated() && !plan.where.testing()) {
            return runElevated(L"--worker uninstall --dir " + quote(target.dir) + L" --scope machine");
        }
        std::wstring error;
        const bool ok = Setup::uninstallFiles(target, plan.where, [](double, const std::wstring&) {}, error);
        if (ok) {
            Setup::uninstallForUser(target, plan.where);
        } else {
            Setup::log(L"silent uninstall failed: " + error);
        }
        return ok ? 0 : 1;
    }
    if (existing.found && ((!args.dir.empty() && Files::lower(args.dir) != Files::lower(existing.dir)) ||
                           (!args.scope.empty() && (args.scope == L"machine") != (existing.scope == Setup::Scope::Machine)))) {
        // an update stays where BetterNMM is; a second copy would leave the first one unknown to Windows
        Setup::log(L"silent install: BetterNMM is installed in " + existing.dir + L"; uninstall it first to install it elsewhere");
        return 4;
    }
    plan.options = applyFlags(existing.found ? existing.options : Setup::Options{}, args);
    // nothing to click in a silent run: Windows' pin question would wait for nobody
    plan.options.pin = false;
    plan.scope = existing.found ? existing.scope : (args.scope == L"machine" ? Setup::Scope::Machine : Setup::Scope::User);
    plan.dir =
        existing.found && args.dir.empty() ? existing.dir : (args.dir.empty() ? Setup::defaultDir(plan.scope, plan.where) : args.dir);
    if (!Setup::runningFrom(plan.dir).empty()) {
        Setup::log(L"silent install: BetterNMM is running from " + plan.dir);
        return 3;
    }
    std::wstring error;
    bool ok = false;
    if (plan.scope == Setup::Scope::Machine && !Setup::isElevated() && !plan.where.testing()) {
        ok = runElevated(L"--worker install --dir " + quote(plan.dir) + L" --scope machine --options " + quote(plan.options.toString())) ==
             0;
    } else {
        ok = Setup::installFiles(plan, [](double, const std::wstring&) {}, error);
        if (!ok) {
            Setup::log(L"silent install failed: " + error);
        }
    }
    if (ok && !Setup::installForUser(plan, error)) {
        Setup::log(error);
    }
    if (ok && plan.options.launch && args.flags.count(L"launch")) {
        Setup::launch(plan);
    }
    return ok ? 0 : 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SetCurrentProcessExplicitAppUserModelID(L"io.github.betternmm.Setup");
    const Args args = parseArgs();

    if (!args.worker.empty()) {
        return runWorker(args);
    }

    const auto self = selfPath();
    const auto selfName = Files::lower(self.substr(self.find_last_of(L'\\') + 1));
    const bool uninstall = args.uninstall || selfName == Files::lower(Setup::k_uninstaller);
    if (uninstall && !args.fromTemp) {
        // the uninstaller can not delete itself: a copy in the temporary folder does the work and is removed after
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        const auto copy = std::wstring(temp) + L"BetterNMM-uninstall-" + std::to_wstring(GetTickCount()) + L".exe";
        const auto dir = Files::parent(self);
        if (!CopyFileW(self.c_str(), copy.c_str(), FALSE)) {
            MessageBoxW(nullptr, (L"Could not start the uninstaller: " + Files::systemMessage(GetLastError())).c_str(),
                        L"Uninstall BetterNMM", MB_ICONERROR);
            return 1;
        }
        std::wstring params = L"--uninstall --from-temp --parent " + std::to_wstring(GetCurrentProcessId()) + L" --dir " +
                              quote(selfName == Files::lower(Setup::k_uninstaller) ? dir : args.dir);
        if (args.silent) {
            params += L" --silent";
        }
        if (!args.testRoot.empty()) {
            params += L" --test-root " + quote(args.testRoot);
        }
        STARTUPINFOW startup{ sizeof(startup) };
        PROCESS_INFORMATION process{};
        std::wstring commandLine = quote(copy) + L" " + params;
        if (!CreateProcessW(copy.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
            DeleteFileW(copy.c_str());
            return 1;
        }
        // a silent uninstall reports its result to whoever started it
        DWORD code = 0;
        if (args.silent) {
            WaitForSingleObject(process.hProcess, INFINITE);
            GetExitCodeProcess(process.hProcess, &code);
        }
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
        return static_cast<int>(code);
    }

    ULONG_PTR gdiplus = 0;
    GdiplusStartupInput input;
    GdiplusStartup(&gdiplus, &input, nullptr);
    INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&controls);

    int code = 0;
    if (args.silent) {
        code = runSilent(args, uninstall);
    } else {
        Wizard wizard(args, uninstall);
        code = wizard.run();
    }
    GdiplusShutdown(gdiplus);

    if (uninstall && args.fromTemp) {
        // this copy goes once it has exited
        Setup::deleteLater(self);
    }
    CoUninitialize();
    return code;
}
