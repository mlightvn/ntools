#include "theme.h"
#include "app.h"
#include "favs.h"
#include <dwmapi.h>
#include <map>

HFONT g_fontMono, g_fontSmall, g_fontTitle;

namespace {

//                id       name     dark  app                   toolbar               sidebar               pane                  border
const Theme kThemes[] = {
    {L"void", L"Void", true, RGB(15, 26, 50), RGB(19, 33, 61), RGB(15, 27, 52), RGB(17, 30, 57), RGB(33, 52, 88),
     /*text*/ RGB(214, 222, 235), RGB(138, 155, 184), RGB(88, 106, 138),
     /*accent*/ RGB(74, 144, 226), RGB(255, 255, 255),
     /*sel*/ RGB(30, 56, 98), RGB(74, 144, 226), RGB(23, 41, 74), RGB(27, 43, 74),
     /*input*/ RGB(12, 22, 43), RGB(28, 45, 78),
     /*icons*/ RGB(150, 168, 196), RGB(130, 148, 178), ICON_LINE, false, 31},
    {L"flux", L"Flux", true, RGB(12, 11, 22), RGB(16, 15, 30), RGB(14, 13, 26), RGB(18, 17, 33), RGB(38, 36, 62),
     RGB(228, 226, 242), RGB(150, 146, 182), RGB(100, 96, 132),
     RGB(139, 92, 246), RGB(255, 255, 255),
     RGB(37, 32, 70), RGB(110, 90, 190), RGB(27, 25, 48), RGB(18, 17, 33),
     RGB(21, 20, 38), RGB(34, 32, 58),
     RGB(167, 139, 250), RGB(150, 146, 182), ICON_COLOR, false, 34},
    {L"mono", L"Mono", true, RGB(10, 12, 14), RGB(13, 15, 18), RGB(12, 14, 16), RGB(11, 13, 15), RGB(32, 36, 40),
     RGB(201, 209, 217), RGB(125, 133, 144), RGB(85, 92, 100),
     RGB(74, 222, 128), RGB(10, 12, 14),
     RGB(19, 46, 32), RGB(74, 222, 128), RGB(19, 23, 27), RGB(11, 13, 15),
     RGB(16, 19, 22), RGB(26, 30, 34),
     RGB(110, 200, 140), RGB(125, 133, 144), ICON_LINE, true, 24},
    // Nexus: futuristic HUD - near-black, neon accent (cyan / magenta / lime / amber), hexagon tiles, corner brackets
    {L"nexus", L"Nexus", true, RGB(2, 6, 10), RGB(4, 13, 19), RGB(4, 13, 19), RGB(4, 13, 19), RGB(14, 54, 64),
     RGB(214, 246, 251), RGB(127, 163, 173), RGB(72, 110, 120),
     RGB(25, 240, 255), RGB(2, 6, 10),
     RGB(7, 43, 50), RGB(25, 240, 255), RGB(8, 32, 42), RGB(8, 38, 48),
     RGB(5, 18, 24), RGB(6, 26, 34),
     RGB(25, 240, 255), RGB(127, 163, 173), ICON_HEX, false, 40, 0, STYLE_HUD},
    // Glass: frosted panels over the blurred desktop wallpaper (or the chosen picture)
    {L"glass", L"Glass", true, RGB(14, 16, 22), RGB(34, 38, 48), RGB(26, 29, 38), RGB(22, 25, 33), RGB(92, 100, 118),
     RGB(244, 246, 250), RGB(200, 206, 218), RGB(150, 158, 174),
     RGB(10, 132, 255), RGB(255, 255, 255),
     RGB(28, 92, 170), RGB(64, 160, 255), RGB(70, 78, 96), RGB(22, 25, 33),
     RGB(30, 34, 44), RGB(58, 64, 80),
     RGB(130, 196, 255), RGB(206, 212, 224), ICON_LINE, false, 32, 150},
    {L"light", L"Light", false, RGB(255, 255, 255), RGB(246, 247, 249), RGB(250, 250, 251), RGB(255, 255, 255), RGB(222, 224, 228),
     RGB(28, 28, 30), RGB(96, 100, 110), RGB(150, 154, 162),
     RGB(0, 103, 192), RGB(255, 255, 255),
     RGB(204, 228, 247), RGB(0, 103, 192), RGB(229, 241, 251), RGB(240, 240, 242),
     RGB(255, 255, 255), RGB(236, 237, 240),
     RGB(214, 160, 40), RGB(110, 114, 124), ICON_SYSTEM, false, 28},
};
int g_index = 0;

// undocumented uxtheme exports (Windows 10 1809+) used by Explorer / Notepad for dark menus and scrollbars
enum AppMode { AM_DEFAULT, AM_ALLOW_DARK, AM_FORCE_DARK, AM_FORCE_LIGHT };
using SetPreferredAppModeFn = int(WINAPI*)(int);
using AllowDarkModeForWindowFn = bool(WINAPI*)(HWND, bool);
using FlushMenuThemesFn = void(WINAPI*)();
SetPreferredAppModeFn pSetPreferredAppMode;
AllowDarkModeForWindowFn pAllowDarkModeForWindow;
FlushMenuThemesFn pFlushMenuThemes;

void LoadUxtheme() {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    pSetPreferredAppMode = (SetPreferredAppModeFn)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    pAllowDarkModeForWindow = (AllowDarkModeForWindowFn)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(133));
    pFlushMenuThemes = (FlushMenuThemesFn)(void*)GetProcAddress(ux, MAKEINTRESOURCEA(136));
}

