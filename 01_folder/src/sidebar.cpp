// Sidebar: Favourites (pinned), Quick access (known + frequently used folders), Drives.
#include "app.h"
#include "pane.h"
#include <knownfolders.h>
#include <cmath>
#include <ctime>

namespace {

enum Kind { K_GFAV, K_GQUICK, K_GDRIVES, K_FAV, K_KNOWN, K_RECENT, K_DRIVE };
struct Node { Kind kind; wstring path; int fav = -1; };
struct Fav { wstring path, name; };
struct Visit { wstring path; double count = 0; long long time = 0; };

HWND g_tree = nullptr;
vector<Node> g_nodes;
vector<Fav> g_favs;
vector<std::pair<wstring, wstring>> g_known;          // name, path
std::unordered_map<wstring, Visit> g_visits;           // key: lower-case path
std::unordered_map<wstring, wstring> g_driveLabels;    // root -> label
std::unordered_set<wstring> g_missing;
vector<wstring> g_lastTop;
bool g_expanded[3] = {true, true, true};
int g_dragFav = -1;
enum { WM_SB_REBUILD = WM_APP + 70 };

void SaveFavs() {
    IniClearSection(L"Favourites");
    for (size_t i = 0; i < g_favs.size(); i++)
        IniSet(L"Favourites", (L"f" + std::to_wstring(i)).c_str(), g_favs[i].path + L"|" + g_favs[i].name);
}

int FindFav(const wstring& p) {
    for (size_t i = 0; i < g_favs.size(); i++) if (IEq(g_favs[i].path, p)) return (int)i;
    return -1;
}

bool IsKnown(const wstring& p) {
    for (auto& k : g_known) if (IEq(k.second, p)) return true;
    return false;
}

vector<wstring> TopRecent(size_t n) {
    long long now = time(nullptr);
    vector<std::pair<double, wstring>> v;
    for (auto& kv : g_visits) {
        const Visit& x = kv.second;
        if (FindFav(x.path) >= 0 || IsKnown(x.path) || IsRoot(x.path)) continue;
        double age = (now - x.time) / 86400.0;
        v.push_back({x.count * std::pow(0.5, age / 7.0), x.path});
    }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    vector<wstring> r;
    for (size_t i = 0; i < v.size() && i < n; i++) r.push_back(v[i].second);
    return r;
}

HTREEITEM Add(HTREEITEM parent, const wstring& text, int image, Node n, bool bold = false) {
    g_nodes.push_back(n);
    TVINSERTSTRUCTW is{};
    is.hParent = parent;
    is.hInsertAfter = TVI_LAST;
    is.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_STATE;
    is.item.pszText = (LPWSTR)text.c_str();
    is.item.iImage = is.item.iSelectedImage = image;
    is.item.lParam = (LPARAM)(g_nodes.size() - 1);
    is.item.stateMask = TVIS_BOLD;
    is.item.state = bold ? TVIS_BOLD : 0;
    return TreeView_InsertItem(g_tree, &is);
}

void Rebuild() {
    // remember group expansion
    for (HTREEITEM h = TreeView_GetRoot(g_tree); h; h = TreeView_GetNextSibling(g_tree, h)) {
        TVITEMW it{TVIF_PARAM | TVIF_STATE};
        it.hItem = h;
        it.stateMask = TVIS_EXPANDED;
        TreeView_GetItem(g_tree, &it);
        Kind k = g_nodes[it.lParam].kind;
        if (k <= K_GDRIVES && TreeView_GetChild(g_tree, h)) g_expanded[k] = (it.state & TVIS_EXPANDED) != 0;  // empty groups can't expand
    }
    SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(g_tree);
    g_nodes.clear();
    const int none = 0x7FFF;
    HTREEITEM gf = Add(TVI_ROOT, L"★  Favourites", none, {K_GFAV}, true);
    for (size_t i = 0; i < g_favs.size(); i++) {
        Node n{K_FAV, g_favs[i].path, (int)i};
        Add(gf, g_favs[i].name.empty() ? FileName(g_favs[i].path) : g_favs[i].name, FolderIcon(), n);
    }
    HTREEITEM gq = Add(TVI_ROOT, L"⚡  Quick access", none, {K_GQUICK}, true);
    for (auto& k : g_known) Add(gq, k.first, PathIcon(k.second), {K_KNOWN, k.second});
    g_lastTop = TopRecent(8);
    for (auto& p : g_lastTop) Add(gq, FileName(p), FolderIcon(), {K_RECENT, p});
    HTREEITEM gd = Add(TVI_ROOT, L"Drives", none, {K_GDRIVES}, true);
    vector<Item> drives = ListDrives();
    for (auto& d : drives) {
        Item it = d;
        auto l = g_driveLabels.find(d.name);
        if (l != g_driveLabels.end()) it.label = l->second;
        Add(gd, DriveDisplay(it), DriveIcon(d.dtype), {K_DRIVE, d.name});
    }
    HTREEITEM groups[3] = {gf, gq, gd};
    for (int i = 0; i < 3; i++) TreeView_Expand(g_tree, groups[i], g_expanded[i] ? TVE_EXPAND : TVE_COLLAPSE);
    SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_tree, nullptr, TRUE);
    DriveInfoAsync(g_tree, drives);
}

