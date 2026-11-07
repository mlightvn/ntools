// Theme-coloured scrollbars. Native scrollbars only come in light / dark grey, so a thin custom bar is laid
// over each native one (a sibling window above the list / tree) and forwards the scrolling to it.
#include "theme.h"
#include "app.h"

namespace {

struct Bar {
    HWND target = nullptr, wnd = nullptr;
    int bar = SB_VERT;            // SB_VERT / SB_HORZ
    int role = 0;                 // background: 0 pane, 1 sidebar
    bool list = false;            // a ListView (scrolls in pixels / rows) rather than a tree
    RECT last{};
    SCROLLINFO si{};
    bool hot = false, drag = false, tracking = false;
    int dragOff = 0, trackPos = 0;
    int repeat = 0;               // SB_PAGEUP / SB_PAGEDOWN while the track is held
};

struct Pair { Bar v, h; };

const wchar_t* kClass = L"NxScroll";
enum { TIMER_REPEAT = 1 };

bool Vert(const Bar* b) { return b->bar == SB_VERT; }

int Length(const Bar* b) {
    RECT rc; GetClientRect(b->wnd, &rc);
    return Vert(b) ? rc.bottom : rc.right;
}

// thumb start / length along the bar
void Thumb(const Bar* b, int& start, int& len) {
    int L = Length(b);
    const SCROLLINFO& si = b->si;
    int range = std::max(1, si.nMax - si.nMin + 1);
    int page = std::max(1, (int)si.nPage);
    len = std::max(S(28), (int)((long long)L * page / range));
    len = std::min(len, L);
    int maxPos = std::max(1, range - page);
    int pos = (b->drag ? b->trackPos : si.nPos) - si.nMin;
    pos = std::max(0, std::min(pos, maxPos));
    start = (int)((long long)(L - len) * pos / maxPos);
}

void Sync(Bar* b) {
    if (!b->wnd) return;
    SCROLLBARINFO sbi{sizeof(sbi)};
    LONG obj = Vert(b) ? OBJID_VSCROLL : OBJID_HSCROLL;
    bool vis = IsWindowVisible(b->target) && GetScrollBarInfo(b->target, obj, &sbi) &&
               !(sbi.rgstate[0] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN));
    if (!vis) {
        if (IsWindowVisible(b->wnd)) ShowWindow(b->wnd, SW_HIDE);
        b->last = {};
        return;
    }
    RECT r = sbi.rcScrollBar;
    if (Vert(b)) {  // also cover the little box where both bars meet
        SCROLLBARINFO h{sizeof(h)};
        if (GetScrollBarInfo(b->target, OBJID_HSCROLL, &h) && !(h.rgstate[0] & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)))
            r.bottom = h.rcScrollBar.bottom;
    }
    MapWindowPoints(nullptr, GetParent(b->target), (POINT*)&r, 2);
    SCROLLINFO si{sizeof(si), SIF_ALL};
    GetScrollInfo(b->target, b->bar, &si);
    bool moved = !EqualRect(&r, &b->last) || !IsWindowVisible(b->wnd);
    bool changed = si.nPos != b->si.nPos || si.nMax != b->si.nMax || si.nMin != b->si.nMin || si.nPage != b->si.nPage;
    b->si = si;
    if (moved) {
        b->last = r;
        SetWindowPos(b->wnd, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    if (moved || changed) InvalidateRect(b->wnd, nullptr, FALSE);
}

void SyncPair(Pair* p) { Sync(&p->v); Sync(&p->h); }

void ScrollTo(Bar* b, int pos) {
    if (b->list) {  // ListView: vertical position = top row, horizontal = pixels
        int cur = GetScrollPos(b->target, b->bar);
        if (Vert(b)) {
            RECT ir{LVIR_BOUNDS};
            int rowH = ListView_GetItemRect(b->target, 0, &ir, LVIR_BOUNDS) ? ir.bottom - ir.top : S(24);
            ListView_Scroll(b->target, 0, (pos - cur) * rowH);
        } else {
            ListView_Scroll(b->target, pos - cur, 0);
        }
    } else {
        SendMessageW(b->target, Vert(b) ? WM_VSCROLL : WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, pos), 0);
    }
}

void Paint(Bar* b, HDC dc) {
    const Theme& t = *g_theme;
    RECT rc; GetClientRect(b->wnd, &rc);
    COLORREF bg = b->role == 1 ? t.sidebar : t.pane;
    Surface(dc, b->wnd, rc, bg);
    if (b->si.nPage && (int)b->si.nPage > b->si.nMax - b->si.nMin) return;  // nothing to scroll
    int start, len;
    Thumb(b, start, len);
    bool big = b->hot || b->drag;
    int thick = big ? S(8) : S(4);
    int across = Vert(b) ? rc.right : rc.bottom;
    int off = (across - thick) / 2;
    RECT th = Vert(b) ? RECT{off, start + S(2), off + thick, start + len - S(2)} : RECT{start + S(2), off, start + len - S(2), off + thick};
    COLORREF c = b->drag ? t.accent : big ? Mix(t.text, bg, 55) : Mix(t.dim, bg, 60);
    if (Hud()) {  // Nexus: square, accent-tinted
        if (!b->drag) c = big ? Mix(t.accent, bg, 70) : Mix(t.accent, bg, 40);
        Fill(dc, th, c);
    } else {
        RoundFill(dc, th, thick, c, c);
    }
}

