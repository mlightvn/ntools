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
