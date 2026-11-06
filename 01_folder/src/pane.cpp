#include "pane.h"
#include "app.h"
#include "columns.h"
#include "clip.h"
#include "fileops.h"
#include "theme.h"

enum { ID_BACK = 201, ID_FWD, ID_UP, ID_REFRESH, ID_STAR };
static const wchar_t* kGlyph[] = {L"", L"", L"", L"", L""};
static const wchar_t* kTip[] = {L"Back (Alt+Left)", L"Forward (Alt+Right)", L"Up (Alt+Up)", L"Refresh (F5)", L"Add to Favourites (Ctrl+D)"};
static int g_nextTabId = 1;
static const int kTabH = 36, kNavH = 42, kTitleH = 38, kStatusH = 28;  // row heights at 96 dpi

static wstring LnkFolder(const wstring& lnk) {  // .lnk pointing to a folder -> folder path
    wstring r;
    IShellLinkW* sl = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
        IPersistFile* pf = nullptr;
        if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) {
            wchar_t t[MAX_PATH];
            if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ)) && SUCCEEDED(sl->GetPath(t, MAX_PATH, nullptr, 0)) && IsDir(t)) r = t;
            pf->Release();
        }
        sl->Release();
    }
    return r;
}

void Pane::Create(HWND parent, int index) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = StaticProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"NxPane";
        RegisterClassW(&wc);
        WNDCLASSW tc{};
        tc.lpfnWndProc = TabsWndProc;
        tc.hInstance = g_inst;
        tc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        tc.lpszClassName = L"NxTabs";
        tc.style = CS_DBLCLKS;
        RegisterClassW(&tc);
        reg = true;
    }
    index_ = index;
    hwnd = CreateWindowExW(0, L"NxPane", L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10, parent, nullptr, g_inst, this);
    tabs = CreateWindowExW(0, L"NxTabs", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, nullptr, g_inst, this);
    tip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, hwnd, nullptr, g_inst, nullptr);
    for (int i = 0; i < 5; i++) {
        btn_[i] = CreateWindowW(L"BUTTON", kGlyph[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)(ID_BACK + i), g_inst, nullptr);
        TTTOOLINFOW ti{sizeof(ti), TTF_IDISHWND | TTF_SUBCLASS, hwnd, (UINT_PTR)btn_[i]};
        ti.lpszText = (LPWSTR)kTip[i];
        SendMessageW(tip_, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    }
    TTTOOLINFOW ti{sizeof(ti), TTF_SUBCLASS, tabs, 1};
    ti.lpszText = LPSTR_TEXTCALLBACKW;
    ti.rect = {0, 0, 32000, 32000};
    SendMessageW(tip_, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    addr.Create(hwnd, this);
    list = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA |
                           LVS_SHOWSELALWAYS | LVS_EDITLABELS | LVS_SHAREIMAGELISTS, 0, 0, 10, 10, hwnd, nullptr, g_inst, nullptr);
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_HEADERDRAGDROP);
    ListView_SetCallbackMask(list, LVIS_CUT | LVIS_DROPHILITED);
    SetWindowSubclass(list, ListProc, 1, (DWORD_PTR)this);
    SetWindowSubclass(ListView_GetHeader(list), HeaderProc, 1, (DWORD_PTR)this);
    ApplyTheme();
}