void ApplyAppMode() {
    if (pSetPreferredAppMode) pSetPreferredAppMode(g_theme->dark ? AM_FORCE_DARK : AM_FORCE_LIGHT);
    if (pFlushMenuThemes) pFlushMenuThemes();
}

int CALLBACK FontExists(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM l) { *(bool*)l = true; return 0; }

bool HasFont(const wchar_t* face) {
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy(lf.lfFaceName, face);
    bool found = false;
    HDC dc = GetDC(nullptr);
    EnumFontFamiliesExW(dc, &lf, FontExists, (LPARAM)&found, 0);
    ReleaseDC(nullptr, dc);
    return found;
}

HFONT MakeFont(const wchar_t* face, int px, int weight) {
    LOGFONTW lf{};
    lf.lfHeight = -px;
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy(lf.lfFaceName, face);
    return CreateFontIndirectW(&lf);
}

COLORREF Blend(COLORREF a, COLORREF b, int pctA) {
    return RGB((GetRValue(a) * pctA + GetRValue(b) * (100 - pctA)) / 100, (GetGValue(a) * pctA + GetGValue(b) * (100 - pctA)) / 100,
               (GetBValue(a) * pctA + GetBValue(b) * (100 - pctA)) / 100);
}

bool ExtIn(const wchar_t* ext, std::initializer_list<const wchar_t*> list) {
    for (auto e : list) if (!lstrcmpiW(ext, e)) return true;
    return false;
}

}  // namespace

namespace {
Theme g_live = kThemes[0];   // the active theme (a copy, so Nexus can swap its accent)
const COLORREF kNexusAccents[] = {RGB(25, 240, 255), RGB(255, 43, 214), RGB(182, 255, 59), RGB(255, 176, 32)};
const wchar_t* kNexusAccentNames[] = {L"Cyan", L"Magenta", L"Lime", L"Amber"};
int g_nexusAccent = 0;

void Activate(int i) {
    g_index = i;
    g_live = kThemes[i];
    if (g_live.style == STYLE_HUD) {
        COLORREF a = kNexusAccents[g_nexusAccent];
        g_live.accent = g_live.selBorder = g_live.folder = a;
        g_live.sel = Blend(a, g_live.pane, 14);
        g_live.hot = Blend(a, g_live.pane, 7);
    }
}
}  // namespace

const Theme* g_theme = &g_live;

int ThemeCount() { return (int)(sizeof(kThemes) / sizeof(kThemes[0])); }
const Theme& ThemeAt(int i) { return kThemes[i]; }
int ThemeIndex() { return g_index; }

void ThemeInit() {
    LoadUxtheme();
    wstring id = IniGet(L"Window", L"theme", L"void");
    g_nexusAccent = std::max(0, std::min(3, _wtoi(IniGet(L"Window", L"nexusAccent", L"0").c_str())));
    int idx = 0;
    for (int i = 0; i < ThemeCount(); i++) if (IEq(id, kThemes[i].id)) idx = i;
    Activate(idx);
    ApplyAppMode();
}

