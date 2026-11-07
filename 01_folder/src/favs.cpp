#include "favs.h"
#include "app.h"
#include "pane.h"
#include "theme.h"
#include <knownfolders.h>

vector<Fav> g_favs;
vector<Pin> g_pins;

namespace {

int g_nextId = 1;

// ---------- model ----------
Fav* FindIn(vector<Fav>& v, int id) {
    for (auto& f : v) {
        if (f.id == id) return &f;
        if (Fav* k = FindIn(f.kids, id)) return k;
    }
    return nullptr;
}

Fav* FindPathIn(vector<Fav>& v, const wstring& p) {
    for (auto& f : v) {
        if (!f.group() && IEq(f.path, p)) return &f;
        if (Fav* k = FindPathIn(f.kids, p)) return k;
    }
    return nullptr;
}

bool ParentIn(vector<Fav>& v, int parentId, int id, int& outParent, int& outIndex) {
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i].id == id) { outParent = parentId; outIndex = (int)i; return true; }
        if (ParentIn(v[i].kids, v[i].id, id, outParent, outIndex)) return true;
    }
    return false;
}

void Write(const vector<Fav>& v, int depth, int& n) {
    for (auto& f : v) {
        wstring val = std::to_wstring(depth) + L"\t" + f.path + L"\t" + f.name + L"\t" + (f.open ? L"1" : L"0");
        IniSet(L"Bookmarks", (L"b" + std::to_wstring(n++)).c_str(), val);
        Write(f.kids, depth + 1, n);
    }
}

vector<wstring> SplitTab(const wstring& s) {
    vector<wstring> r;
    size_t b = 0;
    for (;;) {
        size_t e = s.find(L'\t', b);
        r.push_back(s.substr(b, e == wstring::npos ? wstring::npos : e - b));
        if (e == wstring::npos) break;
        b = e + 1;
    }
    return r;
}

// ---------- manager window ----------
enum { B_NEWGROUP = 1, B_ADDCUR, B_RENAME, B_PATH, B_DELETE, B_UP, B_DOWN, B_COUNT };
HWND g_mgr, g_mgrTree, g_mgrBtn[B_COUNT];
int g_mgrDrag = 0;

int MgrId(HTREEITEM h) {
    if (!h) return 0;
    TVITEMW it{TVIF_PARAM};
    it.hItem = h;
    TreeView_GetItem(g_mgrTree, &it);
    return (int)it.lParam;
}

int MgrSel() { return MgrId(TreeView_GetSelection(g_mgrTree)); }

void MgrFillList(HTREEITEM parent, vector<Fav>& v, int select, HTREEITEM& selItem) {
    for (auto& f : v) {
        TVINSERTSTRUCTW is{};
        is.hParent = parent;
        is.hInsertAfter = TVI_LAST;
        is.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
        wstring label = f.Label();
        is.item.pszText = (LPWSTR)label.c_str();
        is.item.lParam = f.id;
        is.item.cChildren = f.group() && !f.kids.empty() ? 1 : 0;
        HTREEITEM h = TreeView_InsertItem(g_mgrTree, &is);
        if (f.id == select) selItem = h;
        MgrFillList(h, f.kids, select, selItem);
        if (f.group() && f.open) TreeView_Expand(g_mgrTree, h, TVE_EXPAND);
    }
}

