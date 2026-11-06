// Global toolbar (Void style): New folder, Cut / Copy / Paste / Rename / Delete, Terminal,
// pane layout (1/2/3), theme picker and the profile selector. Fully custom-drawn.
#include "app.h"
#include "pane.h"
#include "theme.h"

namespace {

enum { T_NEWFOLDER = 1, T_CUT, T_COPY, T_PASTE, T_RENAME, T_DELETE, T_TERMINAL, T_PANE1, T_PANE2, T_PANE3, T_THEME, T_PROFILE };
struct TItem { int id; const wchar_t* glyph; wstring label; const wchar_t* tip; RECT r; };

HWND g_tb, g_tip, g_edit;
vector<TItem> g_items;
vector<int> g_seps;
RECT g_searchR{};          // the search box frame (the EDIT sits inside it)
int g_hot = -1, g_down = -1;
bool g_track = false;
enum { TIMER_SEARCH = 1 };

void RunSearch() {
    KillTimer(g_tb, TIMER_SEARCH);
    wchar_t buf[1024];
    GetWindowTextW(g_edit, buf, ARRAYSIZE(buf));
    ActivePane()->Search(buf);
}

// Enter: search now; Esc: clear and go back to the folder; Down: into the results
LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_KEYDOWN:
        if (w == VK_RETURN) { RunSearch(); return 0; }
        if (w == VK_ESCAPE) {
            SetWindowTextW(h, L"");
            KillTimer(g_tb, TIMER_SEARCH);
            ActivePane()->Search(L"");
            ActivePane()->FocusList();
            return 0;
        }
        if (w == VK_DOWN) { KillTimer(g_tb, TIMER_SEARCH); if (GetWindowTextLengthW(h)) RunSearch(); ActivePane()->FocusList(); return 0; }
        break;
    case WM_CHAR:
        if (w == VK_RETURN || w == VK_ESCAPE) return 0;  // no beep
        break;
    case WM_SETFOCUS: case WM_KILLFOCUS:
        InvalidateRect(g_tb, &g_searchR, FALSE);
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

int TextW(HDC dc, const wstring& s) { SIZE sz; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz); return sz.cx; }