// ▲ ▼ hit areas at the right end of a favourite row
void ArrowRects(const RECT& row, RECT& up, RECT& down) {
    RECT cr; GetClientRect(g_tree, &cr);
    int w = S(22), right = cr.right - S(4);
    down = {right - w, row.top, right, row.bottom};
    up = {right - 2 * w, row.top, right - w, row.bottom};
}

Node* NodeAt(POINT screen, HTREEITEM* out = nullptr) {
    TVHITTESTINFO hi{};
    hi.pt = screen;
    ScreenToClient(g_tree, &hi.pt);
    HTREEITEM h = TreeView_HitTest(g_tree, &hi);
    if (!h || !(hi.flags & (TVHT_ONITEM | TVHT_ONITEMRIGHT | TVHT_ONITEMINDENT))) return nullptr;
    TVITEMW it{TVIF_PARAM};
    it.hItem = h;
    TreeView_GetItem(g_tree, &it);
    if (out) *out = h;
    return it.lParam < (LPARAM)g_nodes.size() ? &g_nodes[it.lParam] : nullptr;
}

void RemoveNode(const Node& n) {
    if (n.kind == K_FAV && n.fav >= 0 && n.fav < (int)g_favs.size()) { g_favs.erase(g_favs.begin() + n.fav); SaveFavs(); }
    if (n.kind == K_RECENT) g_visits.erase(Lower(n.path));
    PostMessageW(g_tree, WM_SB_REBUILD, 0, 0);
}

void Go(const Node& n, bool newTab) {
    if (n.kind <= K_GDRIVES) return;
    if (!Exists(n.path)) {
        g_missing.insert(Lower(n.path));
        InvalidateRect(g_tree, nullptr, FALSE);
        if (n.kind == K_FAV || n.kind == K_RECENT) {
            wstring q = L"Folder not found:\n" + n.path + L"\n\nRemove it from the list?";
            if (MessageBoxW(g_main, q.c_str(), APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES) RemoveNode(n);
        } else {
            MessageBeep(MB_ICONWARNING);
        }
        return;
    }
    g_missing.erase(Lower(n.path));
    TreeView_SelectItem(g_tree, nullptr);  // the sidebar is a launcher, not a selection
    Pane* p = ActivePane();
    if (newTab) p->NewTab(n.path); else p->Navigate(n.path);
    p->FocusList();
}

void Menu(Node n, POINT pt) {
    if (n.kind <= K_GDRIVES) return;
    enum { M_TAB = 1, M_PIN, M_RENAME, M_REMOVE, M_UP, M_DOWN, M_TERM, M_PROPS, M_EJECT };
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, M_TAB, L"Open in new tab");
    if (n.kind == K_DRIVE && IsExternalDrive(n.path)) AppendMenuW(m, MF_STRING, M_EJECT, L"Eject");
    if (n.kind == K_FAV) {
        AppendMenuW(m, MF_STRING, M_RENAME, L"Rename");
        AppendMenuW(m, MF_STRING, M_REMOVE, L"Remove from Favourites");
        AppendMenuW(m, MF_STRING | (n.fav > 0 ? 0 : MF_GRAYED), M_UP, L"Move up");
        AppendMenuW(m, MF_STRING | (n.fav + 1 < (int)g_favs.size() ? 0 : MF_GRAYED), M_DOWN, L"Move down");
    } else {
        AppendMenuW(m, MF_STRING | (FindFav(n.path) >= 0 ? MF_GRAYED : 0), M_PIN, L"Pin to Favourites");
        if (n.kind == K_RECENT) AppendMenuW(m, MF_STRING, M_REMOVE, L"Remove from list");
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_TERM, L"Open terminal here");
    AppendMenuW(m, MF_STRING, M_PROPS, L"Properties");
    int r = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_main, nullptr);
    DestroyMenu(m);
    switch (r) {
    case M_TAB: Go(n, true); break;
    case M_PIN: AddFavourite(n.path); break;
    case M_REMOVE: RemoveNode(n); break;
    case M_UP: case M_DOWN: {
        int j = n.fav + (r == M_UP ? -1 : 1);
        std::swap(g_favs[n.fav], g_favs[j]);
        SaveFavs();
        Rebuild();
        break;
    }
    case M_RENAME:
        for (HTREEITEM h = TreeView_GetChild(g_tree, TreeView_GetRoot(g_tree)); h; h = TreeView_GetNextSibling(g_tree, h)) {
            TVITEMW it{TVIF_PARAM}; it.hItem = h; TreeView_GetItem(g_tree, &it);
            if (g_nodes[it.lParam].fav == n.fav) { SetFocus(g_tree); TreeView_EditLabel(g_tree, h); break; }
        }
        break;
    case M_TERM: OpenTerminal(n.path); break;
    case M_PROPS: ShowProperties(n.path); break;
    case M_EJECT: EjectDrive(n.path); break;
    }
}