void MgrFill(int select) {
    if (!g_mgr) return;
    SendMessageW(g_mgrTree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(g_mgrTree);
    HTREEITEM sel = nullptr;
    MgrFillList(TVI_ROOT, g_favs, select, sel);
    if (sel) { TreeView_SelectItem(g_mgrTree, sel); TreeView_EnsureVisible(g_mgrTree, sel); }
    SendMessageW(g_mgrTree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_mgrTree, nullptr, TRUE);
    InvalidateRect(g_mgr, nullptr, FALSE);
}

// where new things go: into the selected group, or next to the selected bookmark
int TargetGroup() {
    int id = MgrSel();
    Fav* f = FavFind(id);
    if (!f) return 0;
    if (f->group()) return f->id;
    int idx;
    int p = FavParentOf(id, &idx);
    return p < 0 ? 0 : p;
}

void MgrCommand(int cmd) {
    int id = MgrSel();
    Fav* f = FavFind(id);
    switch (cmd) {
    case B_NEWGROUP: {
        wstring name = L"New group";
        if (!InputBox(L"New group", L"Name of the group:", name) || name.empty()) return;
        int g = FavAddGroup(name, TargetGroup());
        FavsChanged();
        MgrFill(g);
        break;
    }
    case B_ADDCUR: {
        wstring p = ActivePane()->InSearch() ? SearchRoot(ActivePane()->Path()) : ActivePane()->Path();
        if (!IsFsPath(p)) { MessageBeep(MB_ICONWARNING); return; }
        int n = FavAdd(p, TargetGroup());
        FavsChanged();
        MgrFill(n);
        break;
    }
    case B_RENAME:
        if (f) { SetFocus(g_mgrTree); TreeView_EditLabel(g_mgrTree, TreeView_GetSelection(g_mgrTree)); }
        break;
    case B_PATH: {
        if (!f || f->group()) { MessageBeep(MB_ICONWARNING); return; }
        wstring p = f->path;
        while (InputBox(L"Edit path", L"Folder:", p)) {
            wstring dir, sel;
            if (ResolveUserPath(p, L"", dir, sel) && IsFsPath(dir)) {
                FavFind(id)->path = dir;
                FavsChanged();
                MgrFill(id);
                break;
            }
            MessageBoxW(g_mgr, L"This folder does not exist.", L"Favourites", MB_ICONWARNING);
        }
        break;
    }
    case B_DELETE: {
        if (!f) return;
        if (f->group() && !f->kids.empty()) {
            wstring q = L"Delete the group \"" + f->Label() + L"\" and everything in it?";
            if (MessageBoxW(g_mgr, q.c_str(), L"Favourites", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
        }
        HTREEITEM h = TreeView_GetSelection(g_mgrTree);
        HTREEITEM next = TreeView_GetNextSibling(g_mgrTree, h);
        if (!next) next = TreeView_GetPrevSibling(g_mgrTree, h);
        if (!next) next = TreeView_GetParent(g_mgrTree, h);
        int nextId = MgrId(next);
        FavRemove(id);
        FavsChanged();
        MgrFill(nextId);
        break;
    }
    case B_UP: case B_DOWN: {
        if (!f) return;
        int idx;
        int p = FavParentOf(id, &idx);
        vector<Fav>* list = FavListOf(p);
        int j = idx + (cmd == B_UP ? -1 : 1);
        if (!list || j < 0 || j >= (int)list->size()) return;
        FavMove(id, (*list)[j].id, cmd == B_UP ? -1 : 1);
        FavsChanged();
        MgrFill(id);
        break;
    }
    }
}

void MgrLayout() {
    RECT rc; GetClientRect(g_mgr, &rc);
    int x = S(12), y = S(12), h = S(30);
    const int widths[B_COUNT] = {0, 104, 138, 86, 88, 76, 34, 34};
    for (int i = 1; i < B_COUNT; i++) {
        MoveWindow(g_mgrBtn[i], x, y, S(widths[i]), h, TRUE);
        x += S(widths[i]) + S(6);
    }
    int top = y + h + S(12), bottom = rc.bottom - S(40);
    MoveWindow(g_mgrTree, S(12), top, rc.right - S(24), std::max(S(40), bottom - top), TRUE);
}

void MgrDrawButton(DRAWITEMSTRUCT* d) {
    const Theme& t = *g_theme;
    bool primary = d->CtlID == B_NEWGROUP;
    bool down = d->itemState & ODS_SELECTED, dis = d->itemState & ODS_DISABLED;
    Fill(d->hDC, d->rcItem, t.pane);
    COLORREF bg = primary ? t.accent : down ? t.hot : t.chip;
    RoundFill(d->hDC, d->rcItem, S(6), bg, primary ? bg : t.border);
    static const wchar_t* glyphs[B_COUNT] = {nullptr, L"", L"", L"", L"", L"", L"", L""};
    static const wchar_t* text[B_COUNT] = {nullptr, L"New group", L"Add current folder", L"Rename", L"Edit path", L"Delete", L"", L""};
    COLORREF c = primary ? t.accentText : dis ? t.faint : t.text;
    RECT r = d->rcItem;
    if (!*text[d->CtlID]) { Glyph(d->hDC, glyphs[d->CtlID], r, c, g_iconFontSmall); return; }
    RECT g = r; g.left += S(8); g.right = g.left + S(18);
    Glyph(d->hDC, glyphs[d->CtlID], g, c, g_iconFontSmall);
    r.left = g.right + S(4);
    SelectObject(d->hDC, primary ? g_fontBold : g_font);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC, c);
    DrawTextW(d->hDC, text[d->CtlID], -1, &r, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

LRESULT CALLBACK MgrTreeProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_MOUSEMOVE:
        if (g_mgrDrag) {
            int target, where;
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            SetCursor(LoadCursor(nullptr, FavDropAt(h, pt, MgrId, g_mgrDrag, &target, &where) ? IDC_ARROW : IDC_NO));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_mgrDrag) {
            int id = g_mgrDrag, target, where;
            g_mgrDrag = 0;
            ReleaseCapture();
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            bool ok = FavDropAt(h, pt, MgrId, id, &target, &where);
            FavDropAt(h, {-1, -1}, MgrId, 0, nullptr, nullptr);  // clears the feedback
            if (ok && FavMove(id, target, where)) { FavsChanged(); MgrFill(id); }
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (g_mgrDrag) { g_mgrDrag = 0; FavDropAt(h, {-1, -1}, MgrId, 0, nullptr, nullptr); }
        break;
    case WM_KEYDOWN:
        if (w == VK_DELETE) { MgrCommand(B_DELETE); return 0; }
        if (w == VK_F2) { MgrCommand(B_RENAME); return 0; }
        if (w == VK_RETURN) {
            Fav* f = FavFind(MgrSel());
            if (f && !f->group()) ActivePane()->Navigate(f->path);
            return 0;
        }
        break;
    case WM_CTLCOLOREDIT:  // rename box
        SetTextColor((HDC)w, g_theme->text);
        SetBkColor((HDC)w, g_theme->input);
        return (LRESULT)ThemeBrush(g_theme->input);
    }
    return DefSubclassProc(h, m, w, l);
}

void MgrApplyTheme() {
    const Theme& t = *g_theme;
    ThemeTitleBar(g_mgr);
    ThemeWindow(g_mgrTree);
    TreeView_SetBkColor(g_mgrTree, t.pane);
    TreeView_SetTextColor(g_mgrTree, t.text);
    TreeView_SetLineColor(g_mgrTree, t.border);
    TreeView_SetInsertMarkColor(g_mgrTree, t.accent);
    TreeView_SetItemHeight(g_mgrTree, S(30));
    TreeIconSlot(g_mgrTree);
    SendMessageW(g_mgrTree, WM_SETFONT, (WPARAM)g_font, FALSE);
    InvalidateRect(g_mgr, nullptr, TRUE);
}

LRESULT CALLBACK MgrProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_mgr = h;
        g_mgrTree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_LINESATROOT |
                                    TVS_EDITLABELS | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT, 0, 0, 10, 10, h, (HMENU)100, g_inst, nullptr);
        TreeView_SetExtendedStyle(g_mgrTree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
        SetWindowSubclass(g_mgrTree, MgrTreeProc, 1, 0);
        ThemedScrollbars(g_mgrTree, 0);
        for (int i = 1; i < B_COUNT; i++)
            g_mgrBtn[i] = CreateWindowW(L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 10, 10, h, (HMENU)(INT_PTR)i, g_inst, nullptr);
        MgrApplyTheme();
        MgrFill(0);
        return 0;
    }
    case WM_SIZE: MgrLayout(); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        const Theme& t = *g_theme;
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        Fill(dc, rc, t.pane);
        RECT st = rc; st.top = st.bottom - S(40); st.left += S(14); st.right -= S(14);
        Fav* f = FavFind(MgrSel());
        wstring s = !f ? L"Drag items to reorder them or to move them into a group.  Double-click opens a folder."
                       : f->group() ? L"Group · " + std::to_wstring(f->kids.size()) + L" items" : f->path;
        SelectObject(dc, f && !f->group() ? g_fontMono : g_fontSmall);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, t.dim);
        DrawTextW(dc, s.c_str(), -1, &st, DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS | DT_NOPREFIX);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DRAWITEM: MgrDrawButton((DRAWITEMSTRUCT*)l); return TRUE;
    case WM_COMMAND:
        if (HIWORD(w) == BN_CLICKED && LOWORD(w) >= 1 && LOWORD(w) < B_COUNT) MgrCommand(LOWORD(w));
        return 0;
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)l;
        if (nm->hwndFrom != g_mgrTree) break;
        switch (nm->code) {
        case TVN_SELCHANGEDW: {
            Fav* f = FavFind(MgrSel());
            EnableWindow(g_mgrBtn[B_PATH], f && !f->group());
            for (int b : {B_RENAME, B_DELETE, B_UP, B_DOWN}) EnableWindow(g_mgrBtn[b], f != nullptr);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case TVN_BEGINDRAGW:
            g_mgrDrag = (int)((NMTREEVIEWW*)l)->itemNew.lParam;
            SetCapture(g_mgrTree);
            return 0;
        case TVN_ITEMEXPANDEDW: {
            auto tv = (NMTREEVIEWW*)l;
            if (Fav* f = FavFind((int)tv->itemNew.lParam)) f->open = (tv->action & TVE_EXPAND) != 0;
            return 0;
        }
        case NM_DBLCLK: {
            Fav* f = FavFind(MgrSel());
            if (f && !f->group()) { ActivePane()->Navigate(f->path); return TRUE; }
            return 0;
        }
        case TVN_ENDLABELEDITW: {
            auto d = (NMTVDISPINFOW*)l;
            Fav* f = FavFind((int)d->item.lParam);
            if (!d->item.pszText || !f) return FALSE;
            wstring n = d->item.pszText;
            if (f->group() && n.empty()) return FALSE;      // a group needs a name
            f->name = !f->group() && IEq(n, DisplayName(f->path)) ? L"" : n;  // "" = follow the folder name
            FavsChanged();
            MgrFill(f->id);
            return FALSE;
        }
        case NM_CUSTOMDRAW: {
            auto cd = (NMTVCUSTOMDRAW*)l;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) return CDRF_NOTIFYPOSTPAINT;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT) {
                Fav* f = FavFind((int)cd->nmcd.lItemlParam);
                if (f) TreeGlyph(cd->nmcd.hdc, g_mgrTree, (HTREEITEM)cd->nmcd.dwItemSpec, f->group() ? L"" : L"",
                                 f->group() ? g_theme->folder : g_theme->accent);
            }
            return CDRF_DODEFAULT;
        }
        }
        break;
    }
    case WM_GETMINMAXINFO: ((MINMAXINFO*)l)->ptMinTrackSize = {S(560), S(320)}; return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: g_mgr = nullptr; g_mgrTree = nullptr; return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

}  // namespace