void Pane::ApplyTheme() {
    const Theme& t = *g_theme;
    ThemeWindow(list);
    ThemeWindow(tip_);
    SendMessageW(list, WM_SETFONT, (WPARAM)g_font, FALSE);
    ListView_SetBkColor(list, t.pane);
    ListView_SetTextBkColor(list, t.pane);
    ListView_SetTextColor(list, t.text);
    {  // rows are custom-drawn: an empty image list sets the row height and the icon slot
        HIMAGELIST old = rowIml_;
        rowIml_ = ImageList_Create(S(22), S(t.rowH), ILC_COLOR32, 1, 0);
        ListView_SetImageList(list, rowIml_, LVSIL_SMALL);
        if (old) ImageList_Destroy(old);
    }
    SetWindowPos(list, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    Layout();
    for (HWND h : {hwnd, list, tabs, addr.hwnd, ListView_GetHeader(list)}) InvalidateRect(h, nullptr, TRUE);
}

// ---------- layout / painting ----------
void Pane::Layout() {
    if (!list) return;
    RECT rc; GetClientRect(hwnd, &rc);
    int w = rc.right, bw = S(32), bh = S(30);
    MoveWindow(tabs, 0, 0, w, S(kTabH), TRUE);
    LayoutTabs();
    int y = S(kTabH), ny = y + (S(kNavH) - bh) / 2;
    for (int i = 0; i < 3; i++) MoveWindow(btn_[i], S(8) + i * bw, ny, bw - S(2), bh, TRUE);
    MoveWindow(btn_[3], w - S(8) - bw, ny, bw - S(2), bh, TRUE);  // refresh
    int ax = S(8) + 3 * bw + S(8);
    MoveWindow(addr.hwnd, ax, ny, std::max(S(40), w - S(14) - bw - ax), bh, TRUE);
    y += S(kNavH);
    MoveWindow(btn_[4], w - S(8) - bw, y + (S(kTitleH) - bh) / 2, bw - S(2), bh, TRUE);  // star
    y += S(kTitleH);
    MoveWindow(list, 0, y, w, std::max(0, (int)rc.bottom - S(kStatusH) - y), TRUE);
}

void Pane::DrawButton(DRAWITEMSTRUCT* d) {
    const Theme& t = *g_theme;
    int id = d->CtlID;
    bool fav = id == ID_STAR && IsFavourite(Path());
    bool dis = d->itemState & ODS_DISABLED;
    Fill(d->hDC, d->rcItem, t.pane);
    if (d->itemState & ODS_SELECTED) RoundFill(d->hDC, d->rcItem, S(6), t.hot, t.hot);
    COLORREF c = dis ? t.faint : fav ? RGB(250, 190, 40) : t.dim;
    Glyph(d->hDC, fav ? L"" : kGlyph[id - ID_BACK], d->rcItem, c);
}

void Pane::UpdateStar() { InvalidateRect(btn_[4], nullptr, FALSE); }

// ---------- tabs ----------
void Pane::NewTab(const wstring& path, bool activate) {
    auto t = std::make_unique<Tab>();
    t->id = g_nextTabId++;
    t->path = path;
    tabs_.push_back(std::move(t));
    if (activate || tabs_.size() == 1) SwitchTab((int)tabs_.size() - 1);
    else { tabs_.back()->loading = true; ListAsync(hwnd, tabs_.back()->id, 0, path, C_NAME, true); }
    UpdateTabs();
}

void Pane::ResetTabs(const vector<wstring>& paths, int cur) {
    if (editing_) ListView_CancelEditLabel(list);
    tabs_.clear();
    cur_ = -1;
    if (paths.empty()) { NewTab(L""); return; }
    cur = std::max(0, std::min(cur, (int)paths.size() - 1));
    for (int i = 0; i < (int)paths.size(); i++) NewTab(paths[i], i == cur);  // the first tab is always activated too
}

void Pane::CloseTab(int i) {
    if (tabs_.size() <= 1) { Navigate(L""); tabs_[0]->back.clear(); tabs_[0]->fwd.clear(); return; }
    if (i == cur_) SaveView();
    tabs_.erase(tabs_.begin() + i);
    int n = cur_;
    if (i < cur_ || n >= (int)tabs_.size()) n = std::max(0, cur_ - 1);
    cur_ = -1;
    tabHot_ = closeHot_ = -1;
    SwitchTab(std::min(n, (int)tabs_.size() - 1));
    UpdateTabs();
}

void Pane::SwitchTab(int i) {
    if (i < 0 || i >= (int)tabs_.size()) return;
    if (cur_ >= 0 && cur_ < (int)tabs_.size() && cur_ != i) SaveView();
    cur_ = i;
    Tab* t = T();
    addr.SetPath(t->path);
    UpdateStar();
    watcher_.Watch(hwnd, t->path);
    if (t->items.empty() && !t->loading) Load();
    else { ShowItems(true); if (!t->loading) Refresh(); }
    Layout();
    InvalidateRect(hwnd, nullptr, FALSE);
}

void Pane::UpdateTabs() {
    LayoutTabs();
    InvalidateRect(hwnd, nullptr, FALSE);  // title row
}

void Pane::LayoutTabs() {
    RECT rc; GetClientRect(tabs, &rc);
    HDC dc = GetDC(tabs);
    HGDIOBJ of = SelectObject(dc, g_font);
    int n = (int)tabs_.size(), x = S(6);
    int avail = rc.right - x - S(40) - (g_paneCount > 1 ? S(96) : S(8));
    vector<int> widths;
    int total = 0;
    for (auto& tb : tabs_) {
        wstring s = DisplayName(tb->path);
        SIZE sz; GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
        int w = std::max(S(90), std::min(S(210), (int)sz.cx + S(66)));
        widths.push_back(w);
        total += w;
    }
    if (n && total > avail) { int each = std::max(S(56), avail / n); for (auto& w : widths) w = std::min(w, each); }
    tabRects_.clear();
    for (int i = 0; i < n; i++) { tabRects_.push_back({x, S(4), x + widths[i], rc.bottom}); x += widths[i] + S(2); }
    plusRect_ = {x + S(4), S(9), x + S(30), rc.bottom - S(5)};
    SelectObject(dc, of);
    ReleaseDC(tabs, dc);
    InvalidateRect(tabs, nullptr, FALSE);
}

int Pane::HitTab(POINT pt) {
    for (int i = 0; i < (int)tabRects_.size(); i++) if (PtInRect(&tabRects_[i], pt)) return i;
    return -1;
}

// "×" area at the right of tab i
bool Pane::CloseRect(int i, RECT* r) {
    if (i < 0 || i >= (int)tabRects_.size()) return false;
    *r = tabRects_[i];
    r->right -= S(8);
    r->left = r->right - S(20);
    int mid = (r->top + r->bottom) / 2;
    r->top = mid - S(10);
    r->bottom = mid + S(10);
    return true;
}

void Pane::PaintTabs(HDC dc) {
    const Theme& t = *g_theme;
    RECT rc; GetClientRect(tabs, &rc);
    Fill(dc, rc, t.app);
    RECT line = rc; line.top = line.bottom - 1;
    Fill(dc, line, t.border);
    bool active = ActivePane() == this;
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < (int)tabRects_.size(); i++) {
        RECT r = tabRects_[i];
        bool cur = i == cur_;
        if (cur) {
            Fill(dc, r, t.pane);
            RECT e = r; e.right = e.left + 1; Fill(dc, e, t.border);
            e = r; e.left = e.right - 1; Fill(dc, e, t.border);
            e = r; e.bottom = e.top + (active ? S(2) : 1); Fill(dc, e, active ? t.accent : t.border);
        } else if (i == tabHot_) {
            Fill(dc, r, t.hot);
        }
        const wstring& p = tabs_[i]->path;
        RECT ic = r; ic.left += S(10); ic.right = ic.left + S(18);
        Glyph(dc, IsVirtual(p) ? L"" : p.empty() ? L"" : L"", ic, cur ? t.text : t.dim);
        RECT tr = r; tr.left = ic.right + S(6); tr.right -= S(32);
        SelectObject(dc, g_font);
        SetTextColor(dc, cur ? t.text : t.dim);
        DrawTextW(dc, DisplayName(p).c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT cr;
        if ((cur || i == tabHot_) && CloseRect(i, &cr)) {
            if (i == closeHot_) RoundFill(dc, cr, S(4), RGB(200, 60, 60), RGB(200, 60, 60));
            Glyph(dc, L"", cr, i == closeHot_ ? RGB(255, 255, 255) : t.dim, g_iconFontSmall);
        }
    }
    if (tabHot_ == -2) RoundFill(dc, plusRect_, S(4), t.hot, t.hot);
    Glyph(dc, L"", plusRect_, t.dim, g_iconFontSmall);
    if (g_paneCount > 1) {  // "L / ACTIVE" like the Void design
        static const wchar_t* k3[] = {L"L", L"M", L"R"};
        wstring lbl = g_paneCount == 3 ? k3[index_] : (index_ == 0 ? L"L" : L"R");
        if (active) lbl += L" / ACTIVE";
        RECT lr = rc; lr.right -= S(12); lr.left = lr.right - S(110);
        SelectObject(dc, g_fontMono);
        SetTextColor(dc, active ? t.dim : t.faint);
        DrawTextW(dc, lbl.c_str(), -1, &lr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
}

void Pane::TabMenu(int i, POINT screen) {
    enum { M_CLOSE = 1, M_OTHERS, M_RIGHT, M_DUP, M_NEW };
    int n = (int)tabs_.size();
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, M_CLOSE, L"Close tab\tCtrl+W");
    AppendMenuW(m, MF_STRING | (n > 1 ? 0 : MF_GRAYED), M_OTHERS, L"Close other tabs");
    AppendMenuW(m, MF_STRING | (i + 1 < n ? 0 : MF_GRAYED), M_RIGHT, L"Close tabs to the right");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_DUP, L"Duplicate tab");
    AppendMenuW(m, MF_STRING, M_NEW, L"New tab\tCtrl+T");
    int r = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, screen.x, screen.y, 0, hwnd, nullptr);
    DestroyMenu(m);
    switch (r) {
    case M_CLOSE: CloseTab(i); break;
    case M_OTHERS:
        SwitchTab(i);
        for (int j = n - 1; j >= 0; j--) if (j != i) CloseTab(j);
        break;
    case M_RIGHT:
        if (cur_ > i) SwitchTab(i);
        for (int j = n - 1; j > i; j--) CloseTab(j);
        break;
    case M_DUP: NewTab(tabs_[i]->path); break;
    case M_NEW: NewTab(L""); break;
    }
    FocusList();
}