LRESULT CALLBACK TreeProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_SB_REBUILD: Rebuild(); return 0;
    case WM_APP_DRIVEINFO: {
        std::unique_ptr<DriveInfo> d((DriveInfo*)l);
        if (d->label.empty() || g_driveLabels[d->root] == d->label) return 0;
        g_driveLabels[d->root] = d->label;
        for (HTREEITEM g = TreeView_GetRoot(h); g; g = TreeView_GetNextSibling(h, g))
            for (HTREEITEM c = TreeView_GetChild(h, g); c; c = TreeView_GetNextSibling(h, c)) {
                TVITEMW it{TVIF_PARAM}; it.hItem = c; TreeView_GetItem(h, &it);
                Node& n = g_nodes[it.lParam];
                if (n.kind != K_DRIVE || !IEq(n.path, d->root)) continue;
                Item di; di.name = n.path; di.label = d->label; di.dtype = GetDriveTypeW(n.path.c_str());
                wstring text = DriveDisplay(di);
                TVITEMW st{TVIF_TEXT}; st.hItem = c; st.pszText = (LPWSTR)text.c_str();
                TreeView_SetItem(h, &st);
            }
        return 0;
    }
    case WM_MBUTTONUP: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ClientToScreen(h, &pt);
        if (Node* n = NodeAt(pt)) Go(*n, true);
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_RETURN) {
            HTREEITEM s = TreeView_GetSelection(h);
            if (s) { TVITEMW it{TVIF_PARAM}; it.hItem = s; TreeView_GetItem(h, &it); Go(g_nodes[it.lParam], GetKeyState(VK_CONTROL) < 0); }
            return 0;
        }
        if (w == VK_DELETE) {
            HTREEITEM s = TreeView_GetSelection(h);
            if (s) { TVITEMW it{TVIF_PARAM}; it.hItem = s; TreeView_GetItem(h, &it); if (g_nodes[it.lParam].kind == K_FAV || g_nodes[it.lParam].kind == K_RECENT) RemoveNode(g_nodes[it.lParam]); }
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        if (g_dragFav >= 0) {
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ClientToScreen(h, &pt);
            HTREEITEM t = nullptr;
            Node* n = NodeAt(pt, &t);
            TreeView_SelectDropTarget(h, n && n->kind == K_FAV ? t : nullptr);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_dragFav >= 0) {
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ClientToScreen(h, &pt);
            Node* n = NodeAt(pt);
            int from = g_dragFav;
            g_dragFav = -1;
            ReleaseCapture();
            TreeView_SelectDropTarget(h, nullptr);
            if (n && n->kind == K_FAV && n->fav != from) {
                Fav f = g_favs[from];
                g_favs.erase(g_favs.begin() + from);
                g_favs.insert(g_favs.begin() + n->fav, f);
                SaveFavs();
                Rebuild();
            }
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (g_dragFav >= 0) { g_dragFav = -1; TreeView_SelectDropTarget(h, nullptr); }
        break;
    }
    return DefSubclassProc(h, m, w, l);
}

}  // namespace

