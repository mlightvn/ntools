// nExplorer: main window, layout (sidebar + 1..3 panes), global keys, settings.
#include "app.h"
#include "pane.h"
#include "columns.h"
#include "fileops.h"
#include <thread>

HINSTANCE g_inst;
HWND g_main;
int g_dpi = 96;
HFONT g_font, g_fontBold, g_iconFont, g_iconFontSmall;
Pane* g_panes[3];
int g_paneCount = 2;
std::unordered_set<wstring> g_cutSet;

int g_active = 0;
static bool g_sideVisible = true;
static int g_sideW = 230;            // at 96 dpi
double g_frac[3] = {1, 1, 1};  // relative pane widths
static wstring g_ini;
static int g_dragGap = -1;           // splitter being dragged: 0 = sidebar, 1.. = between panes
static HWND g_lastFocus = nullptr;

// ---------- INI ----------
wstring IniGet(const wchar_t* sec, const wchar_t* key, const wchar_t* def) {
    vector<wchar_t> buf(32768);
    GetPrivateProfileStringW(sec, key, def, buf.data(), (DWORD)buf.size(), g_ini.c_str());
    return buf.data();
}
bool IniSet(const wchar_t* sec, const wchar_t* key, const wstring& val) { return WritePrivateProfileStringW(sec, key, val.c_str(), g_ini.c_str()) != 0; }
void IniClearSection(const wchar_t* sec) { WritePrivateProfileStringW(sec, nullptr, nullptr, g_ini.c_str()); }
static int IniInt(const wchar_t* sec, const wchar_t* key, int def) { return GetPrivateProfileIntW(sec, key, def, g_ini.c_str()); }

static void InitIni() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    PathRemoveExtensionW(exe);
    g_ini = wstring(exe) + L".ini";
    if (!Exists(g_ini)) {  // UTF-16 BOM makes the profile API write Unicode
        HANDLE h = CreateFileW(g_ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) { DWORD n; WORD bom = 0xFEFF; WriteFile(h, &bom, 2, &n, nullptr); CloseHandle(h); }
    }
}

// ---------- fonts ----------
static int CALLBACK FontExists(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM l) { *(bool*)l = true; return 0; }

static void CreateFonts() {
    if (g_font) { DeleteObject(g_font); DeleteObject(g_fontBold); DeleteObject(g_iconFont); DeleteObject(g_iconFontSmall); }
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, g_dpi);
    g_font = CreateFontIndirectW(&ncm.lfMessageFont);
    ncm.lfMessageFont.lfWeight = FW_SEMIBOLD;
    g_fontBold = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy(lf.lfFaceName, L"Segoe Fluent Icons");
    bool fluent = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, FontExists, (LPARAM)&fluent, 0);
    ReleaseDC(nullptr, dc);
    lf.lfHeight = -S(14);
    lf.lfQuality = CLEARTYPE_QUALITY;
    if (!fluent) wcscpy(lf.lfFaceName, L"Segoe MDL2 Assets");
    g_iconFont = CreateFontIndirectW(&lf);
    lf.lfHeight = -S(9);
    g_iconFontSmall = CreateFontIndirectW(&lf);
}

static BOOL CALLBACK SetFontProc(HWND h, LPARAM) {
    SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE);
    return TRUE;
}

// ---------- panes / layout ----------
Pane* ActivePane() { return g_panes[std::min(g_active, g_paneCount - 1)]; }

void SetActive(Pane* p) {
    for (int i = 0; i < 3; i++)
        if (g_panes[i] == p && g_active != i) {
            int old = g_active;
            g_active = i;
            g_panes[old]->PaintBorder();
            p->PaintBorder();
        }
}

void FocusNextPane(bool fromSidebar) {
    int n = fromSidebar ? std::min(g_active, g_paneCount - 1) : (g_active + 1) % g_paneCount;
    SetActive(g_panes[n]);
    g_panes[n]->FocusList();
}

void RebuildAllColumns() {
    for (int i = 0; i < 3; i++) {
        g_panes[i]->RebuildColumns();
        g_panes[i]->Redraw();
    }
}