LRESULT CALLBACK BarProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Bar* b = (Bar*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (!b) return DefWindowProcW(h, m, w, l);
    POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    int at = Vert(b) ? pt.y : pt.x;
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, std::max(1, (int)rc.right), std::max(1, (int)rc.bottom));
        HGDIOBJ ob = SelectObject(mem, bmp);
        Paint(b, mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!b->tracking) { TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0}; TrackMouseEvent(&t); b->tracking = true; }
        if (!b->hot) { b->hot = true; InvalidateRect(h, nullptr, FALSE); }
        if (b->drag) {
            int start, len;
            Thumb(b, start, len);
            int L = Length(b);
            int range = std::max(1, b->si.nMax - b->si.nMin + 1), page = std::max(1, (int)b->si.nPage);
            int maxPos = std::max(0, range - page);
            int ns = std::max(0, std::min(at - b->dragOff, L - len));
            int pos = b->si.nMin + (L - len > 0 ? (int)((long long)ns * maxPos / (L - len)) : 0);
            if (pos != b->trackPos) {
                b->trackPos = pos;
                ScrollTo(b, pos);
                SCROLLINFO si{sizeof(si), SIF_ALL};
                GetScrollInfo(b->target, b->bar, &si);
                b->si = si;
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        b->tracking = false;
        if (!b->drag) { b->hot = false; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONDOWN: {
        int start, len;
        Thumb(b, start, len);
        SetCapture(h);
        if (at >= start && at < start + len) {
            b->drag = true;
            b->dragOff = at - start;
            b->trackPos = b->si.nPos;
        } else {  // click on the track: page up / down, repeating while held
            b->repeat = at < start ? SB_PAGEUP : SB_PAGEDOWN;
            SendMessageW(b->target, Vert(b) ? WM_VSCROLL : WM_HSCROLL, b->repeat, 0);
            SetTimer(h, TIMER_REPEAT, 350, nullptr);
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_TIMER: {
        int start, len;
        Thumb(b, start, len);
        POINT p; GetCursorPos(&p); ScreenToClient(h, &p);
        int c = Vert(b) ? p.y : p.x;
        bool still = b->repeat == SB_PAGEUP ? c < start : c >= start + len;
        if (b->repeat && still) SendMessageW(b->target, Vert(b) ? WM_VSCROLL : WM_HSCROLL, b->repeat, 0);
        SetTimer(h, TIMER_REPEAT, 60, nullptr);
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (m == WM_LBUTTONUP && GetCapture() == h) ReleaseCapture();
        if (b->drag) {
            b->drag = false;
            SendMessageW(b->target, Vert(b) ? WM_VSCROLL : WM_HSCROLL, SB_ENDSCROLL, 0);
        }
        b->repeat = 0;
        KillTimer(h, TIMER_REPEAT);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        return SendMessageW(b->target, m, w, l);
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        return TRUE;
    }
    return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK TargetProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref) {
    Pair* p = (Pair*)ref;
    if (m == WM_NCDESTROY) {
        RemoveWindowSubclass(h, TargetProc, id);
        for (Bar* b : {&p->v, &p->h}) if (b->wnd) DestroyWindow(b->wnd);
        delete p;
        return DefSubclassProc(h, m, w, l);
    }
    LRESULT r = DefSubclassProc(h, m, w, l);
    switch (m) {  // anything that can change the native scrollbar
    case WM_PAINT: case WM_NCPAINT: case WM_SIZE: case WM_WINDOWPOSCHANGED: case WM_SHOWWINDOW: case WM_STYLECHANGED:
    case WM_VSCROLL: case WM_HSCROLL: case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: case WM_KEYDOWN:
    case LVM_SETITEMCOUNT: case LVM_SCROLL: case LVM_ENSUREVISIBLE: case LVM_DELETEALLITEMS:
    case TVM_EXPAND: case TVM_DELETEITEM: case TVM_ENSUREVISIBLE:
        SyncPair(p);
        break;
    }
    return r;
}

}  // namespace

void ThemedScrollbars(HWND target, int role) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = BarProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kClass;
        RegisterClassW(&wc);
        reg = true;
    }
    // the list must not paint over the bars that lie on top of it
    SetWindowLongPtrW(target, GWL_STYLE, GetWindowLongPtrW(target, GWL_STYLE) | WS_CLIPSIBLINGS);
    wchar_t cls[64] = L"";
    GetClassNameW(target, cls, 64);
    auto p = new Pair();
    for (Bar* b : {&p->v, &p->h}) {
        b->target = target;
        b->bar = b == &p->v ? SB_VERT : SB_HORZ;
        b->role = role;
        b->list = !lstrcmpiW(cls, WC_LISTVIEWW);
        b->wnd = CreateWindowExW(0, kClass, L"", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, GetParent(target), nullptr, g_inst, nullptr);
        SetWindowLongPtrW(b->wnd, GWLP_USERDATA, (LONG_PTR)b);
    }
    SetWindowSubclass(target, TargetProc, 0x5C, (DWORD_PTR)p);
    SyncPair(p);
}