// ---------- model API ----------
void FavLoad() {
    g_favs.clear();
    if (IniGet(L"Bookmarks", L"v").empty()) {  // first run of this version: take over the old flat list
        for (int i = 0;; i++) {
            wstring v = IniGet(L"Favourites", (L"f" + std::to_wstring(i)).c_str());
            if (v.empty()) break;
            size_t bar = v.find(L'|');
            Fav f;
            f.id = g_nextId++;
            f.path = v.substr(0, bar);
            f.name = bar == wstring::npos ? L"" : v.substr(bar + 1);
            if (!f.path.empty()) g_favs.push_back(f);
        }
        FavSave();
        return;
    }
    vector<vector<Fav>*> stack{&g_favs};
    for (int i = 0;; i++) {
        wstring v = IniGet(L"Bookmarks", (L"b" + std::to_wstring(i)).c_str());
        if (v.empty()) break;
        vector<wstring> p = SplitTab(v);
        if (p.size() < 3) continue;
        size_t depth = std::min<size_t>(_wtoi(p[0].c_str()), stack.size() - 1);
        stack.resize(depth + 1);
        Fav f;
        f.id = g_nextId++;
        f.path = p[1];
        f.name = p[2];
        f.open = p.size() < 4 || p[3] != L"0";
        if (f.group() && f.name.empty()) f.name = L"Group";
        stack.back()->push_back(f);
        if (f.group()) stack.push_back(&stack.back()->back().kids);
    }
}

