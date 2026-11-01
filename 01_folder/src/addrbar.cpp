#include "addrbar.h"
#include "pane.h"
#include "app.h"
#include "clip.h"
#include <thread>

struct Suggest { UINT req; vector<wstring> items; };

static const COLORREF kHot = RGB(229, 241, 251), kHotBorder = RGB(204, 228, 247), kDrop = RGB(204, 232, 255);

void AddrBar::Create(HWND parent, Pane* owner) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = Proc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"NxAddr";
        wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassW(&wc);
        reg = true;
    }
    owner_ = owner;
    hwnd = CreateWindowExW(0, L"NxAddr", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10, 10, parent, nullptr, g_inst, this);
}

void AddrBar::SetPath(const wstring& p) {
    path_ = p;
    err_ = false;
    if (edit_) EndEdit();
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ---------- breadcrumb ----------
void AddrBar::Layout(HDC dc) {
    vector<Seg> all;
    all.push_back({L"This PC", L""});
    if (!path_.empty()) {
        size_t pos;
        wstring root;
        if (path_.rfind(L"\\\\", 0) == 0) {  // \\server\share
            pos = path_.find(L'\\', 2);
            pos = pos == wstring::npos ? pos : path_.find(L'\\', pos + 1);
            root = path_.substr(0, pos);
            all.push_back({root, root});
        } else {
            root = path_.substr(0, 3);
            pos = 3;
            all.push_back({root.substr(0, 2), root});
        }
        wstring acc = root;
        while (pos != wstring::npos && pos < path_.size()) {
            if (path_[pos] == L'\\') { pos++; continue; }
            size_t e = path_.find(L'\\', pos);
            wstring part = path_.substr(pos, e == wstring::npos ? wstring::npos : e - pos);
            acc = Join(acc, part);
            all.push_back({part, acc});
            pos = e;
        }
    }
    RECT rc; GetClientRect(hwnd, &rc);
    int h = rc.bottom, pad = S(6), aw = S(16), dw = S(24);
    drop_ = {rc.right - dw, 0, rc.right, h};
    int avail = rc.right - dw - S(2);
    vector<int> w(all.size());
    int total = 0;
    HFONT old = (HFONT)SelectObject(dc, g_font);
    for (size_t i = 0; i < all.size(); i++) {
        SIZE sz; GetTextExtentPoint32W(dc, all[i].text.c_str(), (int)all[i].text.size(), &sz);
        w[i] = sz.cx + pad * 2;
        total += w[i] + aw;
    }
    SelectObject(dc, old);
    size_t first = 0;
    int moreW = S(22);
    while (total > avail && first + 1 < all.size()) { total -= w[first] + aw; first++; if (first == 1) total += moreW; }
    hidden_.clear();
    segs_.clear();
    int x = S(2);
    if (first) {
        for (size_t i = 0; i < first; i++) hidden_.push_back(all[i].path);
        Seg m{L"\u00AB", L""};
        m.more = true;
        m.r = {x, S(2), x + moreW, h - S(2)};
        x += moreW;
        segs_.push_back(m);
    }
    for (size_t i = first; i < all.size(); i++) {
        Seg s = all[i];
        s.r = {x, S(2), x + w[i], h - S(2)};
        x += w[i];
        s.arrow = {x, S(2), x + aw, h - S(2)};
        x += aw;
        segs_.push_back(s);
    }
}

void AddrBar::Paint(HDC dc) {
    RECT rc; GetClientRect(hwnd, &rc);
    FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
    HBRUSH frame = CreateSolidBrush(err_ ? RGB(220, 50, 50) : GetFocus() == edit_ && edit_ ? GetSysColor(COLOR_HIGHLIGHT) : RGB(200, 200, 200));
    FrameRect(dc, &rc, frame);
    DeleteObject(frame);
    if (edit_) return;
    Layout(dc);
    SetBkMode(dc, TRANSPARENT);
    HBRUSH hot = CreateSolidBrush(kHot), drop = CreateSolidBrush(kDrop);
    for (int i = 0; i < (int)segs_.size(); i++) {
        auto& s = segs_[i];
        if (i == dropHi_) FillRect(dc, &s.r, drop);
        else if (i == hot_ && hotArrow_ == 0) FillRect(dc, &s.r, hot);
        if (i == hot_ && hotArrow_ == 1) FillRect(dc, &s.arrow, hot);
        SelectObject(dc, g_font);
        SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        DrawTextW(dc, s.text.c_str(), -1, &s.r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (!s.more) {
            SelectObject(dc, g_iconFont);
            SetTextColor(dc, RGB(110, 110, 110));
            DrawTextW(dc, L"\uE76C", -1, &s.arrow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
    if (hot_ == -2) FillRect(dc, &drop_, hot);
    SelectObject(dc, g_iconFont);
    SetTextColor(dc, RGB(90, 90, 90));
    DrawTextW(dc, L"\uE70D", -1, &drop_, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    DeleteObject(hot);
    DeleteObject(drop);
}

int AddrBar::HitSeg(POINT pt, bool* arrow) {
    for (int i = 0; i < (int)segs_.size(); i++) {
        if (PtInRect(&segs_[i].r, pt)) { if (arrow) *arrow = false; return i; }
        if (!segs_[i].more && PtInRect(&segs_[i].arrow, pt)) { if (arrow) *arrow = true; return i; }
    }
    return -1;
}

void AddrBar::FolderMenu(const Seg& s) {
    vector<wstring> subs = ListSubdirs(s.path, L"", 2000);
    if (subs.empty()) return;
    HMENU m = CreatePopupMenu();
    for (size_t i = 0; i < subs.size(); i++) {
        wstring full = s.path.empty() ? subs[i] : Join(s.path, subs[i]);
        bool onPath = IStarts(path_, full) && (path_.size() == full.size() || path_[full.size()] == L'\\' || full.back() == L'\\');
        wstring text = s.path.empty() ? subs[i].substr(0, 2) : subs[i];
        AppendMenuW(m, MF_STRING, i + 1, text.c_str());
        if (onPath) SetMenuDefaultItem(m, (UINT)i + 1, FALSE);
    }
    POINT pt{s.arrow.left, s.arrow.bottom};
    ClientToScreen(hwnd, &pt);
    int r = TrackPopupMenu(m, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(m);
    if (r > 0) owner_->Navigate(s.path.empty() ? subs[r - 1] : Join(s.path, subs[r - 1]));
}

void AddrBar::Click(POINT pt) {
    bool arrow = false;
    int i = HitSeg(pt, &arrow);
    if (i >= 0) {
        Seg s = segs_[i];
        if (s.more) {  // hidden leading segments
            HMENU m = CreatePopupMenu();
            for (size_t k = 0; k < hidden_.size(); k++) AppendMenuW(m, MF_STRING, k + 1, hidden_[k].empty() ? L"This PC" : hidden_[k].c_str());
            POINT p{s.r.left, s.r.bottom}; ClientToScreen(hwnd, &p);
            int r = TrackPopupMenu(m, TPM_RETURNCMD, p.x, p.y, 0, hwnd, nullptr);
            DestroyMenu(m);
            if (r > 0) owner_->Navigate(hidden_[r - 1]);
        } else if (arrow) {
            FolderMenu(s);
        } else {
            owner_->Navigate(s.path);
        }
        return;
    }
    if (PtInRect(&drop_, pt)) {
        vector<wstring> rec = RecentPaths(20);
        if (rec.empty()) return;
        HMENU m = CreatePopupMenu();
        for (size_t k = 0; k < rec.size(); k++) AppendMenuW(m, MF_STRING, k + 1, rec[k].c_str());
        POINT p{drop_.right, drop_.bottom}; ClientToScreen(hwnd, &p);
        int r = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTALIGN, p.x, p.y, 0, hwnd, nullptr);
        DestroyMenu(m);
        if (r > 0) owner_->Navigate(rec[r - 1]);
        return;
    }
    BeginEdit();
}

// ---------- edit mode ----------
void AddrBar::BeginEdit() {
    if (edit_) { SetFocus(edit_); SendMessageW(edit_, EM_SETSEL, 0, -1); return; }
    RECT rc; GetClientRect(hwnd, &rc);
    int h = rc.bottom - S(4);
    edit_ = CreateWindowExW(0, L"EDIT", path_.empty() ? L"This PC" : path_.c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                            S(6), (rc.bottom - h) / 2 + S(1), rc.right - S(10), h, hwnd, nullptr, g_inst, nullptr);
    SendMessageW(edit_, WM_SETFONT, (WPARAM)g_font, TRUE);
    SetWindowSubclass(edit_, EditProc, 1, (DWORD_PTR)this);
    SetFocus(edit_);
    SendMessageW(edit_, EM_SETSEL, 0, -1);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void AddrBar::EndEdit() {
    HideSuggest();
    if (!edit_) return;
    HWND e = edit_;
    edit_ = nullptr;
    err_ = false;
    DestroyWindow(e);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void AddrBar::Commit() {
    wchar_t buf[32768];
    GetWindowTextW(edit_, buf, ARRAYSIZE(buf));
    wstring dir, sel;
    if (!ResolveUserPath(buf, owner_->Path(), dir, sel)) {
        err_ = true;
        HideSuggest();
        InvalidateRect(hwnd, nullptr, FALSE);
        MessageBeep(MB_ICONWARNING);
        return;
    }
    EndEdit();
    owner_->Navigate(dir, true, sel);
    owner_->FocusList();
}

void AddrBar::RequestSuggest() {
    wchar_t buf[4096];
    GetWindowTextW(edit_, buf, ARRAYSIZE(buf));
    wstring t = buf;
    std::replace(t.begin(), t.end(), L'/', L'\\');
    size_t k = t.find_last_of(L'\\');
    UINT req = ++sugReq_;
    if (k == wstring::npos && t.size() > 2) { HideSuggest(); return; }
    wstring parentTyped = k == wstring::npos ? L"" : t.substr(0, k + 1);
    wstring prefix = k == wstring::npos ? t : t.substr(k + 1);
    wchar_t ex[4096];
    wstring parent = parentTyped;
    if (ExpandEnvironmentStringsW(parentTyped.c_str(), ex, ARRAYSIZE(ex))) parent = ex;
    if (!parent.empty() && PathIsRelativeW(parent.c_str())) { HideSuggest(); return; }
    HWND h = hwnd;
    std::thread([=] {
        auto r = new Suggest{req};
        if (parent.empty()) {
            for (auto& d : ListDrives()) if (IStarts(d.name, prefix)) r->items.push_back(d.name);
        } else {
            for (auto& n : ListSubdirs(parent, prefix, 60)) r->items.push_back(parentTyped + n);
        }
        if (!PostMessageW(h, WM_APP_SUGGEST, 0, (LPARAM)r)) delete r;
    }).detach();
}

void AddrBar::ShowSuggest(const vector<wstring>& v) {
    wchar_t cur[4096];
    if (edit_) GetWindowTextW(edit_, cur, ARRAYSIZE(cur));
    if (!edit_ || v.empty() || (v.size() == 1 && IEq(v[0], cur))) { HideSuggest(); return; }
    if (!sug_) {
        sug_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, L"LISTBOX", L"",
                               WS_POPUP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                               0, 0, 10, 10, hwnd, nullptr, g_inst, nullptr);
        SendMessageW(sug_, WM_SETFONT, (WPARAM)g_font, FALSE);
        SetWindowSubclass(sug_, SugProc, 2, (DWORD_PTR)this);
    }
    SendMessageW(sug_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(sug_, LB_RESETCONTENT, 0, 0);
    for (auto& s : v) SendMessageW(sug_, LB_ADDSTRING, 0, (LPARAM)s.c_str());
    SendMessageW(sug_, WM_SETREDRAW, TRUE, 0);
    int ih = (int)SendMessageW(sug_, LB_GETITEMHEIGHT, 0, 0);
    RECT r; GetWindowRect(hwnd, &r);
    int n = std::min<int>((int)v.size(), 12);
    SetWindowPos(sug_, HWND_TOPMOST, r.left, r.bottom, r.right - r.left, n * ih + S(4), SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(sug_, nullptr, TRUE);
}

void AddrBar::HideSuggest() {
    if (sug_) { DestroyWindow(sug_); sug_ = nullptr; }
}

void AddrBar::PickSuggest(int i, bool commit) {
    if (!sug_ || i < 0) return;
    wchar_t buf[4096];
    SendMessageW(sug_, LB_GETTEXT, i, (LPARAM)buf);
    wstring s = buf;
    if (!commit && s.back() != L'\\') s += L'\\';
    SetWindowTextW(edit_, s.c_str());
    SendMessageW(edit_, EM_SETSEL, s.size(), s.size());
    if (commit) Commit();
}

LRESULT CALLBACK AddrBar::SugProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) {
    auto self = (AddrBar*)ref;
    if (m == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (m == WM_LBUTTONUP) {
        LRESULT r = SendMessageW(h, LB_ITEMFROMPOINT, 0, l);
        if (!HIWORD(r)) self->PickSuggest(LOWORD(r), true);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}

LRESULT CALLBACK AddrBar::EditProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR ref) {
    auto self = (AddrBar*)ref;
    switch (m) {
    case WM_KEYDOWN: {
        int sel = self->sug_ ? (int)SendMessageW(self->sug_, LB_GETCURSEL, 0, 0) : -1;
        int cnt = self->sug_ ? (int)SendMessageW(self->sug_, LB_GETCOUNT, 0, 0) : 0;
        switch (w) {
        case VK_RETURN:
            if (sel >= 0) self->PickSuggest(sel, true); else self->Commit();
            return 0;
        case VK_ESCAPE:
            if (self->sug_) self->HideSuggest();
            else { Pane* o = self->owner_; self->EndEdit(); o->FocusList(); }
            return 0;
        case VK_DOWN: case VK_UP:
            if (cnt) {
                sel = w == VK_DOWN ? (sel + 1) % cnt : (sel <= 0 ? cnt - 1 : sel - 1);
                SendMessageW(self->sug_, LB_SETCURSEL, sel, 0);
            }
            return 0;
        case VK_TAB:
            if (cnt) self->PickSuggest(sel >= 0 ? sel : 0, false);
            return 0;
        }
        break;
    }
    case WM_CHAR:
        if (w == VK_RETURN || w == VK_ESCAPE || w == VK_TAB) return 0;
        break;
    case WM_PASTE: {  // a copied file/folder pastes as its full path
        vector<wstring> files; bool mv;
        if (ClipGetFiles(GetParent(h), files, mv)) {
            SendMessageW(h, EM_REPLACESEL, TRUE, (LPARAM)files[0].c_str());
            return 0;
        }
        break;
    }
    case WM_KILLFOCUS:
        if ((HWND)w != self->sug_) PostMessageW(self->hwnd, WM_APP + 99, 0, 0);
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

// ---------- drop target ----------
bool AddrBar::DropTarget(POINT screen, wstring& dir) {
    POINT pt = screen;
    ScreenToClient(hwnd, &pt);
    int i = edit_ ? -1 : HitSeg(pt, nullptr);
    if (i >= 0 && (segs_[i].more || segs_[i].path.empty())) i = -1;
    if (i != dropHi_) { dropHi_ = i; InvalidateRect(hwnd, nullptr, FALSE); }
    if (i < 0) return false;
    dir = segs_[i].path;
    return true;
}

void AddrBar::ClearDropHi() {
    if (dropHi_ != -1) { dropHi_ = -1; InvalidateRect(hwnd, nullptr, FALSE); }
}

LRESULT CALLBACK AddrBar::Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto self = (AddrBar*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        self = (AddrBar*)((CREATESTRUCTW*)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
        self->hwnd = h;
    }
    if (!self) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);
        self->Paint(mem);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        if (!self->tracking_) {
            TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            self->tracking_ = true;
        }
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        bool arrow = false;
        int hot = self->HitSeg(pt, &arrow);
        int ha = arrow ? 1 : 0;
        if (hot < 0 && PtInRect(&self->drop_, pt)) hot = -2;
        if (hot != self->hot_ || ha != self->hotArrow_) { self->hot_ = hot; self->hotArrow_ = ha; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        self->tracking_ = false;
        self->hot_ = -1;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        if (!self->edit_) self->Click({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        return 0;
    case WM_COMMAND:
        if (HIWORD(w) == EN_CHANGE && (HWND)l == self->edit_) {
            if (self->err_) { self->err_ = false; InvalidateRect(h, nullptr, FALSE); }
            SetTimer(h, 1, 100, nullptr);
        }
        return 0;
    case WM_TIMER:
        KillTimer(h, 1);
        if (self->edit_) self->RequestSuggest();
        return 0;
    case WM_APP_SUGGEST: {
        std::unique_ptr<Suggest> r((Suggest*)l);
        if (r->req == self->sugReq_ && self->edit_) self->ShowSuggest(r->items);
        return 0;
    }
    case WM_APP + 99:  // edit lost focus to another control of ours (not an app switch)
        if (self->edit_ && GetFocus() && GetFocus() != self->edit_) self->EndEdit();
        else self->HideSuggest();
        return 0;
    case WM_SIZE:
        if (self->edit_) {
            RECT rc; GetClientRect(h, &rc);
            int eh = rc.bottom - S(4);
            MoveWindow(self->edit_, S(6), (rc.bottom - eh) / 2 + S(1), rc.right - S(10), eh, TRUE);
        }
        return 0;
    case WM_CTLCOLOREDIT:
        SetBkColor((HDC)w, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY:
        self->HideSuggest();
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