void SidebarCreate(HWND parent) {
    g_tree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_FULLROWSELECT |
                             TVS_EDITLABELS | TVS_NOHSCROLL | TVS_TRACKSELECT, 0, 0, 10, 10, parent, nullptr, g_inst, nullptr);
    SetWindowTheme(g_tree, L"Explorer", nullptr);
    TreeView_SetExtendedStyle(g_tree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    TreeView_SetImageList(g_tree, SysImageList(), TVSIL_NORMAL);
    TreeView_SetItemHeight(g_tree, S(26));
    TreeView_SetIndent(g_tree, S(19));
    SendMessageW(g_tree, WM_SETFONT, (WPARAM)g_font, FALSE);
    SetWindowSubclass(g_tree, TreeProc, 1, 0);

    const KNOWNFOLDERID* ids[] = {&FOLDERID_Desktop, &FOLDERID_Downloads, &FOLDERID_Documents, &FOLDERID_Pictures, &FOLDERID_Videos, &FOLDERID_Music};
    const wchar_t* names[] = {L"Desktop", L"Downloads", L"Documents", L"Pictures", L"Videos", L"Music"};
    for (int i = 0; i < 6; i++) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*ids[i], 0, nullptr, &p))) g_known.push_back({names[i], p});
        CoTaskMemFree(p);
    }
    for (int i = 0;; i++) {
        wstring v = IniGet(L"Favourites", (L"f" + std::to_wstring(i)).c_str());
        if (v.empty()) break;
        size_t bar = v.find(L'|');
        g_favs.push_back({v.substr(0, bar), bar == wstring::npos ? L"" : v.substr(bar + 1)});
    }
    for (int i = 0;; i++) {
        wstring v = IniGet(L"Recent", (L"r" + std::to_wstring(i)).c_str());
        if (v.empty()) break;
        size_t a = v.find(L'\t'), b = v.find(L'\t', a + 1);
        if (a == wstring::npos || b == wstring::npos) continue;
        Visit x{v.substr(0, a), _wtof(v.substr(a + 1, b - a - 1).c_str()), _wtoi64(v.substr(b + 1).c_str())};
        g_visits[Lower(x.path)] = x;
    }
    Rebuild();
}

HWND SidebarHwnd() { return g_tree; }
void SidebarRefreshDrives() { Rebuild(); }

void SidebarSave() {
    // keep the 150 best entries
    long long now = time(nullptr);
    vector<std::pair<double, Visit>> v;
    for (auto& kv : g_visits) v.push_back({kv.second.count * std::pow(0.5, (now - kv.second.time) / 86400.0 / 7.0), kv.second});
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    IniClearSection(L"Recent");
    for (size_t i = 0; i < v.size() && i < 150; i++) {
        wchar_t c[32]; swprintf(c, 32, L"%.2f", v[i].second.count);
        IniSet(L"Recent", (L"r" + std::to_wstring(i)).c_str(), v[i].second.path + L"\t" + c + L"\t" + std::to_wstring(v[i].second.time));
    }
}

bool IsFavourite(const wstring& p) { return !p.empty() && FindFav(p) >= 0; }

void AddFavourite(const wstring& p) {
    if (p.empty() || FindFav(p) >= 0) return;
    g_favs.push_back({p, L""});
    SaveFavs();
    Rebuild();
    for (int i = 0; i < 3; i++) if (g_panes[i]) g_panes[i]->UpdateStar();
}

void ToggleFavourite(const wstring& p) {
    int i = FindFav(p);
    if (i < 0) { AddFavourite(p); return; }
    g_favs.erase(g_favs.begin() + i);
    SaveFavs();
    Rebuild();
    for (int k = 0; k < 3; k++) if (g_panes[k]) g_panes[k]->UpdateStar();
}

void TrackVisit(const wstring& p) {
    Visit& v = g_visits[Lower(p)];
    v.path = p;
    v.count += 1;
    v.time = time(nullptr);
    if (TopRecent(8) != g_lastTop) PostMessageW(g_tree, WM_SB_REBUILD, 0, 0);
}

vector<wstring> RecentPaths(size_t n) {
    vector<const Visit*> v;
    for (auto& kv : g_visits) v.push_back(&kv.second);
    std::sort(v.begin(), v.end(), [](auto a, auto b) { return a->time > b->time; });
    vector<wstring> r;
    for (size_t i = 0; i < v.size() && r.size() < n; i++) r.push_back(v[i]->path);
    return r;
}