void ToggleHidden() {
    g_showHidden = !g_showHidden;
    for (int i = 0; i < 3; i++) g_panes[i]->Refresh();
}

void SetCutSet(const vector<wstring>& paths) {
    g_cutSet.clear();
    for (auto& p : paths) g_cutSet.insert(Lower(p));
    for (int i = 0; i < 3; i++) g_panes[i]->Redraw();
}

void ClearCutSet() {
    if (g_cutSet.empty()) return;
    g_cutSet.clear();
    for (int i = 0; i < 3; i++) g_panes[i]->Redraw();
}

static int Gap() { return S(5); }

// x-ranges of splitter gaps, in client coordinates
static vector<RECT> GapRects() {
    vector<RECT> v;
    RECT rc; GetClientRect(g_main, &rc);
    int x = 0;
    if (g_sideVisible) { x = S(g_sideW); v.push_back({x, 0, x + Gap(), rc.bottom}); x += Gap(); }
    else v.push_back({-100, 0, -100, 0});
    int avail = rc.right - x - Gap() * (g_paneCount - 1);
    double sum = 0;
    for (int i = 0; i < g_paneCount; i++) sum += g_frac[i];
    for (int i = 0; i < g_paneCount - 1; i++) {
        x += (int)(avail * g_frac[i] / sum);
        v.push_back({x, 0, x + Gap(), rc.bottom});
        x += Gap();
    }
    return v;
}