void FavSave() {
    IniClearSection(L"Bookmarks");
    IniSet(L"Bookmarks", L"v", L"2");
    int n = 0;
    Write(g_favs, 0, n);
    IniClearSection(L"Favourites");  // the old format
}

Fav* FavFind(int id) { return id ? FindIn(g_favs, id) : nullptr; }
Fav* FavFindPath(const wstring& path) { return path.empty() ? nullptr : FindPathIn(g_favs, path); }

vector<Fav>* FavListOf(int groupId) {
    if (!groupId) return &g_favs;
    Fav* g = FavFind(groupId);
    return g && g->group() ? &g->kids : nullptr;
}

int FavParentOf(int id, int* index) {
    int p = -1, i = -1;
    if (!ParentIn(g_favs, 0, id, p, i)) return -1;
    if (index) *index = i;
    return p;
}

bool FavIsInside(int id, int ancestor) {
    if (id == ancestor) return true;
    Fav* a = FavFind(ancestor);
    return a && FindIn(a->kids, id) != nullptr;
}

int FavAdd(const wstring& path, int groupId, const wstring& name) {
    vector<Fav>* list = FavListOf(groupId);
    if (!list) list = &g_favs;
    Fav f;
    f.id = g_nextId++;
    f.path = path;
    f.name = name;
    list->push_back(f);
    return f.id;
}