bool SidebarDropTarget(POINT screen, wstring& dir, bool& pin) {
    HTREEITEM h = nullptr;
    Node* n = NodeAt(screen, &h);
    pin = false;
    if (!n || n->kind == K_GQUICK || n->kind == K_GDRIVES) { TreeView_SelectDropTarget(g_tree, nullptr); return false; }
    TreeView_SelectDropTarget(g_tree, h);
    if (n->kind == K_GFAV) { pin = true; return true; }
    dir = n->path;
    return true;
}

void SidebarClearDropHi() { TreeView_SelectDropTarget(g_tree, nullptr); }

void SidebarOnNotify(NMHDR* nm, LRESULT& res) {
    switch (nm->code) {
    case NM_CLICK: {
        POINT pt; GetCursorPos(&pt);
        HTREEITEM h = nullptr;
        Node* n = NodeAt(pt, &h);
        if (!n) return;
        if (n->kind <= K_GDRIVES) { TreeView_Expand(g_tree, h, TVE_TOGGLE); res = TRUE; return; }
        if (n->kind == K_FAV && g_favs.size() > 1) {  // ▲ / ▼ buttons move the favourite
            RECT row, up, down;
            TreeView_GetItemRect(g_tree, h, &row, FALSE);
            ArrowRects(row, up, down);
            POINT c = pt;
            ScreenToClient(g_tree, &c);
            int j = PtInRect(&up, c) ? n->fav - 1 : PtInRect(&down, c) ? n->fav + 1 : -2;
            if (j != -2) {
                if (j >= 0 && j < (int)g_favs.size()) {
                    std::swap(g_favs[n->fav], g_favs[j]);
                    SaveFavs();
                    Rebuild();
                }
                res = TRUE;
                return;
            }
        }
        Go(*n, GetKeyState(VK_CONTROL) < 0);
        res = TRUE;
        return;
    }
    case NM_RCLICK: {
        POINT pt; GetCursorPos(&pt);
        if (Node* n = NodeAt(pt)) Menu(*n, pt);
        res = TRUE;
        return;
    }
    case TVN_BEGINDRAGW: {
        auto tv = (NMTREEVIEWW*)nm;
        Node& n = g_nodes[tv->itemNew.lParam];
        if (n.kind == K_FAV) { g_dragFav = n.fav; SetCapture(g_tree); }
        return;
    }
    case TVN_BEGINLABELEDITW: {
        auto d = (NMTVDISPINFOW*)nm;
        res = g_nodes[d->item.lParam].kind != K_FAV;
        return;
    }
    case TVN_ENDLABELEDITW: {
        auto d = (NMTVDISPINFOW*)nm;
        Node& n = g_nodes[d->item.lParam];
        if (d->item.pszText && n.kind == K_FAV) {
            g_favs[n.fav].name = d->item.pszText;
            SaveFavs();
            res = TRUE;
        }
        return;
    }
    case NM_CUSTOMDRAW: {
        auto cd = (NMTVCUSTOMDRAW*)nm;
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) { res = CDRF_NOTIFYITEMDRAW; return; }
        size_t i = cd->nmcd.lItemlParam;
        if (i >= g_nodes.size()) return;
        if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            if (g_nodes[i].kind <= K_GDRIVES) cd->clrText = RGB(110, 110, 110);
            else if (g_missing.count(Lower(g_nodes[i].path))) cd->clrText = RGB(170, 170, 170);
            bool arrows = g_nodes[i].kind == K_FAV && g_favs.size() > 1 && (cd->nmcd.uItemState & CDIS_HOT);
            res = arrows ? CDRF_NOTIFYPOSTPAINT : CDRF_DODEFAULT;
        } else if (cd->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT) {  // ▲ ▼ on the hovered favourite
            RECT up, down;
            ArrowRects(cd->nmcd.rc, up, down);
            HDC dc = cd->nmcd.hdc;
            HGDIOBJ of = SelectObject(dc, g_iconFont);
            SetBkMode(dc, TRANSPARENT);
            int f = g_nodes[i].fav;
            SetTextColor(dc, f > 0 ? RGB(60, 60, 60) : RGB(200, 200, 200));
            DrawTextW(dc, L"", -1, &up, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(dc, f + 1 < (int)g_favs.size() ? RGB(60, 60, 60) : RGB(200, 200, 200));
            DrawTextW(dc, L"", -1, &down, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, of);
        }
        return;
    }
    }
}