COLORREF Mix(COLORREF a, COLORREF b, int pctA) { return Blend(a, b, pctA); }
bool Hud() { return g_theme->style == STYLE_HUD; }

void HudCorners(HDC dc, const RECT& r, int which) {
    if (!Hud()) return;
    int len = S(16), th = S(2);
    COLORREF a = g_theme->accent;
    if (which & 1) {
        Fill(dc, {r.left, r.top, r.left + len, r.top + th}, a);
        Fill(dc, {r.left, r.top, r.left + th, r.top + len}, a);
        Fill(dc, {r.right - len, r.top, r.right, r.top + th}, a);
        Fill(dc, {r.right - th, r.top, r.right, r.top + len}, a);
    }
    if (which & 2) {
        Fill(dc, {r.left, r.bottom - th, r.left + len, r.bottom}, a);
        Fill(dc, {r.left, r.bottom - len, r.left + th, r.bottom}, a);
        Fill(dc, {r.right - len, r.bottom - th, r.right, r.bottom}, a);
        Fill(dc, {r.right - th, r.bottom - len, r.right, r.bottom}, a);
    }
}

void HudGrid(HDC dc, HWND hwnd, const RECT& r) {
    if (!Hud()) return;
    int step = S(40);
    POINT o{0, 0};
    if (hwnd != g_main) MapWindowPoints(hwnd, g_main, &o, 1);  // one grid across all windows
    COLORREF c = Blend(g_theme->accent, g_theme->app, 6);
    for (int x = r.left + ((step - (o.x + r.left) % step) % step); x < r.right; x += step) Fill(dc, {x, r.top, x + 1, r.bottom}, c);
    for (int y = r.top + ((step - (o.y + r.top) % step) % step); y < r.bottom; y += step) Fill(dc, {r.left, y, r.right, y + 1}, c);
}

wstring HudLabel(const wstring& s) { return Hud() ? L"// " + s : s; }

