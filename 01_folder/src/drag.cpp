// Internal drag & drop between panes / tabs / address bar / sidebar, plus drops from other apps.
#include "app.h"
#include "pane.h"
#include "fileops.h"

namespace {

struct {
    bool active = false;
    Pane* src = nullptr;
    vector<wstring> paths;
    HWND tip = nullptr;
    wstring text;
} D;

struct Target { wstring dir; bool pin = false; bool ok = false; };

Target Resolve(POINT pt) {
    Target t;
    HWND under = WindowFromPoint(pt);
    for (int i = 0; i < g_paneCount; i++) {
        Pane* p = g_panes[i];
        if (under == p->list || IsChild(p->hwnd, under) || under == p->hwnd) {
            t.ok = p->DropTarget(pt, under, t.dir);
        } else {
            p->ClearDropHi();
            p->addr.ClearDropHi();
        }
    }
    if (under == SidebarHwnd()) t.ok = SidebarDropTarget(pt, t.dir, t.pin);
    else SidebarClearDropHi();
    return t;
}

bool HasDir(const vector<wstring>& v) {
    for (auto& p : v) if (IsDir(p)) return true;
    return false;
}

// Is the drop meaningful? (not into the same folder for a move, not into itself)
bool Valid(const Target& t, bool copy) {
    if (!t.ok) return false;
    if (t.pin) return HasDir(D.paths);
    if (t.dir.empty()) return false;
    for (auto& p : D.paths) {
        if (IEq(p, t.dir) || (IStarts(t.dir, p) && t.dir.size() > p.size() && t.dir[p.size()] == L'\\')) return false;
        if (!copy && IEq(Parent(p), t.dir)) return false;
    }
    return true;
}

LRESULT CALLBACK TipProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255)), fr = CreateSolidBrush(RGB(120, 160, 210));
        FillRect(dc, &rc, bg);
        FrameRect(dc, &rc, fr);
        DeleteObject(bg); DeleteObject(fr);
        SelectObject(dc, g_font);
        SetBkMode(dc, TRANSPARENT);
        InflateRect(&rc, -S(8), 0);
        DrawTextW(dc, D.text.c_str(), -1, &rc, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        EndPaint(h, &ps);
        return 0;
    }
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
    return DefWindowProcW(h, m, w, l);
}

void SetTip(const wstring& text, POINT pt) {
    if (!D.tip) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = TipProc;
        wc.hInstance = g_inst;
        wc.lpszClassName = L"NxDragTip";
        RegisterClassW(&wc);
        D.tip = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
                                L"NxDragTip", L"", WS_POPUP, 0, 0, 10, 10, g_main, nullptr, g_inst, nullptr);
        SetLayeredWindowAttributes(D.tip, 0, 235, LWA_ALPHA);
    }
    HDC dc = GetDC(D.tip);
    SelectObject(dc, g_font);
    SIZE sz; GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &sz);
    ReleaseDC(D.tip, dc);
    bool changed = text != D.text;
    D.text = text;
    SetWindowPos(D.tip, HWND_TOPMOST, pt.x + S(16), pt.y + S(18), sz.cx + S(16), sz.cy + S(10), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (changed) InvalidateRect(D.tip, nullptr, TRUE);
}

}  // namespace

bool DragActive() { return D.active; }

void DragBegin(Pane* src, const vector<wstring>& paths) {
    D.active = true;
    D.src = src;
    D.paths = paths;
    SetCapture(g_main);
    SetTimer(g_main, 77, 100, nullptr);  // keeps tab-hover / auto-scroll alive without mouse moves
    DragUpdate();
}

void DragUpdate() {
    if (!D.active) return;
    POINT pt; GetCursorPos(&pt);
    bool copy = GetKeyState(VK_CONTROL) < 0;
    Target t = Resolve(pt);
    bool ok = Valid(t, copy);
    wstring what = D.paths.size() == 1 ? L"\"" + FileName(D.paths[0]) + L"\"" : std::to_wstring(D.paths.size()) + L" items";
    wstring text;
    if (!ok) text = L"✕  " + what;
    else if (t.pin) text = L"★  Pin to Favourites";
    else if (IsVirtual(t.dir)) text = L"→  Move " + what + L" to Recycle Bin";
    else text = (copy ? L"+  Copy " : L"→  Move ") + what + L" to " + (IsRoot(t.dir) ? t.dir.substr(0, 2) : FileName(t.dir));
    SetTip(text, pt);
    SetCursor(LoadCursor(nullptr, ok ? IDC_ARROW : IDC_NO));
}

void DragEnd(bool drop) {
    if (!D.active) return;
    D.active = false;
    KillTimer(g_main, 77);
    if (GetCapture() == g_main) ReleaseCapture();
    if (D.tip) ShowWindow(D.tip, SW_HIDE);
    POINT pt; GetCursorPos(&pt);
    bool copy = GetKeyState(VK_CONTROL) < 0;
    Target t = Resolve(pt);
    for (int i = 0; i < 3; i++) if (g_panes[i]) { g_panes[i]->ClearDropHi(); g_panes[i]->addr.ClearDropHi(); }
    SidebarClearDropHi();
    if (!drop || !Valid(t, copy)) return;
    if (t.pin) { for (auto& p : D.paths) if (IsDir(p)) AddFavourite(p); return; }
    if (IsVirtual(t.dir)) { FileOp(OP_RECYCLE, D.paths); return; }
    FileOp(copy ? OP_COPY : OP_MOVE, D.paths, t.dir);
}

void DropExternal(HDROP h) {
    vector<wstring> files;
    UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < n; i++) {
        UINT len = DragQueryFileW(h, i, nullptr, 0);
        wstring s(len, L'\0');
        DragQueryFileW(h, i, &s[0], len + 1);
        files.push_back(s);
    }
    DragFinish(h);
    if (files.empty()) return;
    D.paths = files;
    POINT pt; GetCursorPos(&pt);
    Target t = Resolve(pt);
    for (int i = 0; i < 3; i++) if (g_panes[i]) { g_panes[i]->ClearDropHi(); g_panes[i]->addr.ClearDropHi(); }
    SidebarClearDropHi();
    if (!Valid(t, true)) return;
    if (t.pin) { for (auto& p : files) if (IsDir(p)) AddFavourite(p); return; }
    if (IsVirtual(t.dir)) { FileOp(OP_RECYCLE, files); return; }
    FileOp(GetKeyState(VK_SHIFT) < 0 ? OP_MOVE : OP_COPY, files, t.dir);
}