void Relayout() {
    RECT rc; GetClientRect(g_main, &rc);
    vector<RECT> gaps = GapRects();
    HWND side = SidebarHwnd(), prof = ProfileButton();
    ShowWindow(side, g_sideVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(prof, g_sideVisible ? SW_SHOW : SW_HIDE);
    if (g_sideVisible) {
        int ph = S(34);  // profile selector on top of the sidebar
        MoveWindow(prof, S(4), S(4), S(g_sideW) - S(8), ph, TRUE);
        MoveWindow(side, 0, ph + S(8), S(g_sideW), rc.bottom - ph - S(8), TRUE);
    }
    int x = g_sideVisible ? gaps[0].right : 0;
    for (int i = 0; i < 3; i++) {
        if (i >= g_paneCount) { ShowWindow(g_panes[i]->hwnd, SW_HIDE); continue; }
        int right = i + 1 < g_paneCount ? gaps[i + 1].left : rc.right;
        MoveWindow(g_panes[i]->hwnd, x, 0, right - x, rc.bottom, TRUE);
        ShowWindow(g_panes[i]->hwnd, SW_SHOW);
        if (i + 1 < g_paneCount) x = gaps[i + 1].right;
    }
    InvalidateRect(g_main, nullptr, TRUE);
}

void SetPaneCount(int n) {
    g_paneCount = n;
    for (int i = 0; i < 3; i++) g_frac[i] = 1;
    if (g_active >= n) g_active = n - 1;
    Relayout();
    ActivePane()->FocusList();
    for (int i = 0; i < 3; i++) g_panes[i]->PaintBorder();
}

// ---------- shell helpers ----------
void OpenInShell(const wstring& path) {
    std::thread([path] {  // slow handlers must not freeze the UI
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        SHELLEXECUTEINFOW sei{sizeof(sei)};
        sei.fMask = SEE_MASK_NOASYNC;
        sei.hwnd = g_main;
        sei.lpFile = path.c_str();
        sei.lpDirectory = nullptr;
        sei.nShow = SW_SHOWNORMAL;
        wstring dir = Parent(path);
        sei.lpDirectory = dir.c_str();
        ShellExecuteExW(&sei);
        CoUninitialize();
    }).detach();
}

void OpenTerminal(const wstring& dir) {
    wstring args = L"-d \"" + dir + L"\"";
    if ((INT_PTR)ShellExecuteW(g_main, nullptr, L"wt.exe", args.c_str(), nullptr, SW_SHOWNORMAL) > 32) return;
    ShellExecuteW(g_main, nullptr, L"cmd.exe", nullptr, dir.c_str(), SW_SHOWNORMAL);
}

void ShowProperties(const wstring& path) {
    if (!path.empty()) SHObjectProperties(g_main, SHOP_FILEPATH, path.c_str(), nullptr);
}

void ReleaseDrive(const wstring& root) {
    for (int i = 0; i < 3; i++)
        if (IStarts(g_panes[i]->Path(), root.substr(0, 2))) g_panes[i]->Navigate(L"");
}

wstring SevenZipExe() {
    wchar_t dir[MAX_PATH] = L"";
    DWORD n = sizeof(dir);
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
        for (const wchar_t* val : {L"Path64", L"Path"}) {
            n = sizeof(dir);
            if (!dir[0] && RegGetValueW(root, L"SOFTWARE\\7-Zip", val, RRF_RT_REG_SZ, nullptr, dir, &n) != ERROR_SUCCESS) dir[0] = 0;
        }
    vector<wstring> dirs;
    if (dir[0]) dirs.push_back(dir);
    wchar_t pf[MAX_PATH];
    if (ExpandEnvironmentStringsW(L"%ProgramFiles%\\7-Zip", pf, MAX_PATH)) dirs.push_back(pf);
    for (auto& d : dirs) {
        if (Exists(Join(d, L"7zG.exe"))) return Join(d, L"7zG.exe");  // GUI version shows its own progress window
        if (Exists(Join(d, L"7z.exe"))) return Join(d, L"7z.exe");
    }
    return L"";
}

wstring ArchiveName(const vector<wstring>& items, const wchar_t* ext) {
    wstring dir = Parent(items[0]), base = FileName(items[0]);
    if (!IsDir(items[0])) { size_t dot = base.find_last_of(L'.'); if (dot != wstring::npos && dot > 0) base.resize(dot); }
    return Join(dir, UniqueName(dir, base + ext, false));
}

void SevenZipAdd(const vector<wstring>& items, const wstring& archive, const wstring& password, bool encryptNames) {
    wstring exe = SevenZipExe();
    if (exe.empty() || items.empty()) return;
    // names go through a UTF-8 list file (no command-line length limit, Unicode safe), relative to the folder
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wstring list = wstring(tmp) + L"nexplorer_7z_" + std::to_wstring(GetTickCount()) + L".txt";
    std::string data;
    for (auto& p : items) {
        wstring name = FileName(p);
        int len = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), (int)name.size(), nullptr, 0, nullptr, nullptr);
        std::string u(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, name.c_str(), (int)name.size(), &u[0], len, nullptr, nullptr);
        data += u + "\r\n";
    }
    HANDLE f = CreateFileW(list.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wr;
    WriteFile(f, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(f);
    wstring dir = Parent(items[0]);
    wstring cmd = L"\"" + exe + L"\" a -t7z \"" + archive + L"\" -scsUTF-8";
    if (!password.empty()) cmd += L" \"-p" + password + L"\"" + (encryptNames ? L" -mhe=on" : L"");  // AES-256
    cmd += L" @\"" + list + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    bool gui = IStarts(FileName(exe), L"7zG");
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, gui ? 0 : CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) {
        DeleteFileW(list.c_str());
        MessageBoxW(g_main, (L"Could not start 7-Zip.\n\n" + ErrText(GetLastError())).c_str(), APP_NAME, MB_ICONERROR);
        return;
    }
    CloseHandle(pi.hThread);
    HANDLE proc = pi.hProcess;
    std::thread([proc, list, dir] {  // clean up and refresh the folder when 7-Zip is done
        WaitForSingleObject(proc, INFINITE);
        CloseHandle(proc);
        DeleteFileW(list.c_str());
        PostMessageW(g_main, WM_APP_OPDONE, 0, (LPARAM)new vector<wstring>{dir});
    }).detach();
}

bool IsArchive(const wstring& path) {
    static const wchar_t* exts[] = {L".zip", L".7z", L".rar", L".tar", L".gz", L".tgz", L".bz2", L".tbz2", L".xz", L".txz"};
    const wchar_t* e = PathFindExtensionW(path.c_str());
    for (auto x : exts) if (!lstrcmpiW(e, x)) return true;
    return false;
}