LRESULT CALLBACK Pane::TabsWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto self = (Pane*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        self = (Pane*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
    }
    if (!self) return DefWindowProcW(h, m, w, l);
    POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);
        self->PaintTabs(mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: self->LayoutTabs(); return 0;
    case WM_MOUSEMOVE: {
        if (!self->tabTracking_) {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            self->tabTracking_ = true;
        }
        int hot = self->HitTab(pt);
        if (hot < 0 && PtInRect(&self->plusRect_, pt)) hot = -2;
        RECT cr;
        int ch = hot >= 0 && self->CloseRect(hot, &cr) && PtInRect(&cr, pt) ? hot : -1;
        if (hot != self->tabHot_ || ch != self->closeHot_) {
            if (hot != self->tabHot_) SendMessageW(self->tip_, TTM_POP, 0, 0);
            self->tabHot_ = hot;
            self->closeHot_ = ch;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        self->tabTracking_ = false;
        self->tabHot_ = self->closeHot_ = -1;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        SetActive(self);
        int i = self->HitTab(pt);
        RECT cr;
        if (i >= 0 && self->CloseRect(i, &cr) && PtInRect(&cr, pt)) self->CloseTab(i);
        else if (i >= 0) self->SwitchTab(i);
        else if (PtInRect(&self->plusRect_, pt)) self->NewTab(L"");
        self->FocusList();
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        if (self->HitTab(pt) < 0 && !PtInRect(&self->plusRect_, pt)) { self->NewTab(L""); self->FocusList(); }
        return 0;
    case WM_MBUTTONUP: {
        int i = self->HitTab(pt);
        if (i >= 0) self->CloseTab(i);
        return 0;
    }
    case WM_RBUTTONUP: {
        int i = self->HitTab(pt);
        if (i >= 0) { ClientToScreen(h, &pt); self->TabMenu(i, pt); }
        return 0;
    }
    case WM_NOTIFY: {
        auto nm = (NMHDR*)l;
        if (nm->code == TTN_GETDISPINFOW) {
            static wstring tipText;
            int i = self->tabHot_;
            tipText = i >= 0 && i < (int)self->tabs_.size()
                          ? (IsFsPath(self->tabs_[i]->path) ? self->tabs_[i]->path : DisplayName(self->tabs_[i]->path))
                          : i == -2 ? L"New tab (Ctrl+T)" : L"";
            ((NMTTDISPINFOW*)l)->lpszText = (LPWSTR)tipText.c_str();
        }
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------- list drawing (custom-drawn themes) ----------
void Pane::DrawRow(HDC dc, int i) {
    const Theme& th = *g_theme;
    Tab* t = T();
    if (i < 0 || i >= (int)t->items.size()) return;
    const Item& it = t->items[i];
    RECT row, client;
    ListView_GetItemRect(list, i, &row, LVIR_BOUNDS);
    GetClientRect(list, &client);
    row.left = client.left;
    row.right = std::max(row.right, client.right);
    bool sel = ListView_GetItemState(list, i, LVIS_SELECTED) != 0;
    Fill(dc, row, sel ? th.sel : i == hotRow_ || i == dropHi_ ? th.hot : th.pane);
    if (sel) { RECT bar = row; bar.right = bar.left + S(2); Fill(dc, bar, th.selBorder); }
    if (i == dropHi_) Frame(dc, row, th.accent);
    if (th.rowLine != th.pane) { RECT ln = row; ln.top = ln.bottom - 1; Fill(dc, ln, th.rowLine); }
    if (GetFocus() == list && ListView_GetItemState(list, i, LVIS_FOCUSED) && !sel) Frame(dc, row, th.border);
    bool dim = (it.attr & FILE_ATTRIBUTE_HIDDEN) || (!it.dtype && !it.name.empty() && it.name[0] == L'.') ||
               (!g_cutSet.empty() && g_cutSet.count(Lower(ItemPath(i))));
    SetBkMode(dc, TRANSPARENT);
    for (int sub = 0; sub < (int)cols_.size(); sub++) {
        int col = cols_[sub];
        RECT cr;
        if (sub == 0) {
            RECT ic;
            ListView_GetSubItemRect(list, i, 0, LVIR_ICON, &ic);
            ic.left += S(8); ic.right += S(8);
            DrawItemIcon(dc, ic, it);
            ListView_GetSubItemRect(list, i, 0, LVIR_LABEL, &cr);
            cr.left += S(12);
        } else {
            ListView_GetSubItemRect(list, i, sub, LVIR_BOUNDS, &cr);
            cr.left += S(8);
        }
        cr.right -= S(8);
        bool mono = col == C_MTIME || col == C_CTIME || col == C_ATIME || col == C_SIZE || col == C_FREE || col == C_TOTAL ||
                    col == C_DELETED || col == C_ATTR || col == C_EXT;
        SelectObject(dc, mono ? g_fontMono : g_font);
        SetTextColor(dc, dim ? th.faint : col == C_NAME || sel ? th.text : th.dim);
        wstring s = CellText(it, col);
        if (col == C_SIZE && s.empty() && !it.dtype) s = L"—";  // folders: an em dash, like the design
        DrawTextW(dc, s.c_str(), -1, &cr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | (g_colDefs[col].right ? DT_RIGHT : DT_LEFT));
    }
}

void Pane::PaintHeader(HWND h, HDC dc) {
    const Theme& t = *g_theme;
    RECT rc; GetClientRect(h, &rc);
    Fill(dc, rc, t.pane);
    RECT ln = rc; ln.top = ln.bottom - 1;
    Fill(dc, ln, t.border);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, g_fontSmall);
    int n = Header_GetItemCount(h);
    for (int k = 0; k < n; k++) {
        RECT r;
        Header_GetItemRect(h, k, &r);
        wchar_t buf[128] = L"";
        HDITEMW hi{HDI_TEXT | HDI_FORMAT};
        hi.pszText = buf;
        hi.cchTextMax = 128;
        Header_GetItem(h, k, &hi);
        r.left += k == 0 ? S(16) : S(8);
        r.right -= S(8);
        bool right = hi.fmt & HDF_RIGHT;
        SetTextColor(dc, t.dim);
        RECT tr = r;
        DrawTextW(dc, buf, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | (right ? DT_RIGHT : DT_LEFT));
        if (hi.fmt & (HDF_SORTUP | HDF_SORTDOWN)) {
            SIZE sz; GetTextExtentPoint32W(dc, buf, lstrlenW(buf), &sz);
            RECT ar = r;
            if (right) { ar.right = r.right - sz.cx - S(4); ar.left = ar.right - S(14); }
            else { ar.left = r.left + sz.cx + S(4); ar.right = ar.left + S(14); }
            Glyph(dc, hi.fmt & HDF_SORTUP ? L"" : L"", ar, t.dim, g_iconFontSmall);
            SelectObject(dc, g_fontSmall);
        }
    }
}

LRESULT CALLBACK Pane::HeaderProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) {
    auto self = (Pane*)ref;
    switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);
        self->PaintHeader(h, mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case HDM_LAYOUT: {  // a taller header, like the design
        LRESULT r = DefSubclassProc(h, m, w, l);
        auto hl = (HDLAYOUT*)l;
        int hh = S(30);
        hl->prc->top += hh - hl->pwpos->cy;
        hl->pwpos->cy = hh;
        return r;
    }
    }
    return DefSubclassProc(h, m, w, l);
}

LRESULT CALLBACK Pane::ListProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) {
    auto self = (Pane*)ref;
    switch (m) {
    case WM_MOUSEMOVE: {
        if (!self->rowTracking_) {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            self->rowTracking_ = true;
        }
        LVHITTESTINFO hi{};
        hi.pt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        int i = ListView_HitTest(h, &hi);
        if (i != self->hotRow_) {
            int old = self->hotRow_;
            self->hotRow_ = i;
            if (old >= 0) ListView_RedrawItems(h, old, old);
            if (i >= 0) ListView_RedrawItems(h, i, i);
        }
        break;
    }
    case WM_MOUSELEAVE:
        self->rowTracking_ = false;
        if (self->hotRow_ >= 0) { int old = self->hotRow_; self->hotRow_ = -1; ListView_RedrawItems(h, old, old); }
        break;
    case WM_CTLCOLOREDIT:  // the rename box
        if (g_theme->dark) {
            SetTextColor((HDC)w, g_theme->text);
            SetBkColor((HDC)w, g_theme->input);
            return (LRESULT)ThemeBrush(g_theme->input);
        }
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

// ---------- navigation / listing ----------
void Pane::SaveView() {
    Tab* t = T();
    t->selNames.clear();
    for (int i = -1; (i = ListView_GetNextItem(list, i, LVNI_SELECTED)) >= 0;)
        if (i < (int)t->items.size()) t->selNames.push_back(t->items[i].name);
    int f = ListView_GetNextItem(list, -1, LVNI_FOCUSED);
    t->focusName = f >= 0 && f < (int)t->items.size() ? t->items[f].name : L"";
    t->top = ListView_GetTopIndex(list);
}

void Pane::Load() {
    Tab* t = T();
    t->gen++;
    t->loading = true;
    t->err = 0;
    ListAsync(hwnd, t->id, t->gen, t->path, t->sortCol, t->asc);
    UpdateStatus();
}

void Pane::Navigate(const wstring& p, bool hist, const wstring& select) {
    wstring path = p;
    while (path.size() > 3 && path.back() == L'\\') path.pop_back();
    if (path.size() == 2 && path[1] == L':') path += L'\\';
    Tab* t = T();
    if (editing_) ListView_CancelEditLabel(list);
    if (IEq(path, t->path) && select.empty()) { Refresh(); return; }
    if (hist && !IEq(path, t->path)) { t->back.push_back(t->path); t->fwd.clear(); }
    t->path = path;
    t->items.clear();
    t->selNames.clear();
    t->focusName = select;
    if (!select.empty()) t->selNames.push_back(select);
    t->top = -1;
    ShowItems(false);
    Load();
    addr.SetPath(path);
    UpdateTabs();
    UpdateStar();
    watcher_.Watch(hwnd, path);
    if (IsFsPath(path)) TrackVisit(path);
}

void Pane::Refresh() {
    if (editing_) { refreshPending_ = true; return; }
    SaveView();
    Load();
}

void Pane::GoBack() {
    Tab* t = T();
    if (t->back.empty()) return;
    wstring from = t->path, to = t->back.back();
    t->back.pop_back();
    t->fwd.push_back(from);
    Navigate(to, false, IEq(Parent(from), to) ? FileName(from) : L"");
}

void Pane::GoForward() {
    Tab* t = T();
    if (t->fwd.empty()) return;
    wstring from = t->path, to = t->fwd.back();
    t->fwd.pop_back();
    t->back.push_back(from);
    Navigate(to, false, IEq(Parent(from), to) ? FileName(from) : L"");
}

void Pane::GoUp() {
    Tab* t = T();
    if (t->path.empty()) return;
    wstring from = t->path;
    Navigate(Parent(from), true, IsRoot(from) ? from : FileName(from));
}

void Pane::RebuildColumns() {
    int mode = Mode();
    if (colsMode_ >= 0)  // remember widths of current columns
        for (size_t i = 0; i < cols_.size(); i++) g_colW[cols_[i]] = MulDiv(ListView_GetColumnWidth(list, (int)i), 96, g_dpi);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    while (ListView_DeleteColumn(list, 0)) {}
    cols_ = VisibleColumns(mode);
    for (size_t i = 0; i < cols_.size(); i++) {
        int c = cols_[i];
        LVCOLUMNW lc{LVCF_TEXT | LVCF_WIDTH | LVCF_FMT};
        lc.pszText = (LPWSTR)g_colDefs[c].name;
        lc.cx = S(g_colW[c]);
        lc.fmt = g_colDefs[c].right ? LVCFMT_RIGHT : LVCFMT_LEFT;
        ListView_InsertColumn(list, (int)i, &lc);
    }
    colsMode_ = mode;
    // sort arrow
    HWND hdr = ListView_GetHeader(list);
    for (size_t i = 0; i < cols_.size(); i++) {
        HDITEMW hi{HDI_FORMAT};
        Header_GetItem(hdr, (int)i, &hi);
        hi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (cols_[i] == T()->sortCol) hi.fmt |= T()->asc ? HDF_SORTUP : HDF_SORTDOWN;
        Header_SetItem(hdr, (int)i, &hi);
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
}

void Pane::ShowItems(bool restore) {
    Tab* t = T();
    int mode = Mode();
    if (mode != colsMode_) RebuildColumns();
    else {  // refresh sort arrow
        HWND hdr = ListView_GetHeader(list);
        for (size_t i = 0; i < cols_.size(); i++) {
            HDITEMW hi{HDI_FORMAT};
            Header_GetItem(hdr, (int)i, &hi);
            hi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
            if (cols_[i] == t->sortCol) hi.fmt |= t->asc ? HDF_SORTUP : HDF_SORTDOWN;
            Header_SetItem(hdr, (int)i, &hi);
        }
    }
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemCountEx(list, t->items.size(), 0);
    dropHi_ = -1;
    if (restore && !t->items.empty()) {
        std::unordered_set<wstring> sel(t->selNames.begin(), t->selNames.end());
        int focus = -1, firstSel = -1;
        for (int i = 0; i < (int)t->items.size(); i++) {
            const wstring& n = t->items[i].name;
            if (!sel.empty() && sel.count(n)) { ListView_SetItemState(list, i, LVIS_SELECTED, LVIS_SELECTED); if (firstSel < 0) firstSel = i; }
            if (focus < 0 && !t->focusName.empty() && IEq(n, t->focusName)) focus = i;
        }
        if (focus < 0) focus = firstSel;
        if (focus < 0) focus = 0;
        ListView_SetItemState(list, focus, LVIS_FOCUSED, LVIS_FOCUSED);
        if (t->top >= 0) {
            RECT r{LVIR_BOUNDS};
            int top = ListView_GetTopIndex(list);
            if (ListView_GetItemRect(list, 0, &r, LVIR_BOUNDS)) ListView_Scroll(list, 0, (t->top - top) * (r.bottom - r.top));
            if (!t->focusName.empty() || firstSel >= 0) ListView_EnsureVisible(list, focus, FALSE);
        } else {
            ListView_EnsureVisible(list, focus, FALSE);
        }
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
    UpdateStatus();
    EnableWindow(btn_[ID_BACK - ID_BACK], !t->back.empty());
    EnableWindow(btn_[ID_FWD - ID_BACK], !t->fwd.empty());
    EnableWindow(btn_[ID_UP - ID_BACK], !t->path.empty());
    EnableWindow(btn_[ID_STAR - ID_BACK], IsFsPath(t->path));
    InvalidateRect(hwnd, nullptr, FALSE);  // title row
}

void Pane::OnDrivesChanged() {
    for (auto& t : tabs_)
        if (t->path.empty() && t.get() == T()) Refresh();
}

void Pane::UpdateStatus() {
    statusPending_ = false;
    Tab* t = T();
    statusR_.clear();
    if (t->loading && t->items.empty()) statusL_ = L"Loading…";
    else if (t->err) statusL_ = L"⚠  " + ErrText(t->err);
    else {
        int dirs = 0;
        for (auto& it : t->items) if (it.isDir()) dirs++;
        statusR_ = std::to_wstring(dirs) + L" folders · " + std::to_wstring(t->items.size() - dirs) + L" files";
        int n = ListView_GetSelectedCount(list);
        statusL_ = std::to_wstring(t->items.size()) + L" items";
        if (n) {
            int first = ListView_GetNextItem(list, -1, LVNI_SELECTED);
            statusL_ = std::to_wstring(n) + L" selected";
            if (n == 1 && first < (int)t->items.size()) statusL_ += L" · " + t->items[first].name;
            ULONGLONG sz = 0;
            bool any = false;
            for (int i = -1; (i = ListView_GetNextItem(list, i, LVNI_SELECTED)) >= 0;)
                if (i < (int)t->items.size() && (!t->items[i].isDir() || !t->items[i].real.empty())) { sz += t->items[i].size; any = true; }
            if (any) statusL_ += L" · " + FmtSize(sz);
        }
    }
    RECT rc; GetClientRect(hwnd, &rc);
    rc.top = rc.bottom - S(kStatusH);
    InvalidateRect(hwnd, &rc, FALSE);
}

wstring Pane::ItemPath(int i) {
    Tab* t = T();
    if (i < 0 || i >= (int)t->items.size()) return L"";
    if (!t->items[i].real.empty()) return t->items[i].real;  // Recycle Bin: the $R item on disk
    return t->path.empty() ? t->items[i].name : Join(t->path, t->items[i].name);
}

int Pane::Mode() {
    const wstring& p = T()->path;
    return p.empty() ? 1 : IsVirtual(p) ? 2 : 0;
}

// ---------- Recycle Bin view ----------
void Pane::BinDelete(bool all) {
    Tab* t = T();
    vector<wstring> src;
    int n = 0;
    for (int i = 0; i < (int)t->items.size(); i++) {
        if (!all && !ListView_GetItemState(list, i, LVIS_SELECTED)) continue;
        src.push_back(t->items[i].real);
        src.push_back(BinInfoPath(t->items[i].real));  // the record goes too, or Windows shows a ghost entry
        n++;
    }
    if (!n) return;
    wstring q = all ? L"Empty the Recycle Bin?\n\nAll " + std::to_wstring(n) + L" items will be permanently deleted."
              : n == 1 ? L"Permanently delete \"" + t->items[ListView_GetNextItem(list, -1, LVNI_SELECTED)].name + L"\"?"
                       : L"Permanently delete " + std::to_wstring(n) + L" items?";
    if (MessageBoxW(g_main, q.c_str(), L"Recycle Bin", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    FileOp(OP_DELETE, src);
}

void Pane::BinRestore() {
    Tab* t = T();
    auto dirs = new vector<wstring>();
    wstring errors;
    for (int i = -1; (i = ListView_GetNextItem(list, i, LVNI_SELECTED)) >= 0;) {
        const Item& it = t->items[i];
        if (!IsDir(it.label)) SHCreateDirectoryExW(nullptr, it.label.c_str(), nullptr);
        wstring target = Join(it.label, it.name);
        if (Exists(target)) target = Join(it.label, UniqueName(it.label, it.name, false));  // keep both
        if (MoveFileExW(it.real.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED)) {
            DeleteFileW(BinInfoPath(it.real).c_str());
            dirs->push_back(it.label);
        } else {
            errors += Join(it.label, it.name) + L"\n      " + ErrText(GetLastError()) + L"\n";
        }
    }
    if (!errors.empty()) MessageBoxW(g_main, (L"Could not restore:\n\n" + errors).c_str(), L"Recycle Bin", MB_ICONWARNING);
    SHUpdateRecycleBinIcon();
    PostMessageW(g_main, WM_APP_OPDONE, 0, (LPARAM)dirs);  // refreshes this view and the restored folders
}

vector<wstring> Pane::Selected() {
    vector<wstring> v;
    for (int i = -1; (i = ListView_GetNextItem(list, i, LVNI_SELECTED)) >= 0;) v.push_back(ItemPath(i));
    return v;
}

// ---------- actions ----------
void Pane::OpenItems(bool newTab) {
    Tab* t = T();
    int f = ListView_GetNextItem(list, -1, LVNI_FOCUSED);
    if (f < 0 || f >= (int)t->items.size() || ListView_GetItemState(list, f, LVIS_SELECTED) == 0)
        f = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (f < 0 || Mode() == 2) return;  // recycled items are restored or deleted, not opened
    if (t->items[f].isDir()) { newTab ? NewTab(ItemPath(f)) : Navigate(ItemPath(f)); return; }
    if (IEq(PathFindExtensionW(t->items[f].name.c_str()), L".lnk")) {
        wstring d = LnkFolder(ItemPath(f));
        if (!d.empty()) { newTab ? NewTab(d) : Navigate(d); return; }
    }
    int n = 0;
    for (int i = -1; (i = ListView_GetNextItem(list, i, LVNI_SELECTED)) >= 0 && n < 32;)
        if (!t->items[i].isDir()) { OpenInShell(ItemPath(i)); n++; }
}

void Pane::Copy(bool cut, bool link) {
    vector<wstring> sel = Selected();
    if (sel.empty() || Mode() == 2) return;
    if (T()->path.empty()) { cut = false; }
    ClipSetFiles(g_main, sel, cut, link);
    if (cut) SetCutSet(sel); else ClearCutSet();
}

void Pane::CopyPath() {
    vector<wstring> sel = Selected();
    if (sel.empty() && !Path().empty()) sel.push_back(Path());
    wstring s;
    for (auto& p : sel) s += (s.empty() ? L"" : L"\r\n") + p;
    if (!s.empty()) ClipSetText(g_main, s);
}

void Pane::Paste(const wstring& dir) {
    vector<wstring> files;
    bool move;
    if (ClipGetFiles(g_main, files, move)) {
        if (dir.empty()) { MessageBeep(MB_ICONWARNING); return; }
        if (IsVirtual(dir)) { FileOp(OP_RECYCLE, files); if (move) ClipClear(g_main); return; }  // paste into the bin = delete
        FileOp(move ? OP_MOVE : OP_COPY, files, dir);
        if (move) { ClipClear(g_main); ClearCutSet(); }
        return;
    }
    wstring text, d, sel;  // a path as text: go there
    if (ClipGetText(g_main, text) && text.find(L'\n') == wstring::npos && ResolveUserPath(text, Path(), d, sel)) Navigate(d, true, sel);
}

void Pane::Delete(bool permanent) {
    if (Mode() == 2) { BinDelete(false); return; }  // in the bin, Del always deletes for good
    vector<wstring> sel = Selected();
    if (sel.empty() || Path().empty()) return;
    if (permanent) {
        wstring q = sel.size() == 1 ? L"Permanently delete \"" + FileName(sel[0]) + L"\"?" : L"Permanently delete " + std::to_wstring(sel.size()) + L" items?";
        if (MessageBoxW(g_main, q.c_str(), L"Delete", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    }
    // keep a sensible focus after the items disappear
    int last = -1;
    for (int i = -1; (i = ListView_GetNextItem(list, i, LVNI_SELECTED)) >= 0;) last = i;
    int next = last + 1;
    while (next < (int)T()->items.size() && ListView_GetItemState(list, next, LVIS_SELECTED)) next++;
    int first = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (next >= (int)T()->items.size()) next = first - 1;
    T()->focusName = next >= 0 ? T()->items[next].name : L"";
    T()->selNames = {T()->focusName};
    FileOp(permanent ? OP_DELETE : OP_RECYCLE, sel);
}

void Pane::NewFolder(bool file) {
    Tab* t = T();
    if (!IsFsPath(t->path)) { MessageBeep(MB_ICONWARNING); return; }
    wstring name = UniqueName(t->path, file ? L"New Text Document.txt" : L"New folder", false);
    wstring full = Join(t->path, name);
    bool ok;
    if (file) {
        HANDLE h = CreateFileW(full.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        ok = h != INVALID_HANDLE_VALUE;
        if (ok) CloseHandle(h);
    } else {
        ok = CreateDirectoryW(full.c_str(), nullptr);
    }
    if (!ok) { MessageBoxW(g_main, ErrText(GetLastError()).c_str(), APP_NAME, MB_ICONERROR); return; }
    Item it;
    it.name = name;
    it.attr = file ? FILE_ATTRIBUTE_NORMAL : FILE_ATTRIBUTE_DIRECTORY;
    GetSystemTimeAsFileTime(&it.mt);
    it.ct = it.at = it.mt;
    t->items.push_back(it);
    SortItems(t->items, t->sortCol, t->asc);
    int idx = 0;
    for (int i = 0; i < (int)t->items.size(); i++) if (t->items[i].name == name) { idx = i; break; }
    ListView_SetItemCountEx(list, t->items.size(), LVSICF_NOSCROLL);
    ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(list, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list, idx, FALSE);
    SetFocus(list);
    ListView_EditLabel(list, idx);
}

void Pane::SelectAll() { ListView_SetItemState(list, -1, LVIS_SELECTED, LVIS_SELECTED); }

void Pane::ContextMenu(POINT pt, bool keyboard) {
    Tab* t = T();
    int hit = -1;
    if (keyboard) {
        hit = ListView_GetNextItem(list, -1, LVNI_FOCUSED);
        if (hit >= 0 && !ListView_GetItemState(list, hit, LVIS_SELECTED)) hit = -1;
        RECT r{LVIR_LABEL};
        if (hit >= 0 && ListView_GetItemRect(list, hit, &r, LVIR_LABEL)) pt = {r.left, r.bottom};
        else pt = {S(20), S(20)};
        ClientToScreen(list, &pt);
    } else {
        LVHITTESTINFO hi{};
        hi.pt = pt;
        ScreenToClient(list, &hi.pt);
        hit = ListView_HitTest(list, &hi);
        if (hit >= 0 && !ListView_GetItemState(list, hit, LVIS_SELECTED)) {
            ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
            ListView_SetItemState(list, hit, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
        if (hit < 0) ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
    }
    if (Mode() == 2) {  // Recycle Bin
        enum { B_RESTORE = 1, B_DELETE, B_EMPTY, B_REFRESH };
        bool any = ListView_GetSelectedCount(list) > 0;
        HMENU m = CreatePopupMenu();
        if (any) {
            AppendMenuW(m, MF_STRING, B_RESTORE, L"Restore");
            SetMenuDefaultItem(m, B_RESTORE, FALSE);
            AppendMenuW(m, MF_STRING, B_DELETE, L"Delete permanently\tDel");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        }
        AppendMenuW(m, MF_STRING | (t->items.empty() ? MF_GRAYED : 0), B_EMPTY, L"Empty Recycle Bin");
        AppendMenuW(m, MF_STRING, B_REFRESH, L"Refresh\tF5");
        int r = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(m);
        if (r == B_RESTORE) BinRestore();
        if (r == B_DELETE) BinDelete(false);
        if (r == B_EMPTY) BinDelete(true);
        if (r == B_REFRESH) Refresh();
        return;
    }
    bool drives = t->path.empty();
    vector<wstring> sel = Selected();
    bool oneDir = sel.size() == 1 && hit >= 0 && t->items[hit].isDir();
    bool canPaste = IsClipboardFormatAvailable(CF_HDROP);
    enum { M_OPEN = 1, M_OPENTAB, M_CUT, M_COPY, M_LINK, M_PATH, M_PASTE, M_RENAME, M_DELETE, M_NEWDIR, M_NEWFILE,
           M_FAV, M_TERM, M_PROPS, M_REFRESH, M_HIDDEN, M_ZIP, M_7Z, M_7ZPW, M_EJECT, M_XHERE, M_XFOLDER };
    bool archives = !sel.empty() && !drives;
    for (auto& s : sel) if (!IsArchive(s)) { archives = false; break; }
    bool ejectable = drives && oneDir && IsExternalDrive(sel[0]);
    wstring sevenZip = drives || sel.empty() ? L"" : SevenZipExe();
    HMENU m = CreatePopupMenu();
    if (!sel.empty()) {
        AppendMenuW(m, MF_STRING, M_OPEN, L"Open\tEnter");
        SetMenuDefaultItem(m, M_OPEN, FALSE);
        if (oneDir) AppendMenuW(m, MF_STRING, M_OPENTAB, L"Open in new tab\tCtrl+Enter");
        if (ejectable) AppendMenuW(m, MF_STRING, M_EJECT, L"Eject");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        if (!drives) AppendMenuW(m, MF_STRING, M_CUT, L"Cut\tCtrl+X");
        AppendMenuW(m, MF_STRING, M_COPY, L"Copy\tCtrl+C");
        AppendMenuW(m, MF_STRING, M_LINK, L"Copy as link\tCtrl+Shift+C");
        AppendMenuW(m, MF_STRING, M_PATH, L"Copy path");
        if (oneDir) AppendMenuW(m, MF_STRING | (canPaste ? 0 : MF_GRAYED), M_PASTE, L"Paste into folder");
        if (!drives) {
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING | (sel.size() == 1 ? 0 : MF_GRAYED), M_RENAME, L"Rename\tF2");
            AppendMenuW(m, MF_STRING, M_DELETE, L"Delete\tDel");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            if (archives) {
                AppendMenuW(m, MF_STRING, M_XHERE, L"Extract here");
                wstring to = sel.size() == 1 ? L"Extract to \"" + FileName(sel[0]).substr(0, FileName(sel[0]).find_last_of(L'.')) + L"\\\""
                                             : L"Extract each to its own folder";
                AppendMenuW(m, MF_STRING, M_XFOLDER, to.c_str());
                AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            }
            AppendMenuW(m, MF_STRING, M_ZIP, L"Compress to ZIP file");
            UINT no7 = sevenZip.empty() ? MF_GRAYED : 0;
            AppendMenuW(m, MF_STRING | no7, M_7Z, sevenZip.empty() ? L"Add to .7z archive (7-Zip not installed)" : L"Add to .7z archive (7-Zip)");
            AppendMenuW(m, MF_STRING | no7, M_7ZPW, L"Add to .7z with password…");
        }
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        if (oneDir) AppendMenuW(m, MF_STRING, M_FAV, IsFavourite(sel[0]) ? L"Remove from Favourites" : L"Add to Favourites");
        AppendMenuW(m, MF_STRING, M_TERM, L"Open terminal here");
        AppendMenuW(m, MF_STRING, M_PROPS, L"Properties\tAlt+Enter");
    } else {
        if (!drives) {
            AppendMenuW(m, MF_STRING | (canPaste ? 0 : MF_GRAYED), M_PASTE, L"Paste\tCtrl+V");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, M_NEWDIR, L"New folder\tCtrl+Shift+N");
            AppendMenuW(m, MF_STRING, M_NEWFILE, L"New text file\tCtrl+Alt+N");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        }
        AppendMenuW(m, MF_STRING, M_REFRESH, L"Refresh\tF5");
        if (!drives) {
            AppendMenuW(m, MF_STRING, M_PATH, L"Copy path");
            AppendMenuW(m, MF_STRING, M_FAV, IsFavourite(t->path) ? L"Remove from Favourites\tCtrl+D" : L"Add to Favourites\tCtrl+D");
            AppendMenuW(m, MF_STRING, M_TERM, L"Open terminal here");
        }
        AppendMenuW(m, MF_STRING | (g_showHidden ? MF_CHECKED : 0), M_HIDDEN, L"Show hidden files\tCtrl+H");
        if (!drives) AppendMenuW(m, MF_STRING, M_PROPS, L"Properties");
    }
    int r = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(m);
    wstring target = oneDir ? sel[0] : t->path;
    switch (r) {
    case M_OPEN: OpenItems(false); break;
    case M_OPENTAB: NewTab(sel[0]); break;
    case M_CUT: Copy(true, false); break;
    case M_COPY: Copy(false, false); break;
    case M_LINK: Copy(false, true); break;
    case M_PATH: CopyPath(); break;
    case M_PASTE: Paste(target); break;
    case M_RENAME: ListView_EditLabel(list, hit >= 0 ? hit : ListView_GetNextItem(list, -1, LVNI_SELECTED)); break;
    case M_DELETE: Delete(GetKeyState(VK_SHIFT) < 0); break;
    case M_NEWDIR: NewFolder(false); break;
    case M_NEWFILE: NewFolder(true); break;
    case M_FAV: ToggleFavourite(target); UpdateStar(); break;
    case M_TERM: OpenTerminal(oneDir || sel.empty() ? target : t->path); break;
    case M_PROPS: ShowProperties(sel.empty() ? t->path : sel[0]); break;
    case M_REFRESH: Refresh(); break;
    case M_HIDDEN: ToggleHidden(); break;
    case M_ZIP: FileOp(OP_ZIP, sel, ArchiveName(sel, L".zip")); break;
    case M_7Z: SevenZipAdd(sel, ArchiveName(sel, L".7z")); break;
    case M_7ZPW: {
        wstring pw;
        bool names = true;
        if (PasswordBox(L"Add to .7z with password", pw, names)) SevenZipAdd(sel, ArchiveName(sel, L".7z"), pw, names);
        break;
    }
    case M_XHERE: ExtractArchives(sel, false); break;
    case M_XFOLDER: ExtractArchives(sel, true); break;
    case M_EJECT: EjectDrive(sel[0]); break;
    }
}

// ---------- keyboard ----------
bool Pane::HandleKey(const MSG& msg) {
    if (msg.message != WM_KEYDOWN && msg.message != WM_SYSKEYDOWN) return false;
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0, alt = GetKeyState(VK_MENU) < 0;
    switch (msg.wParam) {
    case VK_RETURN:
        if (alt) { auto s = Selected(); ShowProperties(s.empty() ? Path() : s[0]); }
        else OpenItems(ctrl);
        return true;
    case VK_BACK: GoBack(); return true;
    case VK_LEFT: if (alt) { GoBack(); return true; } break;
    case VK_RIGHT: if (alt) { GoForward(); return true; } break;
    case VK_UP: if (alt) { GoUp(); return true; } break;
    case VK_DELETE: Delete(shift); return true;
    case VK_F2: {
        int i = ListView_GetNextItem(list, -1, LVNI_FOCUSED);
        if (i >= 0 && !Path().empty()) ListView_EditLabel(list, i);
        return true;
    }
    case VK_F5: Refresh(); return true;
    case 'C': if (ctrl && !alt) { Copy(false, shift); return true; } break;
    case 'X': if (ctrl && !alt) { Copy(true, false); return true; } break;
    case 'V': if (ctrl && !alt) { Paste(Path()); return true; } break;
    case 'A': if (ctrl) { SelectAll(); return true; } break;
    case 'N': if (ctrl && (shift || alt)) { NewFolder(alt); return true; } break;
    case 'D': if (ctrl && IsFsPath(Path())) { ToggleFavourite(Path()); UpdateStar(); return true; } break;
    }
    return false;
}

// ---------- drag target ----------
bool Pane::DropTarget(POINT screen, HWND under, wstring& dir) {
    Tab* t = T();
    if (under == list || IsChild(list, under)) {
        LVHITTESTINFO hi{};
        hi.pt = screen;
        ScreenToClient(list, &hi.pt);
        int i = ListView_HitTest(list, &hi);
        int hiIdx = -1;
        if (i >= 0 && i < (int)t->items.size() && t->items[i].isDir() && Mode() != 2 && !ListView_GetItemState(list, i, LVIS_SELECTED)) {
            dir = ItemPath(i);
            hiIdx = i;
        } else {
            dir = t->path;
        }
        if (hiIdx != dropHi_) {
            int old = dropHi_;
            dropHi_ = hiIdx;
            if (old >= 0) ListView_RedrawItems(list, old, old);
            if (hiIdx >= 0) ListView_RedrawItems(list, hiIdx, hiIdx);
        }
        hoverTab_ = -1;
        // auto-scroll near the edges
        RECT rc; GetClientRect(list, &rc);
        if (hi.pt.y < S(30)) ListView_Scroll(list, 0, -S(20));
        else if (hi.pt.y > rc.bottom - S(20)) ListView_Scroll(list, 0, S(20));
        return !dir.empty();
    }
    ClearDropHi();
    if (under == tabs) {
        POINT tp = screen;
        ScreenToClient(tabs, &tp);
        int i = HitTab(tp);
        if (i < 0) { hoverTab_ = -1; return false; }
        if (i != hoverTab_) { hoverTab_ = i; hoverTick_ = GetTickCount(); }
        else if (i != cur_ && GetTickCount() - hoverTick_ > 500) SwitchTab(i);
        dir = tabs_[i]->path;
        return !dir.empty();
    }
    hoverTab_ = -1;
    if (under == addr.hwnd) return addr.DropTarget(screen, dir);
    addr.ClearDropHi();
    return false;
}

void Pane::ClearDropHi() {
    if (dropHi_ >= 0) { int o = dropHi_; dropHi_ = -1; ListView_RedrawItems(list, o, o); }
}

// ---------- notifications ----------
void Pane::OnNotify(NMHDR* nm, LRESULT& res) {
    Tab* t = T();
    if (nm->hwndFrom != list) return;
    switch (nm->code) {
    case NM_CUSTOMDRAW: {  // themed rows
        auto cd = (NMLVCUSTOMDRAW*)nm;
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) { res = CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT; return; }
        if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) { DrawRow(cd->nmcd.hdc, (int)cd->nmcd.dwItemSpec); res = CDRF_SKIPDEFAULT; }
        if (cd->nmcd.dwDrawStage == CDDS_POSTPAINT) {  // the visual style draws column lines in the empty area: paint them over
            RECT rc; GetClientRect(list, &rc);
            int n = ListView_GetItemCount(list);
            if (n > 0) {
                RECT last;
                if (ListView_GetItemRect(list, n - 1, &last, LVIR_BOUNDS)) rc.top = std::max(rc.top, last.bottom);
            } else if (HWND hdr = ListView_GetHeader(list)) {
                RECT hr; GetWindowRect(hdr, &hr);
                rc.top = hr.bottom - hr.top;
            }
            if (rc.top < rc.bottom) Fill(cd->nmcd.hdc, rc, g_theme->pane);
        }
        return;
    }
    case LVN_GETDISPINFOW: {
        auto d = (NMLVDISPINFOW*)nm;
        int i = d->item.iItem;
        if (i < 0 || i >= (int)t->items.size()) return;
        const Item& it = t->items[i];
        if (d->item.mask & LVIF_TEXT) {
            int c = d->item.iSubItem < (int)cols_.size() ? cols_[d->item.iSubItem] : C_NAME;
            wstring s = CellText(it, c);
            lstrcpynW(d->item.pszText, s.c_str(), d->item.cchTextMax);
        }
        if (d->item.mask & LVIF_IMAGE) d->item.iImage = 0;  // icons are custom-drawn
        if (d->item.mask & LVIF_STATE) {
            UINT st = 0;
            if ((it.attr & FILE_ATTRIBUTE_HIDDEN) || (!g_cutSet.empty() && g_cutSet.count(Lower(ItemPath(i))))) st |= LVIS_CUT;
            if (i == dropHi_) st |= LVIS_DROPHILITED;
            d->item.state |= st;
            d->item.stateMask |= LVIS_CUT | LVIS_DROPHILITED;
        }
        return;
    }
    case LVN_ODFINDITEMW: {
        auto f = (NMLVFINDITEMW*)nm;
        res = -1;
        if (!(f->lvfi.flags & (LVFI_STRING | LVFI_PARTIAL)) || !f->lvfi.psz) return;
        wstring pre = f->lvfi.psz;
        int n = (int)t->items.size();
        for (int k = 0; k < n; k++) {
            int i = (f->iStart + k) % n;
            wstring name = t->items[i].dtype ? DriveDisplay(t->items[i]) : t->items[i].name;
            if (IStarts(name, pre)) { res = i; return; }
        }
        return;
    }
    case NM_DBLCLK: OpenItems(GetKeyState(VK_CONTROL) < 0); return;
    case NM_SETFOCUS: SetActive(this); return;
    case LVN_ITEMCHANGED: case LVN_ODSTATECHANGED:
        if (!statusPending_) { statusPending_ = true; PostMessageW(hwnd, WM_APP_STATUS, 0, 0); }
        return;
    case LVN_COLUMNCLICK: {
        int c = cols_[((NMLISTVIEW*)nm)->iSubItem];
        if (t->sortCol == c) t->asc = !t->asc; else { t->sortCol = c; t->asc = true; }
        SaveView();
        SortItems(t->items, t->sortCol, t->asc);
        t->top = -1;
        ShowItems(true);
        return;
    }
    case LVN_BEGINDRAG: {
        if (Mode() == 2) return;  // recycled items leave the bin via Restore only
        vector<wstring> sel = Selected();
        if (!sel.empty()) DragBegin(this, sel);
        return;
    }
    case LVN_BEGINLABELEDITW: {
        auto d = (NMLVDISPINFOW*)nm;
        if (!IsFsPath(t->path) || d->item.iItem < 0 || d->item.iItem >= (int)t->items.size()) { res = TRUE; return; }
        editing_ = true;
        PostMessageW(hwnd, WM_APP_SELNAME, d->item.iItem, 0);
        return;
    }
    case LVN_ENDLABELEDITW: {
        auto d = (NMLVDISPINFOW*)nm;
        editing_ = false;
        res = FALSE;
        int i = d->item.iItem;
        if (d->item.pszText && i >= 0 && i < (int)t->items.size()) {
            wstring nn = d->item.pszText;
            while (!nn.empty() && (nn.back() == L' ' || nn.back() == L'.')) nn.pop_back();
            while (!nn.empty() && nn[0] == L' ') nn.erase(0, 1);
            Item& it = t->items[i];
            if (!nn.empty() && nn != it.name) {
                if (nn.find_first_of(L"\\/:*?\"<>|") != wstring::npos) {
                    MessageBoxW(g_main, L"A file name can't contain any of these characters:\n\\ / : * ? \" < > |", APP_NAME, MB_ICONWARNING);
                } else if (MoveFileW(Join(t->path, it.name).c_str(), Join(t->path, nn).c_str())) {
                    it.name = nn;
                    t->focusName = nn;
                    t->selNames = {nn};
                    res = TRUE;
                } else {
                    MessageBoxW(g_main, ErrText(GetLastError()).c_str(), APP_NAME, MB_ICONERROR);
                }
            }
        }
        if (refreshPending_) { refreshPending_ = false; PostMessageW(hwnd, WM_APP_FSCHANGE, watcher_.Token(), 0); }
        return;
    }
    }
}

void Pane::Command(PaneCmd c) {
    SetActive(this);
    switch (c) {
    case PC_NEWFOLDER: NewFolder(false); return;  // keeps focus in the rename box
    case PC_CUT: Copy(true, false); break;
    case PC_COPY: Copy(false, false); break;
    case PC_PASTE: Paste(Path()); break;
    case PC_RENAME: {
        int i = ListView_GetNextItem(list, -1, LVNI_SELECTED);
        if (i >= 0 && IsFsPath(Path())) { SetFocus(list); ListView_EditLabel(list, i); return; }
        MessageBeep(MB_ICONWARNING);
        break;
    }
    case PC_DELETE: Delete(GetKeyState(VK_SHIFT) < 0); break;
    case PC_TERMINAL: if (IsFsPath(Path())) OpenTerminal(Path()); break;
    }
    FocusList();
}

LRESULT Pane::Proc(UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE: Layout(); return 0;
    case WM_PAINT: {  // navigation row background, title row, status row
        const Theme& th = *g_theme;
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        Fill(dc, rc, th.pane);
        RECT ln = rc; ln.top = S(kTabH) + S(kNavH) - 1; ln.bottom = ln.top + 1;
        Fill(dc, ln, th.border);
        SetBkMode(dc, TRANSPARENT);
        RECT title = rc; title.left = S(16); title.top = S(kTabH) + S(kNavH); title.bottom = title.top + S(kTitleH); title.right -= S(48);
        wstring name = DisplayName(Path());
        SelectObject(dc, g_fontTitle);
        SetTextColor(dc, th.text);
        RECT tr = title;
        DrawTextW(dc, name.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | DT_CALCRECT);
        tr.bottom = title.bottom; tr.top = title.top;
        DrawTextW(dc, name.c_str(), -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT cnt = title; cnt.left = tr.right + S(10);
        SelectObject(dc, g_fontMono);
        SetTextColor(dc, th.faint);
        wstring items = std::to_wstring(T()->items.size()) + L" items";
        DrawTextW(dc, items.c_str(), -1, &cnt, DT_SINGLELINE | DT_VCENTER);
        RECT st = rc; st.top = st.bottom - S(kStatusH);
        ln = st; ln.bottom = ln.top + 1;
        Fill(dc, ln, th.border);
        st.left += S(16); st.right -= S(16);
        SetTextColor(dc, th.faint);
        DrawTextW(dc, statusL_.c_str(), -1, &st, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        DrawTextW(dc, statusR_.c_str(), -1, &st, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_DRAWITEM: DrawButton((DRAWITEMSTRUCT*)l); return TRUE;
    case WM_COMMAND:
        // only our buttons; the label-edit box also sends WM_COMMAND (EN_*) through the list
        if (HIWORD(w) != BN_CLICKED || LOWORD(w) < ID_BACK || LOWORD(w) > ID_STAR || GetParent((HWND)l) != hwnd) break;
        SetActive(this);
        switch (LOWORD(w)) {
        case ID_BACK: GoBack(); break;
        case ID_FWD: GoForward(); break;
        case ID_UP: GoUp(); break;
        case ID_REFRESH: Refresh(); break;
        case ID_STAR: if (IsFsPath(Path())) { ToggleFavourite(Path()); UpdateStar(); } break;
        }
        FocusList();
        return 0;
    case WM_NOTIFY: {
        LRESULT res = 0;
        OnNotify((NMHDR*)l, res);
        return res;
    }
    case WM_CONTEXTMENU: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        HWND hdr = ListView_GetHeader(list);
        RECT hr; GetWindowRect(hdr, &hr);
        if ((HWND)w == list || (HWND)w == hdr) {
            SetActive(this);
            if (pt.x != -1 && PtInRect(&hr, pt)) {
                if (IsFsPath(Path()) && ColumnMenu(hwnd, pt) >= 0) RebuildAllColumns();
            } else {
                ContextMenu(pt, pt.x == -1 && pt.y == -1);
            }
        }
        return 0;
    }
    case WM_APP_LISTED: {
        std::unique_ptr<ListResult> r((ListResult*)l);
        for (auto& tp : tabs_) {
            Tab* t = tp.get();
            if (t->id != r->tabId) continue;
            if (r->gen != t->gen) break;
            bool isCur = t == T();
            if (isCur && editing_) { refreshPending_ = true; break; }
            t->items = std::move(r->items);
            t->err = r->err;
            t->loading = false;
            if (t->path.empty()) DriveInfoAsync(hwnd, t->items);
            if (isCur) ShowItems(true);
            break;
        }
        return 0;
    }
    case WM_APP_DRIVEINFO: {
        std::unique_ptr<DriveInfo> d((DriveInfo*)l);
        for (auto& t : tabs_) {
            if (!t->path.empty()) continue;
            for (auto& it : t->items)
                if (IEq(it.name, d->root)) { if (!d->label.empty()) it.label = d->label; it.free = d->free; it.size = d->total; }
        }
        if (T()->path.empty()) InvalidateRect(list, nullptr, FALSE);
        return 0;
    }
    case WM_APP_FSCHANGE:
        if (w == watcher_.Token()) SetTimer(hwnd, 1, 150, nullptr);
        return 0;
    case WM_TIMER:
        KillTimer(hwnd, 1);
        if (!T()->loading) Refresh();
        return 0;
    case WM_APP_STATUS: UpdateStatus(); return 0;
    case WM_APP_SELNAME: {  // select the name without extension, like Explorer
        HWND e = ListView_GetEditControl(list);
        int i = (int)w;
        if (e && i < (int)T()->items.size() && !T()->items[i].isDir()) {
            const wstring& n = T()->items[i].name;
            size_t dot = n.find_last_of(L'.');
            if (dot != wstring::npos && dot > 0) SendMessageW(e, EM_SETSEL, 0, dot);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: SetActive(this); return 0;
    }
    return DefWindowProcW(hwnd, m, w, l);
}

LRESULT CALLBACK Pane::StaticProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto self = (Pane*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        self = (Pane*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd = h;
    }
    return self && self->list ? self->Proc(m, w, l) : DefWindowProcW(h, m, w, l);
}