int FavAddGroup(const wstring& name, int groupId) {
    vector<Fav>* list = FavListOf(groupId);
    if (!list) list = &g_favs;
    Fav f;
    f.id = g_nextId++;
    f.name = name;
    list->push_back(f);
    return f.id;
}

bool FavRemove(int id) {
    int idx;
    int p = FavParentOf(id, &idx);
    if (p < 0) return false;
    vector<Fav>* list = FavListOf(p);
    list->erase(list->begin() + idx);
    return true;
}

bool FavMove(int id, int target, int where) {
    if (!id || id == target) return false;
    if (target && FavIsInside(target, id)) return false;  // not into itself
    int idx;
    int p = FavParentOf(id, &idx);
    if (p < 0) return false;
    Fav moving = (*FavListOf(p))[idx];
    FavListOf(p)->erase(FavListOf(p)->begin() + idx);
    if (where == 0) {  // into a group (or the end of the top level)
        vector<Fav>* list = FavListOf(target);
        if (!list) list = &g_favs;
        list->push_back(moving);
        if (Fav* g = FavFind(target)) g->open = true;
        return true;
    }
    int tidx;
    int tp = FavParentOf(target, &tidx);
    if (tp < 0) { g_favs.push_back(moving); return true; }
    vector<Fav>* list = FavListOf(tp);
    list->insert(list->begin() + tidx + (where > 0 ? 1 : 0), moving);
    return true;
}

void FavsChanged() {
    FavSave();
    SidebarFavsChanged();
    for (int i = 0; i < 3; i++) if (g_panes[i]) g_panes[i]->UpdateStar();
    if (g_mgr) MgrFill(MgrSel());
}

void FavManager() {
    if (g_mgr) { ShowWindow(g_mgr, SW_RESTORE); SetForegroundWindow(g_mgr); return; }
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = MgrProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(g_inst, MAKEINTRESOURCEW(1));
        wc.lpszClassName = L"NxFavManager";
        RegisterClassW(&wc);
        reg = true;
    }
    RECT r; GetWindowRect(g_main, &r);
    int w = S(680), h = S(560);
    CreateWindowExW(0, L"NxFavManager", L"Manage favourites", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                    r.left + (r.right - r.left - w) / 2, r.top + (r.bottom - r.top - h) / 2, w, h, g_main, nullptr, g_inst, nullptr);
}