// "photos.tar.gz" -> "photos"
static wstring ArchiveBase(const wstring& path) {
    wstring b = FileName(path);
    for (int i = 0; i < 2; i++) {
        size_t dot = b.find_last_of(L'.');
        if (dot == wstring::npos || dot == 0) break;
        wstring e = b.substr(dot);
        b.resize(dot);
        if (lstrcmpiW(e.c_str(), L".gz") && lstrcmpiW(e.c_str(), L".bz2") && lstrcmpiW(e.c_str(), L".xz")) break;  // only ".tar.*" has two parts
    }
    return b;
}

// 7-Zip (7zG shows progress and asks for passwords itself); tar.exe that ships with Windows otherwise.
void ExtractArchives(const vector<wstring>& archives, bool toOwnFolder) {
    wstring exe = SevenZipExe();
    std::thread([archives, toOwnFolder, exe] {
        auto dirs = new vector<wstring>();
        wstring errors;
        for (auto& a : archives) {
            wstring dir = Parent(a), target = dir;
            if (toOwnFolder) target = Join(dir, UniqueName(dir, ArchiveBase(a), false));
            wstring cmd;
            DWORD flags = 0;
            if (!exe.empty()) {
                cmd = L"\"" + exe + L"\" x \"" + a + L"\" \"-o" + target + L"\"";
                if (!IStarts(FileName(exe), L"7zG")) flags = CREATE_NO_WINDOW;
            } else {
                CreateDirectoryW(target.c_str(), nullptr);
                cmd = L"tar.exe -xf \"" + a + L"\" -C \"" + target + L"\"";
                flags = CREATE_NO_WINDOW;
            }
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, flags, nullptr, dir.c_str(), &si, &pi)) {
                errors += FileName(a) + L": " + ErrText(GetLastError()) + L"\n";
                continue;
            }
            WaitForSingleObject(pi.hProcess, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            // 7zG reports its own errors; tar.exe is silent, so tell the user
            if (code != 0 && exe.empty())
                errors += FileName(a) + L": tar.exe could not extract it (exit code " + std::to_wstring(code) + L"). Installing 7-Zip adds support for more formats and passwords.\n";
            dirs->push_back(dir);
        }
        if (!errors.empty()) PostMessageW(g_main, WM_APP_MESSAGE, 0, (LPARAM)new wstring(L"Extract failed:\n\n" + errors));
        PostMessageW(g_main, WM_APP_OPDONE, 0, (LPARAM)dirs);
    }).detach();
}

// ---------- settings ----------
static void LoadSettings() {
    g_sideVisible = IniInt(L"Window", L"side", 1) != 0;
    g_sideW = std::max(120, std::min(600, IniInt(L"Window", L"sideW", 230)));
    g_showHidden = IniInt(L"Window", L"hidden", 0) != 0;
    wstring shown = IniGet(L"Columns", L"shown");
    if (!shown.empty()) {
        for (int c = 1; c < C_FILECOLS; c++) g_colShown[c] = false;
        for (wchar_t* p = &shown[0]; *p;) { int c = wcstol(p, &p, 10); if (c > 0 && c < C_FILECOLS) g_colShown[c] = true; while (*p == L',') p++; }
    }
    for (int c = 0; c < C_ALL; c++) g_colW[c] = IniInt(L"Columns", (L"w" + std::to_wstring(c)).c_str(), g_colW[c]);
}