void Layout() {
    RECT rc; GetClientRect(g_tb, &rc);
    int bh = S(34), y = (rc.bottom - bh) / 2, x = S(14);
    HDC dc = GetDC(g_tb);
    HGDIOBJ of = SelectObject(dc, g_fontBold);
    for (auto& it : g_items) {  // drop old tooltips
        TTTOOLINFOW ti{sizeof(ti), 0, g_tb, (UINT_PTR)it.id};
        SendMessageW(g_tip, TTM_DELTOOLW, 0, (LPARAM)&ti);
    }
    g_items.clear();
    g_seps.clear();
    auto add = [&](int id, const wchar_t* g, const wstring& label, const wchar_t* tip, int w) {
        g_items.push_back({id, g, label, tip, {x, y, x + w, y + bh}});
        x += w + S(4);
    };
    auto sep = [&] { x += S(6); g_seps.push_back(x); x += S(10); };
    add(T_NEWFOLDER, L"", L"New folder", L"New folder (Ctrl+Shift+N)", S(44) + TextW(dc, L"New folder"));
    SelectObject(dc, g_font);
    sep();
    add(T_CUT, L"", L"", L"Cut (Ctrl+X)", bh);
    add(T_COPY, L"", L"", L"Copy (Ctrl+C)", bh);
    add(T_PASTE, L"", L"", L"Paste (Ctrl+V)", bh);
    add(T_RENAME, L"", L"", L"Rename (F2)", bh);
    add(T_DELETE, L"", L"", L"Delete (Del) — Shift+click deletes permanently", bh);
    sep();
    add(T_TERMINAL, L"", L"Terminal", L"Open a terminal in the current folder", S(40) + TextW(dc, L"Terminal"));
    // right side, laid out from the right edge
    int rx = rc.right - S(14);
    auto addR = [&](int id, const wchar_t* g, const wstring& label, const wchar_t* tip, int w) {
        rx -= w;
        g_items.push_back({id, g, label, tip, {rx, y, rx + w, y + bh}});
        rx -= S(4);
    };
    wstring prof = ProfileLabel();
    SelectObject(dc, g_fontBold);
    addR(T_PROFILE, L"", prof, L"Profile (Ctrl+P) — Ctrl+S saves the current panes and tabs", S(58) + TextW(dc, prof));
    addR(T_THEME, L"", L"", L"Theme", bh);
    rx -= S(6); g_seps.push_back(rx); rx -= S(10);
    addR(T_PANE3, nullptr, L"", L"3 panes (Ctrl+3)", bh);
    addR(T_PANE2, nullptr, L"", L"2 panes (Ctrl+2)", bh);
    addR(T_PANE1, nullptr, L"", L"1 pane (Ctrl+1)", bh);
    // search box: centred in the window when there is room, else in the gap between the two button groups
    int gapL = x + S(12), gapR = rx - S(12);
    int sw = std::min(S(460), gapR - gapL);
    if (sw >= S(140)) {
        int sx = std::max(gapL, std::min((int)rc.right / 2 - sw / 2, gapR - sw));
        g_searchR = {sx, y, sx + sw, y + bh};
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        int eh = tm.tmHeight + S(2);
        MoveWindow(g_edit, sx + S(34), y + (bh - eh) / 2, sw - S(34) - S(64), eh, TRUE);
        ShowWindow(g_edit, SW_SHOW);
    } else {
        g_searchR = {};
        ShowWindow(g_edit, SW_HIDE);
    }
    SelectObject(dc, of);
    ReleaseDC(g_tb, dc);
    for (auto& it : g_items) {  // tooltips follow the item rectangles
        TTTOOLINFOW ti{sizeof(ti), TTF_SUBCLASS, g_tb, (UINT_PTR)it.id};
        ti.rect = it.r;
        ti.lpszText = (LPWSTR)it.tip;
        SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
}

void DrawPanesIcon(HDC dc, const RECT& r, int n, COLORREF c) {
    int w = S(18), h = S(14);
    RECT box{(r.left + r.right - w) / 2, (r.top + r.bottom - h) / 2, 0, 0};
    box.right = box.left + w;
    box.bottom = box.top + h;
    Frame(dc, box, c);
    for (int k = 1; k < n; k++) {
        RECT d{box.left + w * k / n, box.top, box.left + w * k / n + 1, box.bottom};
        Fill(dc, d, c);
    }
}

COLORREF Darker(COLORREF c) { return RGB(GetRValue(c) * 4 / 5, GetGValue(c) * 4 / 5, GetBValue(c) * 4 / 5); }

void Paint(HDC dc) {
    const Theme& t = *g_theme;
    RECT rc; GetClientRect(g_tb, &rc);
    Fill(dc, rc, t.toolbar);
    RECT ln = rc; ln.top = ln.bottom - 1;
    Fill(dc, ln, t.border);
    for (int x : g_seps) { RECT s{x, rc.top + S(16), x + 1, rc.bottom - S(16)}; Fill(dc, s, t.border); }
    SetBkMode(dc, TRANSPARENT);
    if (g_searchR.right > g_searchR.left) {
        bool focus = GetFocus() == g_edit;
        RoundFill(dc, g_searchR, S(8), t.input, focus ? t.accent : t.border);
        RECT g = g_searchR; g.left += S(8); g.right = g.left + S(22);
        Glyph(dc, L"", g, focus ? t.accent : t.faint);
        SelectObject(dc, g_fontMono);
        const wchar_t* key = L"Ctrl F";
        SIZE ks; GetTextExtentPoint32W(dc, key, lstrlenW(key), &ks);
        int cy = (g_searchR.top + g_searchR.bottom) / 2, kh = S(18);
        RECT k{g_searchR.right - S(10) - ks.cx - S(12), cy - kh / 2, g_searchR.right - S(10), cy + kh / 2};
        RoundFill(dc, k, S(5), t.chip, t.border);
        SetTextColor(dc, t.faint);
        DrawTextW(dc, key, -1, &k, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
    }
    for (int i = 0; i < (int)g_items.size(); i++) {
        const TItem& it = g_items[i];
        bool hot = i == g_hot, down = i == g_down && hot;
        RECT r = it.r;
        if (it.id == T_NEWFOLDER) {
            COLORREF bg = down ? Darker(t.accent) : t.accent;
            RoundFill(dc, r, S(6), bg, bg);
            RECT g = r; g.left += S(10); g.right = g.left + S(20);
            Glyph(dc, it.glyph, g, t.accentText);
            RECT tx = r; tx.left = g.right + S(6);
            SelectObject(dc, g_fontBold);
            SetTextColor(dc, t.accentText);
            DrawTextW(dc, it.label.c_str(), -1, &tx, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            continue;
        }
        bool on = (it.id == T_PANE1 && g_paneCount == 1) || (it.id == T_PANE2 && g_paneCount == 2) || (it.id == T_PANE3 && g_paneCount == 3);
        if (hot || on || it.id == T_PROFILE) RoundFill(dc, r, S(6), hot ? t.hot : t.chip, on ? t.border : (hot ? t.hot : t.chip));
        COLORREF c = hot || on ? t.text : t.dim;
        if (it.id >= T_PANE1 && it.id <= T_PANE3) { DrawPanesIcon(dc, r, it.id - T_PANE1 + 1, on ? t.accent : c); continue; }
        if (it.label.empty()) { Glyph(dc, it.glyph, r, c); continue; }
        RECT g = r; g.left += S(8); g.right = g.left + S(20);
        Glyph(dc, it.glyph, g, it.id == T_PROFILE ? t.accent : c);
        RECT tx = r; tx.left = g.right + S(6); tx.right -= it.id == T_PROFILE ? S(20) : S(6);
        SelectObject(dc, it.id == T_PROFILE ? g_fontBold : g_font);
        SetTextColor(dc, it.id == T_PROFILE ? t.text : c);
        DrawTextW(dc, it.label.c_str(), -1, &tx, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (it.id == T_PROFILE) {
            RECT ch = r; ch.left = ch.right - S(22);
            Glyph(dc, L"", ch, t.dim, g_iconFontSmall);
        }
    }
}

void Command(const TItem& it) {
    Pane* p = ActivePane();
    switch (it.id) {
    case T_NEWFOLDER: p->Command(PC_NEWFOLDER); break;
    case T_CUT: p->Command(PC_CUT); break;
    case T_COPY: p->Command(PC_COPY); break;
    case T_PASTE: p->Command(PC_PASTE); break;
    case T_RENAME: p->Command(PC_RENAME); break;
    case T_DELETE: p->Command(PC_DELETE); break;
    case T_TERMINAL: p->Command(PC_TERMINAL); break;
    case T_PANE1: case T_PANE2: case T_PANE3: SetPaneCount(it.id - T_PANE1 + 1); break;
    case T_THEME: {
        POINT pt{it.r.left, it.r.bottom};
        ClientToScreen(g_tb, &pt);
        ThemeMenu(pt);
        break;
    }
    case T_PROFILE: {
        RECT r = it.r;
        MapWindowPoints(g_tb, nullptr, (POINT*)&r, 2);
        ProfileMenu(&r);
        break;
    }
    }
    if (g_tb) InvalidateRect(g_tb, nullptr, FALSE);
}

int Hit(POINT pt) {
    for (int i = 0; i < (int)g_items.size(); i++) if (PtInRect(&g_items[i].r, pt)) return i;
    return -1;
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    switch (m) {
    case WM_SIZE: Layout(); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, g_theme->text);
        SetBkColor((HDC)w, g_theme->input);
        return (LRESULT)ThemeBrush(g_theme->input);
    case WM_COMMAND:
        if ((HWND)l == g_edit && HIWORD(w) == EN_CHANGE && GetFocus() == g_edit) SetTimer(h, TIMER_SEARCH, 450, nullptr);  // search as you type
        return 0;
    case WM_TIMER:
        if (w == TIMER_SEARCH) RunSearch();
        return 0;
    case WM_LBUTTONDOWN:
        if (PtInRect(&g_searchR, pt)) { SetFocus(g_edit); return 0; }
        g_down = Hit(pt); SetCapture(h); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);
        Paint(mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!g_track) { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); g_track = true; }
        int hot = Hit(pt);
        if (hot != g_hot) { g_hot = hot; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE: g_track = false; g_hot = -1; InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_LBUTTONUP: {
        int i = Hit(pt), d = g_down;
        g_down = -1;
        ReleaseCapture();
        InvalidateRect(h, nullptr, FALSE);
        if (i >= 0 && i == d) { TItem it = g_items[i]; Command(it); }
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

}  // namespace

void ToolbarCreate(HWND parent) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"NxToolbar";
    RegisterClassW(&wc);
    g_tb = CreateWindowExW(0, L"NxToolbar", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, nullptr, g_inst, nullptr);
    g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, g_tb, nullptr, g_inst, nullptr);
    ThemeWindow(g_tip);
    g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, g_tb, nullptr, g_inst, nullptr);
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font, FALSE);
    SendMessageW(g_edit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search this folder and its subfolders");
    SetWindowSubclass(g_edit, EditProc, 1, 0);
}

HWND ToolbarHwnd() { return g_tb; }
int ToolbarHeight() { return S(56); }

void ToolbarRefresh() {
    if (!g_tb) return;
    ThemeWindow(g_tip);
    ThemeWindow(g_edit);
    Layout();
    InvalidateRect(g_tb, nullptr, FALSE);
    InvalidateRect(g_edit, nullptr, TRUE);
}

void ToolbarFocusSearch() {
    if (!g_edit || !IsWindowVisible(g_edit)) return;
    SetFocus(g_edit);
    SendMessageW(g_edit, EM_SETSEL, 0, -1);
}

void ToolbarSyncSearch() {
    if (!g_edit || GetFocus() == g_edit) return;  // never rewrite the text while the user is typing
    Pane* p = ActivePane();
    if (!p) return;
    wstring q = p->InSearch() ? SearchQuery(p->Path()) : L"";
    wchar_t cur[1024];
    GetWindowTextW(g_edit, cur, ARRAYSIZE(cur));
    if (q != cur) SetWindowTextW(g_edit, q.c_str());
    wstring where = p->InSearch() ? SearchRoot(p->Path()) : p->Path();
    wstring cue = IsBin(where) ? L"The Recycle Bin can't be searched" : L"Search " + (where.empty() ? wstring(L"all drives") : DisplayName(where));
    SendMessageW(g_edit, EM_SETCUEBANNER, TRUE, (LPARAM)cue.c_str());
}
