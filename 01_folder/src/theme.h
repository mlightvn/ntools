// Themes: colours, fonts and line icons for the custom-drawn UI (Void, Flux, Mono, Light).
#pragma once
#include "fs.h"

enum IconStyle { ICON_SYSTEM, ICON_LINE, ICON_COLOR };

struct Theme {
    const wchar_t* id;
    const wchar_t* name;
    bool dark;
    COLORREF app, toolbar, sidebar, pane, border;
    COLORREF text, dim, faint;
    COLORREF accent, accentText;
    COLORREF sel, selBorder, hot, rowLine;
    COLORREF input, chip;
    COLORREF folder, file;      // line icon colours
    IconStyle icons;
    bool monoUI;                // Mono: monospace everywhere
    int rowH;                   // list row height at 96 dpi (0 = native)
    int glass = 0;              // > 0: surfaces are this opaque (0-255) over the blurred wallpaper / picture
};

extern const Theme* g_theme;
extern HFONT g_fontMono, g_fontSmall, g_fontTitle;

int ThemeCount();
const Theme& ThemeAt(int i);
int ThemeIndex();
void ThemeInit();                      // reads the INI, enables dark menus
void ThemeSet(int i);                  // switch + repaint everything
void ThemeFonts();                     // (re)create g_font & co. for the theme and DPI
void ThemeWindow(HWND h);              // dark/light scrollbars & tooltips for a control
void ThemeTitleBar(HWND h);            // DWM caption colours
void ThemeMenu(POINT screen);          // the theme picker menu

HBRUSH ThemeBrush(COLORREF c);         // cached solid brush
void Fill(HDC dc, const RECT& r, COLORREF c);
void Frame(HDC dc, const RECT& r, COLORREF c);
void RoundFill(HDC dc, const RECT& r, int radius, COLORREF fill, COLORREF border);
void Glyph(HDC dc, const wchar_t* g, const RECT& r, COLORREF c, HFONT f = nullptr);
void DrawItemIcon(HDC dc, const RECT& r, const Item& it);   // line / coloured icon by file type

// Backgrounds (backdrop.cpp): a picture behind the UI, the Glass theme, window opacity
void Surface(HDC dc, HWND hwnd, const RECT& r, COLORREF c);  // fill r (client coords of hwnd); the picture shows through
bool BackdropActive();
bool BackdropFollowsScreen();          // Glass: the backdrop is aligned to the monitor, repaint after moving
void BackdropReset();                  // theme / picture changed
wstring BackdropPicture();
void BackdropChoosePicture();          // file dialog
void BackdropClearPicture();
int WindowOpacity();                   // percent
void SetWindowOpacity(int pct);
void ApplyWindowOpacity();