static void SaveSettings() {
    WINDOWPLACEMENT wp{sizeof(wp)};
    GetWindowPlacement(g_main, &wp);
    RECT r = wp.rcNormalPosition;
    IniSet(L"Window", L"x", std::to_wstring(r.left));
    IniSet(L"Window", L"y", std::to_wstring(r.top));
    IniSet(L"Window", L"w", std::to_wstring(r.right - r.left));
    IniSet(L"Window", L"h", std::to_wstring(r.bottom - r.top));
    IniSet(L"Window", L"max", wp.showCmd == SW_SHOWMAXIMIZED ? L"1" : L"0");
    IniSet(L"Window", L"side", g_sideVisible ? L"1" : L"0");
    IniSet(L"Window", L"sideW", std::to_wstring(g_sideW));
    IniSet(L"Window", L"hidden", g_showHidden ? L"1" : L"0");
    ActivePane()->RebuildColumns();  // pulls current widths into g_colW
    wstring shown;
    for (int c = 1; c < C_FILECOLS; c++) if (g_colShown[c]) shown += (shown.empty() ? L"" : L",") + std::to_wstring(c);
    IniSet(L"Columns", L"shown", shown.empty() ? L"0" : shown);
    for (int c = 0; c < C_ALL; c++) IniSet(L"Columns", (L"w" + std::to_wstring(c)).c_str(), std::to_wstring(g_colW[c]));
    // panes / tabs live in profiles now (saved only on Ctrl+S); drop keys from older versions
    IniClearSection(L"Tabs");
    WritePrivateProfileStringW(L"Window", L"panes", nullptr, g_ini.c_str());
    for (int i = 0; i < 3; i++) WritePrivateProfileStringW(L"Window", (L"frac" + std::to_wstring(i)).c_str(), nullptr, g_ini.c_str());
    SidebarSave();
}

static void OpenCmdPath(const wstring& cmdPath) {
    wstring dir, sel;
    if (cmdPath.empty() || !ResolveUserPath(cmdPath, L"", dir, sel)) return;
    ActivePane()->NewTab(dir);
    if (!sel.empty()) ActivePane()->Navigate(dir, false, sel);
}

// ---------- global keys ----------
static bool IsEditFocus() {
    wchar_t cls[32] = L"";
    GetClassNameW(GetFocus(), cls, 32);
    return !lstrcmpiW(cls, L"Edit");
}

// Shift + wheel (and tilt wheel / touchpad sideways swipe) scrolls a file list horizontally.
static bool HorizontalWheel(const MSG& msg) {
    bool shiftWheel = msg.message == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(msg.wParam) & MK_SHIFT);
    if (!shiftWheel && msg.message != WM_MOUSEHWHEEL) return false;
    for (int i = 0; i < g_paneCount; i++) {
        HWND list = g_panes[i]->list;
        if (msg.hwnd != list && !IsChild(list, msg.hwnd)) continue;  // the header is a child of the list
        int delta = GET_WHEEL_DELTA_WPARAM(msg.wParam);
        int dx = (shiftWheel ? -delta : delta) * S(40) / WHEEL_DELTA;  // wheel down = right, like browsers
        if (dx) ListView_Scroll(list, dx, 0);
        return true;
    }
    return false;
}

static bool GlobalKey(MSG& msg) {
    if (DragActive() && msg.message == WM_KEYDOWN) {
        if (msg.wParam == VK_ESCAPE) DragEnd(false);
        else DragUpdate();
        return true;
    }
    if (DragActive() && msg.message == WM_KEYUP) { DragUpdate(); return false; }
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;
    if (!IsChild(g_main, msg.hwnd) && msg.hwnd != g_main) return false;
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0, alt = GetKeyState(VK_MENU) < 0;
    Pane* p = ActivePane();
    WPARAM k = msg.wParam;
    if (ctrl && !alt) {
        switch (k) {
        case 'T': p->NewTab(shift ? L"" : p->Path()); p->FocusList(); return true;
        case 'W': p->CloseTab(p->CurTab()); p->FocusList(); return true;
        case VK_TAB: p->SwitchTab((p->CurTab() + (shift ? p->TabCount() - 1 : 1)) % p->TabCount()); p->FocusList(); return true;
        case '1': case '2': case '3': SetPaneCount(int(k - '0')); return true;
        case 'B': g_sideVisible = !g_sideVisible; Relayout(); if (!g_sideVisible && GetFocus() == SidebarHwnd()) p->FocusList(); return true;
        case 'L': p->addr.BeginEdit(); return true;
        case 'H': ToggleHidden(); return true;
        case 'S': if (!shift) { ProfileSave(); return true; } break;
        case 'P': ProfileMenu(nullptr); return true;
        }
    }
    if (alt && !ctrl && k == 'D') { p->addr.BeginEdit(); return true; }
    if (k == VK_F4 && !alt) { p->addr.BeginEdit(); return true; }
    if (IsEditFocus()) return false;
    if ((k == VK_TAB && !ctrl && !alt) || k == VK_F6) { FocusNextPane(GetFocus() == SidebarHwnd()); return true; }
    for (int i = 0; i < g_paneCount; i++)
        if (msg.hwnd == g_panes[i]->list) return g_panes[i]->HandleKey(msg);
    return false;
}