void FavManagerTheme() { if (g_mgr) MgrApplyTheme(); }

// ---------- tree helpers ----------
void TreeIconSlot(HWND tree) {
    HIMAGELIST il = ImageList_Create(S(18), S(18), ILC_COLOR32, 1, 0);
    HIMAGELIST old = TreeView_SetImageList(tree, il, TVSIL_NORMAL);
    if (old && old != SysImageList()) ImageList_Destroy(old);
}

void TreeGlyph(HDC dc, HWND tree, HTREEITEM h, const wchar_t* glyph, COLORREF c) {
    RECT r;
    if (!TreeView_GetItemRect(tree, h, &r, TRUE)) return;
    RECT ic{r.left - S(18) - S(3), r.top, r.left - S(3), r.bottom};
    Glyph(dc, glyph, ic, c);
}

bool FavDropAt(HWND tree, POINT pt, int (*idOf)(HTREEITEM), int dragging, int* target, int* where) {
    TreeView_SetInsertMark(tree, nullptr, FALSE);
    TreeView_SelectDropTarget(tree, nullptr);
    if (!dragging || pt.x < 0) return false;
    TVHITTESTINFO hi{};
    hi.pt = pt;
    HTREEITEM h = TreeView_HitTest(tree, &hi);
    int id = h ? idOf(h) : 0;
    Fav* f = FavFind(id);
    if (!f) return false;
    RECT r;
    TreeView_GetItemRect(tree, h, &r, FALSE);
    int y = pt.y - r.top, hgt = r.bottom - r.top;
    int w = f->group() ? (y < hgt / 4 ? -1 : y > hgt * 3 / 4 ? 1 : 0) : (y < hgt / 2 ? -1 : 1);
    if (FavIsInside(id, dragging)) return false;
    if (w == 0) TreeView_SelectDropTarget(tree, h);
    else TreeView_SetInsertMark(tree, h, w > 0);
    if (target) *target = id;
    if (where) *where = w;
    return true;
}

// ---------- Quick access pins ----------
void PinsLoad() {
    g_pins.clear();
    if (IniGet(L"QuickAccess", L"v").empty()) {  // default: the usual system folders and the Recycle Bin
        const KNOWNFOLDERID* ids[] = {&FOLDERID_Desktop, &FOLDERID_Downloads, &FOLDERID_Documents, &FOLDERID_Pictures, &FOLDERID_Videos, &FOLDERID_Music};
        for (auto id : ids) {
            PWSTR p = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(*id, 0, nullptr, &p))) g_pins.push_back({p, L""});
            CoTaskMemFree(p);
        }
        g_pins.push_back({BIN_PATH, L""});
        PinsSave();
        return;
    }
    for (int i = 0;; i++) {
        wstring v = IniGet(L"QuickAccess", (L"q" + std::to_wstring(i)).c_str());
        if (v.empty()) break;
        size_t bar = v.find(L'|');
        g_pins.push_back({v.substr(0, bar), bar == wstring::npos ? L"" : v.substr(bar + 1)});
    }
}

void PinsSave() {
    IniClearSection(L"QuickAccess");
    IniSet(L"QuickAccess", L"v", L"1");
    for (size_t i = 0; i < g_pins.size(); i++)
        IniSet(L"QuickAccess", (L"q" + std::to_wstring(i)).c_str(), g_pins[i].path + L"|" + g_pins[i].name);
}

bool IsPinned(const wstring& path) {
    for (auto& p : g_pins) if (IEq(p.path, path)) return true;
    return false;
}

void TogglePin(const wstring& path) {
    if (path.empty() || IsSearch(path)) return;
    for (size_t i = 0; i < g_pins.size(); i++)
        if (IEq(g_pins[i].path, path)) { g_pins.erase(g_pins.begin() + i); PinsSave(); SidebarFavsChanged(); return; }
    g_pins.push_back({path, L""});
    PinsSave();
    SidebarFavsChanged();
}