void ThemeFonts() {
    for (HFONT f : {g_font, g_fontBold, g_iconFont, g_iconFontSmall, g_fontMono, g_fontSmall, g_fontTitle}) if (f) DeleteObject(f);
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, g_dpi);
    const wchar_t* mono = HasFont(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
    int px = -ncm.lfMessageFont.lfHeight;
    if (g_theme->style == STYLE_HUD) {  // the design's fonts when installed, else Bahnschrift (ships with Windows)
        const wchar_t* ui = HasFont(L"Rajdhani") ? L"Rajdhani" : L"Bahnschrift";
        const wchar_t* head = HasFont(L"Orbitron") ? L"Orbitron" : ui;
        int up = HasFont(L"Rajdhani") ? S(3) : S(1);
        g_font = MakeFont(ui, px + up, FW_NORMAL);
        g_fontBold = MakeFont(ui, px + up, FW_SEMIBOLD);
        g_fontTitle = MakeFont(head, px + S(4), FW_BOLD);
        g_fontSmall = MakeFont(head, px - S(2), FW_SEMIBOLD);
        if (HasFont(L"Share Tech Mono")) mono = L"Share Tech Mono";
    } else if (g_theme->monoUI) {
        g_font = MakeFont(mono, px, FW_NORMAL);
        g_fontBold = MakeFont(mono, px, FW_SEMIBOLD);
        g_fontTitle = MakeFont(mono, px + S(2), FW_SEMIBOLD);
        g_fontSmall = MakeFont(mono, px - S(1), FW_NORMAL);
    } else {
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
        ncm.lfMessageFont.lfWeight = FW_SEMIBOLD;
        g_fontBold = CreateFontIndirectW(&ncm.lfMessageFont);
        g_fontTitle = MakeFont(ncm.lfMessageFont.lfFaceName, px + S(2), FW_SEMIBOLD);
        g_fontSmall = MakeFont(ncm.lfMessageFont.lfFaceName, px - S(1), FW_NORMAL);
    }
    g_fontMono = MakeFont(mono, px - S(1), FW_NORMAL);
    const wchar_t* icons = HasFont(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
    g_iconFont = MakeFont(icons, S(14), FW_NORMAL);
    g_iconFontSmall = MakeFont(icons, S(9), FW_NORMAL);
}

void ThemeWindow(HWND h) {
    if (pAllowDarkModeForWindow) pAllowDarkModeForWindow(h, g_theme->dark);
    SetWindowTheme(h, g_theme->dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

void ThemeTitleBar(HWND h) {
    BOOL dark = g_theme->dark;
    DwmSetWindowAttribute(h, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
    COLORREF cap = g_theme->toolbar, txt = g_theme->text;
    DwmSetWindowAttribute(h, 35 /*DWMWA_CAPTION_COLOR (Win11)*/, &cap, sizeof(cap));
    DwmSetWindowAttribute(h, 36 /*DWMWA_TEXT_COLOR*/, &txt, sizeof(txt));
}

void ThemeSet(int i) {
    if (i < 0 || i >= ThemeCount()) return;
    Activate(i);
    IniSet(L"Window", L"theme", g_theme->id);
    ApplyAppMode();
    BackdropReset();
    ThemeChanged();  // app.h: fonts, controls, layout, repaint
}

void ThemeMenu(POINT pt) {
    HMENU m = CreatePopupMenu();
    for (int i = 0; i < ThemeCount(); i++)
        AppendMenuW(m, MF_STRING | (i == g_index ? MF_CHECKED : 0), 100 + i, (wstring(L"Theme: ") + kThemes[i].name).c_str());
    if (Hud()) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        for (int i = 0; i < 4; i++)
            AppendMenuW(m, MF_STRING | (i == g_nexusAccent ? MF_CHECKED : 0), 70 + i, (wstring(L"Accent: ") + kNexusAccentNames[i]).c_str());
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    wstring pic = BackdropPicture();
    AppendMenuW(m, MF_STRING | (pic.empty() ? 0 : MF_CHECKED), 90, L"Background picture…");
    AppendMenuW(m, MF_STRING | (pic.empty() ? MF_GRAYED : 0), 91, L"Remove background picture");
    HMENU op = CreatePopupMenu();
    static const int pcts[] = {100, 95, 90, 85, 80, 70};
    for (int i = 0; i < 6; i++)
        AppendMenuW(op, MF_STRING | (WindowOpacity() == pcts[i] ? MF_CHECKED : 0), 80 + i,
                    (std::to_wstring(pcts[i]) + (pcts[i] == 100 ? L"%  (opaque)" : L"%")).c_str());
    AppendMenuW(m, MF_POPUP, (UINT_PTR)op, L"Window opacity");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 99, L"Manage favourites…");
    int r = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN, pt.x, pt.y, 0, g_main, nullptr);
    DestroyMenu(m);
    if (r >= 100) ThemeSet(r - 100);
    if (r == 99) FavManager();
    if (r == 90) BackdropChoosePicture();
    if (r == 91) BackdropClearPicture();
    if (r >= 80 && r < 86) SetWindowOpacity(pcts[r - 80]);
    if (r >= 70 && r < 74) {
        g_nexusAccent = r - 70;
        IniSet(L"Window", L"nexusAccent", std::to_wstring(g_nexusAccent));
        ThemeSet(g_index);
    }
}

HBRUSH ThemeBrush(COLORREF c) {
    static std::map<COLORREF, HBRUSH> cache;
    auto it = cache.find(c);
    if (it != cache.end()) return it->second;
    return cache[c] = CreateSolidBrush(c);
}

void Fill(HDC dc, const RECT& r, COLORREF c) { FillRect(dc, &r, ThemeBrush(c)); }
void Frame(HDC dc, const RECT& r, COLORREF c) { FrameRect(dc, &r, ThemeBrush(c)); }

void RoundFill(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border) {
    HGDIOBJ ob = SelectObject(dc, ThemeBrush(fill));
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, op);
    SelectObject(dc, ob);
    DeleteObject(pen);
}

void Glyph(HDC dc, const wchar_t* g, const RECT& r, COLORREF c, HFONT f) {
    HGDIOBJ of = SelectObject(dc, f ? f : g_iconFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    RECT rr = r;
    DrawTextW(dc, g, -1, &rr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, of);
}

void DrawItemIcon(HDC dc, const RECT& r, const Item& it) {
    const Theme& t = *g_theme;
    if (t.icons == ICON_HEX) {  // Nexus: tinted hexagon with the extension (or a folder / drive glyph)
        static const COLORREF pal[] = {RGB(25, 240, 255), RGB(255, 43, 214), RGB(182, 255, 59), RGB(255, 176, 32), RGB(154, 107, 255)};
        const wchar_t* e = PathFindExtensionW(it.name.c_str());
        bool dir = it.isDir();
        COLORREF c = RGB(127, 163, 173);
        if (it.dtype) c = t.accent;
        else if (dir) { unsigned hsh = 0; for (wchar_t ch : it.name) hsh = hsh * 31 + ch; c = pal[hsh % 5]; }
        else if (ExtIn(e, {L".php"})) c = RGB(154, 107, 255);
        else if (ExtIn(e, {L".js", L".ts", L".jsx", L".tsx", L".mp3", L".wav", L".flac", L".zip", L".7z", L".rar", L".gz", L".tar"})) c = RGB(255, 176, 32);
        else if (ExtIn(e, {L".json", L".xlsx", L".xls", L".csv", L".exe", L".msi"})) c = RGB(182, 255, 59);
        else if (ExtIn(e, {L".md", L".psd", L".cpp", L".c", L".h", L".hpp", L".cs", L".py", L".go", L".rs"})) c = RGB(25, 240, 255);
        else if (ExtIn(e, {L".css", L".scss", L".png", L".jpg", L".jpeg", L".gif", L".webp", L".ico", L".svg", L".bmp"})) c = RGB(255, 43, 214);
        else if (ExtIn(e, {L".xml", L".pdf", L".html", L".htm", L".mp4", L".mkv", L".mov", L".avi"})) c = RGB(255, 106, 77);
        else if (ExtIn(e, {L".txt", L".log", L".ini", L".bat", L".cmd"})) c = RGB(191, 233, 240);
        int w = std::min<int>(S(30), r.right - r.left), h = std::min<int>(S(26), r.bottom - r.top);
        int x = (r.left + r.right - w) / 2, y = (r.top + r.bottom - h) / 2;
        POINT pts[6] = {{x + w / 4, y}, {x + w * 3 / 4, y}, {x + w, y + h / 2}, {x + w * 3 / 4, y + h}, {x + w / 4, y + h}, {x, y + h / 2}};
        HGDIOBJ ob = SelectObject(dc, ThemeBrush(Blend(c, t.pane, 22)));
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Polygon(dc, pts, 6);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        RECT tile{x, y, x + w, y + h};
        if (it.dtype) { Glyph(dc, L"", tile, c, g_iconFontSmall); return; }
        if (dir) { Glyph(dc, L"", tile, c, g_iconFontSmall); return; }
        wstring label = *e && lstrlenW(e) <= 5 ? wstring(e + 1) : L"";
        if (label.empty()) { Glyph(dc, L"", tile, c, g_iconFontSmall); return; }
        CharUpperBuffW(&label[0], (DWORD)label.size());
        HFONT f = CreateFontW(-S(label.size() > 3 ? 7 : 8), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0,
                              HasFont(L"Share Tech Mono") ? L"Share Tech Mono" : L"Consolas");
        Glyph(dc, label.c_str(), tile, c, f);
        DeleteObject(f);
        return;
    }
    if (t.icons == ICON_SYSTEM) {  // Windows icons (cached per extension, never extracted from the file)
        int sz = GetSystemMetricsForDpi(SM_CXSMICON, g_dpi);
        ImageList_DrawEx(SysImageList(), IconOf(it), dc, (r.left + r.right - sz) / 2, (r.top + r.bottom - sz) / 2, sz, sz,
                         CLR_NONE, CLR_NONE, ILD_TRANSPARENT);
        return;
    }
    if (it.dtype) {
        const wchar_t* g = it.dtype == DRIVE_REMOVABLE ? L"" : it.dtype == DRIVE_REMOTE ? L"" : it.dtype == DRIVE_CDROM ? L"" : L"";
        Glyph(dc, g, r, t.folder);
        return;
    }
    static const COLORREF palette[] = {RGB(56, 189, 248), RGB(167, 139, 250), RGB(74, 222, 128), RGB(244, 114, 182), RGB(251, 146, 60), RGB(250, 204, 21)};
    bool dir = it.attr & FILE_ATTRIBUTE_DIRECTORY;
    const wchar_t* ext = PathFindExtensionW(it.name.c_str());
    const wchar_t* g = L"";  // page
    COLORREF c = t.file;
    const wchar_t* badge = nullptr;
    wstring badgeText;
    if (dir) { g = L""; c = t.folder; }
    else if (ExtIn(ext, {L".png", L".jpg", L".jpeg", L".gif", L".webp", L".bmp", L".ico", L".heic", L".tif", L".tiff"})) { g = L""; c = RGB(244, 114, 182); }
    else if (ExtIn(ext, {L".mp4", L".mkv", L".mov", L".avi", L".wmv", L".webm", L".m4v", L".flv"})) { g = L""; c = RGB(251, 146, 60); }
    else if (ExtIn(ext, {L".mp3", L".wav", L".flac", L".m4a", L".aac", L".ogg", L".wma"})) { g = L""; c = RGB(250, 204, 21); }
    else if (ExtIn(ext, {L".zip", L".7z", L".rar", L".tar", L".gz", L".tgz", L".bz2", L".xz", L".iso"})) { g = L""; c = RGB(251, 146, 60); }
    else if (ExtIn(ext, {L".json", L".yaml", L".yml", L".toml", L".xml"})) { badge = L"{ }"; c = RGB(250, 204, 21); }
    else if (ExtIn(ext, {L".lock", L".lockb"}) || StrStrIW(it.name.c_str(), L"-lock.")) { g = L""; }
    else if (ExtIn(ext, {L".env", L".ini", L".cfg", L".conf", L".config", L".gitignore", L".editorconfig", L".npmrc"}) || it.name[0] == L'.') { g = L""; }
    else if (ExtIn(ext, {L".js", L".ts", L".jsx", L".tsx", L".c", L".cpp", L".h", L".hpp", L".cs", L".py", L".java", L".go", L".rs", L".php",
                         L".rb", L".sh", L".ps1", L".bat", L".cmd", L".html", L".css", L".scss", L".vue", L".svg", L".sql", L".kt", L".swift"})) { g = L""; c = RGB(56, 189, 248); }
    else if (ExtIn(ext, {L".exe", L".msi", L".dll", L".appx", L".msix"})) { g = L""; c = RGB(74, 222, 128); }
    else if (ExtIn(ext, {L".pdf", L".doc", L".docx", L".txt", L".md", L".rtf", L".xls", L".xlsx", L".csv", L".ppt", L".pptx", L".odt"})) { g = L""; }

    if (t.icons == ICON_COLOR) {  // Flux: tinted tiles, coloured folders, extension badges
        RECT tile = r;
        int sz = std::min(r.right - r.left, r.bottom - r.top) - S(4);
        tile.left = r.left + (r.right - r.left - sz) / 2;
        tile.top = r.top + (r.bottom - r.top - sz) / 2;
        tile.right = tile.left + sz;
        tile.bottom = tile.top + sz;
        if (dir) {
            unsigned h = 0;
            for (wchar_t ch : it.name) h = h * 31 + ch;
            c = palette[h % 6];
            RoundFill(dc, tile, S(6), Blend(c, t.pane, 22), Blend(c, t.pane, 22));
            Glyph(dc, g, tile, c);
            return;
        }
        if (!badge && *ext && lstrlenW(ext) <= 5) { badgeText = ext + 1; CharUpperW(&badgeText[0]); badge = badgeText.c_str(); }
        RoundFill(dc, tile, S(6), Blend(c, t.pane, 18), Blend(c, t.pane, 18));
        if (badge) {
            HFONT f = CreateFontW(-S(lstrlenW(badge) > 3 ? 7 : 8), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
            Glyph(dc, badge, tile, c, f);
            DeleteObject(f);
        } else {
            Glyph(dc, g, tile, c);
        }
        return;
    }
    if (badge) { Glyph(dc, badge, r, t.icons == ICON_LINE ? t.file : c, g_fontMono); return; }
    Glyph(dc, g, r, t.icons == ICON_LINE ? (dir ? t.folder : t.file) : c);
}