// ---------- window proc ----------
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_main = h;
        g_dpi = GetDpiForWindow(h);
        CreateFonts();
        for (int i = 0; i < 3; i++) { g_panes[i] = new Pane(); g_panes[i]->Create(h, i); }
        SidebarCreate(h);
        ProfileButtonCreate(h);
        SetTimer(h, 78, 1000, nullptr);  // refresh the "unsaved profile" marker
        DragAcceptFiles(h, TRUE);
        AddClipboardFormatListener(h);
        return 0;
    }
    case WM_SIZE: Relayout(); return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        FillRect(dc, &ps.rcPaint, GetSysColorBrush(COLOR_WINDOW));
        EndPaint(h, &ps);
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && !DragActive()) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
            for (auto& r : GapRects()) if (PtInRect(&r, pt)) { SetCursor(LoadCursor(nullptr, IDC_SIZEWE)); return TRUE; }
        }
        break;
    case WM_LBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        vector<RECT> gaps = GapRects();
        for (int i = 0; i < (int)gaps.size(); i++)
            if (PtInRect(&gaps[i], pt)) { g_dragGap = i; SetCapture(h); return 0; }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (DragActive()) { DragUpdate(); return 0; }
        if (g_dragGap >= 0) {
            RECT rc; GetClientRect(h, &rc);
            int x = GET_X_LPARAM(l);
            if (g_dragGap == 0) {
                g_sideW = std::max(120, std::min(MulDiv(x, 96, g_dpi), MulDiv(rc.right, 96, g_dpi) / 2));
            } else {
                // move the boundary between pane i-1 and i, keeping the others
                int i = g_dragGap;
                vector<RECT> gaps = GapRects();
                int left = i == 1 ? (g_sideVisible ? gaps[0].right : 0) : gaps[i - 1].right;
                int right = i + 1 < (int)gaps.size() ? gaps[i + 1].left : rc.right;
                x = std::max(left + S(150), std::min(x, right - S(150) - Gap()));
                double total = g_frac[i - 1] + g_frac[i];
                double a = double(x - left) / std::max(1, right - left - Gap());
                g_frac[i - 1] = total * a;
                g_frac[i] = total * (1 - a);
            }
            Relayout();
        }
        return 0;
    case WM_LBUTTONUP:
        if (DragActive()) { DragEnd(true); return 0; }
        if (g_dragGap >= 0) { g_dragGap = -1; ReleaseCapture(); }
        return 0;
    case WM_CAPTURECHANGED:
        if (DragActive() && (HWND)l != h) DragEnd(false);
        g_dragGap = -1;
        return 0;
    case WM_TIMER:
        if (w == 77) DragUpdate();
        if (w == 78) ProfileTick();
        return 0;
    case WM_DRAWITEM:
        if (((DRAWITEMSTRUCT*)l)->hwndItem == ProfileButton()) { ProfileDrawButton((DRAWITEMSTRUCT*)l); return TRUE; }
        break;
    case WM_COMMAND:
        if ((HWND)l == ProfileButton() && HIWORD(w) == BN_CLICKED) ProfileMenu(ProfileButton());
        return 0;
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)l;
        LRESULT res = 0;
        if (nm->hwndFrom == SidebarHwnd()) SidebarOnNotify(nm, res);
        return res;
    }
    case WM_DROPFILES: DropExternal((HDROP)w); return 0;
    case WM_APP_EJECTED: OnEjected(l); return 0;
    case WM_APP_FOCUSLIST: ActivePane()->FocusList(); return 0;
    case WM_APP_MESSAGE: {  // text from a background thread
        std::unique_ptr<wstring> msg((wstring*)l);
        MessageBoxW(h, msg->c_str(), APP_NAME, MB_ICONWARNING);
        return 0;
    }
    case WM_CLIPBOARDUPDATE:
        if (GetClipboardOwner() != h) ClearCutSet();
        return 0;
    case WM_APP_OPDONE: {
        std::unique_ptr<vector<wstring>> dirs((vector<wstring>*)l);
        for (int i = 0; i < 3; i++) {
            if (IsVirtual(g_panes[i]->Path())) { g_panes[i]->Refresh(); continue; }  // Recycle Bin view: any op may change it
            for (auto& d : *dirs)
                if (IEq(g_panes[i]->Path(), d)) { g_panes[i]->Refresh(); break; }
        }
        return 0;
    }
    case WM_DEVICECHANGE:
        if (w == DBT_DEVICEARRIVAL || w == DBT_DEVICEREMOVECOMPLETE) {
            SidebarRefreshDrives();
            for (int i = 0; i < 3; i++) g_panes[i]->OnDrivesChanged();
        }
        return TRUE;
    case WM_ACTIVATE:  // remember / restore the focused child across app switches
        if (LOWORD(w) == WA_INACTIVE) { HWND f = GetFocus(); if (f && IsChild(h, f)) g_lastFocus = f; return 0; }
        if (!IsIconic(h)) SendMessageW(h, WM_SETFOCUS, 0, 0);
        return 0;
    case WM_SETFOCUS:
        if (g_lastFocus && IsWindow(g_lastFocus) && IsWindowVisible(g_lastFocus)) SetFocus(g_lastFocus);
        else ActivePane()->FocusList();
        return 0;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        CreateFonts();
        EnumChildWindows(h, SetFontProc, 0);
        RECT* r = (RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        RebuildAllColumns();
        Relayout();
        return 0;
    }
    case WM_CLOSE:
        if (FileOpBusy() && MessageBoxW(h, L"A file operation is still running. Quit anyway?", APP_NAME, MB_YESNO | MB_ICONWARNING) != IDYES) return 0;
        SaveSettings();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        RemoveClipboardFormatListener(h);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmd, int show) {
    g_inst = inst;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    InitIni();
    LoadSettings();

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"NxMain";
    RegisterClassExW(&wc);

    int x = IniInt(L"Window", L"x", CW_USEDEFAULT), y = IniInt(L"Window", L"y", CW_USEDEFAULT);
    int w = IniInt(L"Window", L"w", 1400), hgt = IniInt(L"Window", L"h", 850);
    HWND h = CreateWindowExW(0, L"NxMain", APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, hgt, nullptr, nullptr, inst, nullptr);
    if (!h) return 1;
    {  // keep the window inside the monitor's work area (taskbar excluded)
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
        RECT r, wa = mi.rcWork;
        GetWindowRect(h, &r);
        int ww = std::min<int>(r.right - r.left, wa.right - wa.left), wh = std::min<int>(r.bottom - r.top, wa.bottom - wa.top);
        int nx = std::max<int>(wa.left, std::min<int>(r.left, wa.right - ww)), ny = std::max<int>(wa.top, std::min<int>(r.top, wa.bottom - wh));
        SetWindowPos(h, nullptr, nx, ny, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmd && *cmd ? cmd : L"x", &argc);
    wstring cmdPath = cmd && *cmd && argc > 0 ? argv[0] : L"";
    LocalFree(argv);
    ProfilesInit();
    OpenCmdPath(cmdPath);
    ShowWindow(h, IniInt(L"Window", L"max", 0) ? SW_SHOWMAXIMIZED : show);
    Relayout();
    SetActive(ActivePane());
    ActivePane()->FocusList();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (HorizontalWheel(msg) || GlobalKey(msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
