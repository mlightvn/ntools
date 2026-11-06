// Sidebar: QUICK ACCESS (pinned folders), FAVOURITES (bookmarks in nested groups), RECENT (most used folders),
// FOLDERS (drives with lazily loaded subfolders) and the disk usage of the active pane's drive.
#include "app.h"
#include "pane.h"
#include "theme.h"
#include "favs.h"
#include <knownfolders.h>
#include <cmath>
#include <ctime>
#include <thread>

namespace {

enum Kind { K_HDR, K_PIN, K_FAV, K_RECENT, K_DIR };
enum Sec { S_QUICK, S_FAVS, S_RECENT, S_FOLDERS, S_COUNT };
struct Node { Kind kind; wstring path; int sec = 0; int fav = 0; int pin = -1; bool loaded = false; };
struct Visit { wstring path; double count = 0; long long time = 0; };
struct Children { int gen; HTREEITEM item; vector<wstring> names; };
struct Usage { wstring root, label; ULONGLONG free = 0, total = 0; bool known = false; };

HWND g_side = nullptr, g_tree = nullptr;
vector<Node> g_nodes;                                  // TreeView lParam = index
HTREEITEM g_hdr[S_COUNT];
bool g_secOpen[S_COUNT] = {true, true, true, true};
std::unordered_map<wstring, Visit> g_visits;           // key: lower-case path
std::unordered_map<wstring, wstring> g_driveLabels;    // root -> label
std::unordered_map<wstring, const wchar_t*> g_knownGlyph;  // lower-case known folder -> glyph
std::unordered_set<wstring> g_missing;
vector<wstring> g_lastTop;
wstring g_current;                                     // active pane folder (lower case)
int g_folderGen = 0;                                   // invalidates pending subfolder lists after a rebuild
int g_dragNode = -1;                                   // pin / favourite being dragged inside the sidebar
int g_pinGroup = -1;                                   // drop of folders from a pane: -2 = Quick access, >= 0 = favourites group
Usage g_usage;
enum { WM_SB_REBUILD = WM_APP + 70, WM_SB_CHILDREN };
const int kUsageH = 86;

// ---------- data ----------
bool IsFav(const wstring& p) { return FavFindPath(p) != nullptr; }

vector<wstring> TopRecent(size_t n) {
    long long now = time(nullptr);
    vector<std::pair<double, wstring>> v;
    for (auto& kv : g_visits) {
        const Visit& x = kv.second;
        if (IsPinned(x.path) || IsFav(x.path) || IsRoot(x.path)) continue;
        double age = (now - x.time) / 86400.0;
        v.push_back({x.count * std::pow(0.5, age / 7.0), x.path});
    }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    vector<wstring> r;
    for (size_t i = 0; i < v.size() && i < n; i++) r.push_back(v[i].second);
    return r;
}

int ImageFor(const Node& n) {
    if (g_theme->icons != ICON_SYSTEM) return 0;
    if (n.kind == K_HDR) return 0x7FFF;
    if (IsBin(n.path)) { SHSTOCKICONINFO s{sizeof(s)}; SHGetStockIconInfo(SIID_RECYCLER, SHGSI_SYSICONINDEX, &s); return s.iSysImageIndex; }
    if (IsRoot(n.path)) return DriveIcon(GetDriveTypeW(n.path.c_str()));
    if (g_knownGlyph.count(Lower(n.path))) return PathIcon(n.path);
    return FolderIcon();
}

HTREEITEM Add(HTREEITEM parent, const wstring& text, Node n, bool kids = false) {
    int image = ImageFor(n);
    g_nodes.push_back(std::move(n));
    TVINSERTSTRUCTW is{};
    is.hParent = parent;
    is.hInsertAfter = TVI_LAST;
    is.item.mask = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_CHILDREN;
    is.item.pszText = (LPWSTR)text.c_str();
    is.item.iImage = is.item.iSelectedImage = image;
    is.item.lParam = (LPARAM)(g_nodes.size() - 1);
    is.item.cChildren = kids ? 1 : 0;
    return TreeView_InsertItem(g_tree, &is);
}

Node* NodeOf(HTREEITEM h) {
    if (!h) return nullptr;
    TVITEMW it{TVIF_PARAM};
    it.hItem = h;
    if (!TreeView_GetItem(g_tree, &it) || it.lParam >= (LPARAM)g_nodes.size()) return nullptr;
    return &g_nodes[it.lParam];
}

int FavIdOf(HTREEITEM h) {
    Node* n = NodeOf(h);
    return n && n->kind == K_FAV ? n->fav : 0;
}

void ClearChildren(HTREEITEM h) {
    for (HTREEITEM c = TreeView_GetChild(g_tree, h); c;) {
        HTREEITEM next = TreeView_GetNextSibling(g_tree, c);
        TreeView_DeleteItem(g_tree, c);
        c = next;
    }
}

void FillFavs(HTREEITEM parent, const vector<Fav>& list) {
    for (auto& f : list) {
        Node n{K_FAV, f.path, S_FAVS, f.id};
        HTREEITEM h = Add(parent, f.Label(), n, f.group() && !f.kids.empty());
        if (f.group()) {
            FillFavs(h, f.kids);
            if (f.open) TreeView_Expand(g_tree, h, TVE_EXPAND);
        }
    }
}

void FillSection(int s) {
    HTREEITEM hdr = g_hdr[s];
    switch (s) {
    case S_QUICK:
        for (size_t i = 0; i < g_pins.size(); i++) {
            Node n{K_PIN, g_pins[i].path, S_QUICK};
            n.pin = (int)i;
            Add(hdr, g_pins[i].name.empty() ? DisplayName(g_pins[i].path) : g_pins[i].name, n);
        }
        break;
    case S_FAVS: FillFavs(hdr, g_favs); break;
    case S_RECENT:
        g_lastTop = TopRecent(8);
        for (auto& p : g_lastTop) Add(hdr, FileName(p), {K_RECENT, p, S_RECENT});
        break;
    case S_FOLDERS: {
        g_folderGen++;
        vector<Item> drives = ListDrives();
        for (auto& d : drives) {
            Item it = d;
            auto l = g_driveLabels.find(d.name);
            if (l != g_driveLabels.end()) it.label = l->second;
            Add(hdr, DriveDisplay(it), {K_DIR, d.name, S_FOLDERS}, true);
        }
        DriveInfoAsync(g_side, drives);
        break;
    }
    }
}

void RebuildSection(int s) {
    SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
    ClearChildren(g_hdr[s]);
    FillSection(s);
    TVITEMW it{TVIF_CHILDREN};
    it.hItem = g_hdr[s];
    it.cChildren = 1;
    TreeView_SetItem(g_tree, &it);
    TreeView_Expand(g_tree, g_hdr[s], g_secOpen[s] ? TVE_EXPAND : TVE_COLLAPSE);
    SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_tree, nullptr, TRUE);
}

void RebuildAll() {
    SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(g_tree);
    g_nodes.clear();
    static const wchar_t* names[S_COUNT] = {L"QUICK ACCESS", L"FAVOURITES", L"RECENT", L"FOLDERS"};
    for (int s = 0; s < S_COUNT; s++) {
        Node n{K_HDR, L"", s};
        g_hdr[s] = Add(TVI_ROOT, names[s], n, true);
        FillSection(s);
        TreeView_Expand(g_tree, g_hdr[s], g_secOpen[s] ? TVE_EXPAND : TVE_COLLAPSE);
    }
    SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_tree, nullptr, TRUE);
}

void LoadChildren(HTREEITEM h, const wstring& path) {
    int gen = g_folderGen;
    HWND target = g_side;
    std::thread([=] {
        auto c = new Children{gen, h, ListSubdirs(path, L"", 5000)};
        if (!PostMessageW(target, WM_SB_CHILDREN, 0, (LPARAM)c)) delete c;
    }).detach();
}

// ---------- disk usage of the active drive ----------
void UpdateUsage() {
    Pane* p = ActivePane();
    wstring path = p && p->TabCount() ? (p->InSearch() ? SearchRoot(p->Path()) : p->Path()) : L"";  // no tabs yet at startup
    wstring root;
    if (IsFsPath(path) && path.size() >= 2 && path[1] == L':') root = path.substr(0, 2) + L"\\";
    else if (IsFsPath(path) && path.rfind(L"\\\\", 0) == 0) { wchar_t b[MAX_PATH]; lstrcpynW(b, path.c_str(), MAX_PATH); PathStripToRootW(b); root = b; }
    else {  // This PC / Recycle Bin: the Windows drive
        wchar_t w[MAX_PATH] = L"C:\\";
        GetWindowsDirectoryW(w, MAX_PATH);
        root = wstring(w).substr(0, 3);
    }
    g_usage = {};
    g_usage.root = root;
    UINT type = GetDriveTypeW(root.c_str());
    Item it;
    it.name = root;
    it.dtype = type;
    auto l = g_driveLabels.find(root);
    if (l != g_driveLabels.end()) it.label = l->second;
    g_usage.label = IsRoot(root) ? DriveDisplay(it) : root;
    if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {  // local: instant; network drives may hang, so they show no numbers
        ULARGE_INTEGER fr, tot;
        if (GetDiskFreeSpaceExW(root.c_str(), &fr, &tot, nullptr)) { g_usage.free = fr.QuadPart; g_usage.total = tot.QuadPart; g_usage.known = true; }
    }
    RECT rc; GetClientRect(g_side, &rc);
    rc.top = rc.bottom - S(kUsageH);
    InvalidateRect(g_side, &rc, FALSE);
}

void PaintUsage(HDC dc, RECT rc) {
    const Theme& t = *g_theme;
    Fill(dc, rc, t.sidebar);
    RECT ln{rc.left + S(14), rc.top, rc.right - S(14), rc.top + 1};
    Fill(dc, ln, t.border);
    SetBkMode(dc, TRANSPARENT);
    RECT lab{rc.left + S(16), rc.top + S(12), rc.right - S(16), rc.top + S(30)};
    wstring name = g_usage.label;
    if (!name.empty()) CharUpperBuffW(&name[0], (DWORD)name.size());
    SelectObject(dc, g_fontSmall);
    SetTextColor(dc, t.faint);
    DrawTextW(dc, name.c_str(), -1, &lab, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
    if (!g_usage.known || !g_usage.total) return;
    int pct = (int)((g_usage.total - g_usage.free) * 100 / g_usage.total);
    COLORREF bar = pct >= 90 ? RGB(239, 83, 80) : t.accent;
    wstring ps = std::to_wstring(pct) + L"%";
    SelectObject(dc, g_fontMono);
    SetTextColor(dc, bar);
    DrawTextW(dc, ps.c_str(), -1, &lab, DT_SINGLELINE | DT_VCENTER | DT_RIGHT);
    RECT track{rc.left + S(16), rc.top + S(36), rc.right - S(16), rc.top + S(42)};
    RoundFill(dc, track, S(6), t.chip, t.chip);
    RECT used = track;
    used.right = track.left + (track.right - track.left) * pct / 100;
    if (used.right > used.left + S(4)) RoundFill(dc, used, S(6), bar, bar);
    RECT txt{rc.left + S(16), rc.top + S(50), rc.right - S(16), rc.top + S(70)};
    wstring s = FmtSize(g_usage.free) + L" free of " + FmtSize(g_usage.total);
    SetTextColor(dc, t.dim);
    DrawTextW(dc, s.c_str(), -1, &txt, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
}

// ---------- actions ----------
Node* NodeAt(POINT screen, HTREEITEM* out = nullptr) {
    TVHITTESTINFO hi{};
    hi.pt = screen;
    ScreenToClient(g_tree, &hi.pt);
    HTREEITEM h = TreeView_HitTest(g_tree, &hi);
    if (!h || !(hi.flags & (TVHT_ONITEM | TVHT_ONITEMRIGHT | TVHT_ONITEMINDENT))) return nullptr;
    if (out) *out = h;
    return NodeOf(h);
}

HTREEITEM ItemOfFav(HTREEITEM from, int favId) {
    for (HTREEITEM c = TreeView_GetChild(g_tree, from); c; c = TreeView_GetNextSibling(g_tree, c)) {
        if (FavIdOf(c) == favId) return c;
        if (HTREEITEM r = ItemOfFav(c, favId)) return r;
    }
    return nullptr;
}

void EditFav(int favId) {
    if (HTREEITEM h = ItemOfFav(g_hdr[S_FAVS], favId)) { SetFocus(g_tree); TreeView_EnsureVisible(g_tree, h); TreeView_EditLabel(g_tree, h); }
}

void Go(const Node& n, bool newTab) {
    if (n.kind == K_HDR || (n.kind == K_FAV && n.path.empty())) return;
    if (!IsVirtual(n.path) && !Exists(n.path)) {
        g_missing.insert(Lower(n.path));
        InvalidateRect(g_tree, nullptr, FALSE);
        if (n.kind == K_DIR) { MessageBeep(MB_ICONWARNING); return; }
        wstring q = L"Folder not found:\n" + n.path + L"\n\nRemove it from the list?";
        if (MessageBoxW(g_main, q.c_str(), APP_NAME, MB_YESNO | MB_ICONQUESTION) != IDYES) return;
        if (n.kind == K_PIN) TogglePin(n.path);
        else if (n.kind == K_FAV) { FavRemove(n.fav); FavsChanged(); }
        else { g_visits.erase(Lower(n.path)); PostMessageW(g_side, WM_SB_REBUILD, S_RECENT, 0); }
        return;
    }
    g_missing.erase(Lower(n.path));
    TreeView_SelectItem(g_tree, nullptr);  // the sidebar is a launcher, not a selection
    Pane* p = ActivePane();
    if (newTab) p->NewTab(n.path); else p->Navigate(n.path);
    PostMessageW(g_main, WM_APP_FOCUSLIST, 0, 0);  // the tree grabs focus again after NM_CLICK
}

// groups as a submenu, for "Move to group"
void GroupMenu(HMENU m, const vector<Fav>& list, int skip, int base, int depth) {
    for (auto& f : list) {
        if (!f.group() || f.id == skip) continue;
        AppendMenuW(m, MF_STRING, base + f.id, (wstring(depth * 3, L' ') + f.Label()).c_str());
        GroupMenu(m, f.kids, skip, base, depth + 1);
    }
}

wstring CurrentFolder() {
    Pane* p = ActivePane();
    if (!p || !p->TabCount()) return L"";
    wstring path = p->InSearch() ? SearchRoot(p->Path()) : p->Path();
    return IsFsPath(path) ? path : L"";
}

void Menu(Node n, POINT pt) {
    enum { M_TAB = 1, M_PIN, M_UNPIN, M_FAV, M_RENAME, M_REMOVE, M_UP, M_DOWN, M_TERM, M_PROPS, M_EJECT, M_NEWGROUP, M_MANAGE,
           M_ADDCUR, M_PINCUR, M_OPENALL, M_REFRESH, M_TOP = 900, M_GROUP = 1000 };
    HMENU m = CreatePopupMenu();
    bool real = IsFsPath(n.path);
    wstring cur = CurrentFolder();
    if (n.kind == K_HDR) {
        if (n.sec == S_RECENT) { DestroyMenu(m); return; }
        if (n.sec == S_QUICK) AppendMenuW(m, MF_STRING | (cur.empty() ? MF_GRAYED : 0), M_PINCUR, L"Pin current folder");
        if (n.sec == S_FAVS) {
            AppendMenuW(m, MF_STRING | (cur.empty() ? MF_GRAYED : 0), M_ADDCUR, L"Add current folder\tCtrl+D");
            AppendMenuW(m, MF_STRING, M_NEWGROUP, L"New group…");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, M_MANAGE, L"Manage favourites…");
        }
        if (n.sec == S_FOLDERS) AppendMenuW(m, MF_STRING, M_REFRESH, L"Refresh");
    } else if (n.kind == K_FAV && n.path.empty()) {  // a favourites group
        AppendMenuW(m, MF_STRING, M_OPENALL, L"Open all in new tabs");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING | (cur.empty() ? MF_GRAYED : 0), M_ADDCUR, L"Add current folder here");
        AppendMenuW(m, MF_STRING, M_NEWGROUP, L"New group inside…");
        AppendMenuW(m, MF_STRING, M_RENAME, L"Rename\tF2");
        HMENU groups = CreatePopupMenu();
        AppendMenuW(groups, MF_STRING, M_TOP, L"(Top level)");
        GroupMenu(groups, g_favs, n.fav, M_GROUP, 0);
        AppendMenuW(m, MF_POPUP, (UINT_PTR)groups, L"Move to group");
        AppendMenuW(m, MF_STRING, M_REMOVE, L"Delete group\tDel");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, M_MANAGE, L"Manage favourites…");
    } else {
        AppendMenuW(m, MF_STRING, M_TAB, L"Open in new tab");
        if (n.kind == K_DIR && IsRoot(n.path) && IsExternalDrive(n.path)) AppendMenuW(m, MF_STRING, M_EJECT, L"Eject");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        if (n.kind == K_PIN) {
            AppendMenuW(m, MF_STRING, M_RENAME, L"Rename\tF2");
            AppendMenuW(m, MF_STRING | (n.pin > 0 ? 0 : MF_GRAYED), M_UP, L"Move up");
            AppendMenuW(m, MF_STRING | (n.pin + 1 < (int)g_pins.size() ? 0 : MF_GRAYED), M_DOWN, L"Move down");
            AppendMenuW(m, MF_STRING, M_UNPIN, L"Unpin from Quick access\tDel");
        } else if (!IsVirtual(n.path)) {
            AppendMenuW(m, MF_STRING, M_PIN, IsPinned(n.path) ? L"Unpin from Quick access" : L"Pin to Quick access");
        }
        if (n.kind == K_FAV) {
            AppendMenuW(m, MF_STRING, M_RENAME, L"Rename\tF2");
            HMENU groups = CreatePopupMenu();
            AppendMenuW(groups, MF_STRING, M_TOP, L"(Top level)");
            GroupMenu(groups, g_favs, -1, M_GROUP, 0);
            AppendMenuW(m, MF_POPUP, (UINT_PTR)groups, L"Move to group");
            AppendMenuW(m, MF_STRING, M_REMOVE, L"Remove from Favourites\tDel");
        } else if (real) {
            AppendMenuW(m, MF_STRING, M_FAV, IsFav(n.path) ? L"Remove from Favourites" : L"Add to Favourites");
        }
        if (n.kind == K_RECENT) AppendMenuW(m, MF_STRING, M_REMOVE, L"Remove from list\tDel");
        if (real) {
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, M_TERM, L"Open terminal here");
            AppendMenuW(m, MF_STRING, M_PROPS, L"Properties");
        }
        if (n.kind == K_FAV) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); AppendMenuW(m, MF_STRING, M_MANAGE, L"Manage favourites…"); }
    }
    int r = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_main, nullptr);
    DestroyMenu(m);  // destroys the submenus too
    switch (r) {
    case 0: break;
    case M_TAB: Go(n, true); break;
    case M_PIN: case M_UNPIN: TogglePin(n.path); break;
    case M_PINCUR: if (!cur.empty() && !IsPinned(cur)) TogglePin(cur); break;
    case M_FAV: ToggleFavourite(n.path); break;
    case M_ADDCUR:
        if (!cur.empty()) { FavAdd(cur, n.kind == K_FAV ? n.fav : 0); FavsChanged(); }
        break;
    case M_NEWGROUP: {
        wstring name = L"New group";
        if (!InputBox(L"New group", L"Name of the group:", name) || name.empty()) break;
        FavAddGroup(name, n.kind == K_FAV ? n.fav : 0);
        FavsChanged();
        break;
    }
    case M_OPENALL:
        if (Fav* g = FavFind(n.fav)) {
            vector<wstring> paths;
            for (auto& k : g->kids) if (!k.group()) paths.push_back(k.path);
            for (auto& p : paths) ActivePane()->NewTab(p, false);
        }
        break;
    case M_RENAME:
        if (n.kind == K_FAV) { EditFav(n.fav); break; }
        for (HTREEITEM c = TreeView_GetChild(g_tree, g_hdr[S_QUICK]); c; c = TreeView_GetNextSibling(g_tree, c)) {
            Node* x = NodeOf(c);
            if (x && x->pin == n.pin) { SetFocus(g_tree); TreeView_EditLabel(g_tree, c); break; }
        }
        break;
    case M_REMOVE:
        if (n.kind == K_FAV) {
            Fav* f = FavFind(n.fav);
            if (f && f->group() && !f->kids.empty()) {
                wstring q = L"Delete the group \"" + f->Label() + L"\" and everything in it?";
                if (MessageBoxW(g_main, q.c_str(), L"Favourites", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) break;
            }
            FavRemove(n.fav);
            FavsChanged();
        } else if (n.kind == K_RECENT) {
            g_visits.erase(Lower(n.path));
            RebuildSection(S_RECENT);
        }
        break;
    case M_UP: case M_DOWN: {
        int j = n.pin + (r == M_UP ? -1 : 1);
        if (j < 0 || j >= (int)g_pins.size()) break;
        std::swap(g_pins[n.pin], g_pins[j]);
        PinsSave();
        RebuildSection(S_QUICK);
        break;
    }
    case M_MANAGE: FavManager(); break;
    case M_REFRESH: RebuildSection(S_FOLDERS); break;
    case M_TERM: OpenTerminal(n.path); break;
    case M_PROPS: ShowProperties(n.path); break;
    case M_EJECT: EjectDrive(n.path); break;
    default:
        if (r == M_TOP) { if (FavMove(n.fav, 0, 0)) FavsChanged(); }
        else if (r >= M_GROUP) { if (FavMove(n.fav, r - M_GROUP, 0)) FavsChanged(); }
    }
}

// ---------- drag inside the sidebar (reorder pins / move favourites) ----------
bool DragTarget(POINT client, int* target, int* where) {
    TreeView_SetInsertMark(g_tree, nullptr, FALSE);
    TreeView_SelectDropTarget(g_tree, nullptr);
    if (g_dragNode < 0 || g_dragNode >= (int)g_nodes.size()) return false;
    const Node src = g_nodes[g_dragNode];
    TVHITTESTINFO hi{};
    hi.pt = client;
    HTREEITEM h = TreeView_HitTest(g_tree, &hi);
    Node* n = NodeOf(h);
    if (!n) return false;
    if (src.kind == K_FAV) {
        if (n->kind == K_HDR && n->sec == S_FAVS) { TreeView_SelectDropTarget(g_tree, h); *target = 0; *where = 0; return true; }
        if (n->kind != K_FAV) return false;
        return FavDropAt(g_tree, client, FavIdOf, src.fav, target, where);
    }
    if (n->kind != K_PIN) return false;  // pins move only inside Quick access
    RECT r;
    TreeView_GetItemRect(g_tree, h, &r, FALSE);
    *target = n->pin;
    *where = client.y < (r.top + r.bottom) / 2 ? -1 : 1;
    TreeView_SetInsertMark(g_tree, h, *where > 0);
    return n->pin != src.pin;
}

void DragDrop(POINT client) {
    int target = 0, where = 0;
    const Node src = g_nodes[g_dragNode];
    bool ok = DragTarget(client, &target, &where);
    TreeView_SetInsertMark(g_tree, nullptr, FALSE);
    TreeView_SelectDropTarget(g_tree, nullptr);
    if (!ok) return;
    if (src.kind == K_FAV) {
        if (FavMove(src.fav, target, where)) FavsChanged();
        return;
    }
    Pin p = g_pins[src.pin];
    int to = target + (where > 0 ? 1 : 0);
    g_pins.erase(g_pins.begin() + src.pin);
    if (to > src.pin) to--;
    g_pins.insert(g_pins.begin() + std::max(0, std::min(to, (int)g_pins.size())), p);
    PinsSave();
    RebuildSection(S_QUICK);
}

const wchar_t* GlyphOf(const Node& n) {
    if (n.kind == K_FAV) return n.path.empty() ? L"\uE8B7" : L"\uE734";
    if (n.kind == K_RECENT) return L"\uE81C";
    if (IsBin(n.path)) return L"\uE74D";
    if (IsRoot(n.path)) {
        UINT t = GetDriveTypeW(n.path.c_str());
        return t == DRIVE_REMOVABLE ? L"\uE88E" : t == DRIVE_REMOTE ? L"\uE8CE" : t == DRIVE_CDROM ? L"\uE958" : L"\uEDA2";
    }
    auto k = g_knownGlyph.find(Lower(n.path));
    return k != g_knownGlyph.end() ? k->second : L"\uE8B7";
}

void OnNotify(NMHDR* nm, LRESULT& res) {
    switch (nm->code) {
    case NM_CLICK: {
        POINT pt; GetCursorPos(&pt);
        HTREEITEM h = nullptr;
        Node* n = NodeAt(pt, &h);
        if (!n) return;
        if (n->kind == K_HDR || (n->kind == K_FAV && n->path.empty())) { TreeView_Expand(g_tree, h, TVE_TOGGLE); res = TRUE; return; }
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
    case TVN_SELCHANGINGW: {  // the tree auto-selects its first row when it gets focus; the sidebar is a launcher
        auto tv = (NMTREEVIEWW*)nm;
        Node* n = NodeOf(tv->itemNew.hItem);
        if ((tv->action == TVC_UNKNOWN && tv->itemNew.hItem) || (n && n->kind == K_HDR)) res = TRUE;  // headers are never selected
        return;
    }
    case TVN_ITEMEXPANDINGW: {
        auto tv = (NMTREEVIEWW*)nm;
        Node* n = NodeOf(tv->itemNew.hItem);
        if (n && n->kind == K_DIR && !n->loaded && (tv->action & TVE_EXPAND)) {
            n->loaded = true;
            LoadChildren(tv->itemNew.hItem, n->path);
        }
        return;
    }
    case TVN_ITEMEXPANDEDW: {
        auto tv = (NMTREEVIEWW*)nm;
        Node* n = NodeOf(tv->itemNew.hItem);
        bool open = (tv->action & TVE_EXPAND) != 0;
        if (n && n->kind == K_HDR && TreeView_GetChild(g_tree, tv->itemNew.hItem)) g_secOpen[n->sec] = open;
        if (n && n->kind == K_FAV) if (Fav* f = FavFind(n->fav)) f->open = open;
        return;
    }
    case TVN_BEGINDRAGW: {
        auto tv = (NMTREEVIEWW*)nm;
        Node* n = NodeOf(tv->itemNew.hItem);
        if (n && (n->kind == K_FAV || n->kind == K_PIN)) { g_dragNode = (int)tv->itemNew.lParam; SetCapture(g_tree); }
        return;
    }
    case TVN_BEGINLABELEDITW: {
        auto d = (NMTVDISPINFOW*)nm;
        Node* n = d->item.lParam < (LPARAM)g_nodes.size() ? &g_nodes[d->item.lParam] : nullptr;
        res = !(n && (n->kind == K_FAV || n->kind == K_PIN));
        return;
    }
    case TVN_ENDLABELEDITW: {
        auto d = (NMTVDISPINFOW*)nm;
        if (!d->item.pszText || d->item.lParam >= (LPARAM)g_nodes.size()) return;
        const Node n = g_nodes[d->item.lParam];
        wstring text = d->item.pszText;
        if (n.kind == K_FAV) {
            Fav* f = FavFind(n.fav);
            if (!f || (f->group() && text.empty())) return;
            f->name = !f->group() && (text.empty() || IEq(text, DisplayName(f->path))) ? L"" : text;
            FavSave();
            for (int i = 0; i < 3; i++) if (g_panes[i]) g_panes[i]->UpdateStar();
            PostMessageW(g_side, WM_SB_REBUILD, S_FAVS, 0);  // not during the edit notification
        } else if (n.kind == K_PIN && n.pin < (int)g_pins.size()) {
            g_pins[n.pin].name = text.empty() || IEq(text, DisplayName(n.path)) ? L"" : text;
            PinsSave();
            PostMessageW(g_side, WM_SB_REBUILD, S_QUICK, 0);
        }
        res = FALSE;  // the section is rebuilt with the new name
        return;
    }
    case NM_CUSTOMDRAW: {
        auto cd = (NMTVCUSTOMDRAW*)nm;
        if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) { res = CDRF_NOTIFYITEMDRAW; return; }
        size_t i = cd->nmcd.lItemlParam;
        if (i >= g_nodes.size()) return;
        const Node& n = g_nodes[i];
        const Theme& t = *g_theme;
        bool current = n.kind != K_HDR && !n.path.empty() && Lower(n.path) == g_current;
        if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            res = CDRF_NOTIFYPOSTPAINT;
            if (n.kind == K_HDR) {  // section labels
                cd->clrText = t.faint;
                cd->clrTextBk = t.sidebar;
                SelectObject(cd->nmcd.hdc, g_fontSmall);
                res |= CDRF_NEWFONT;
            } else if (g_missing.count(Lower(n.path))) {
                cd->clrText = t.faint;
            } else if (current) {
                cd->clrText = t.accent;
                SelectObject(cd->nmcd.hdc, g_fontBold);
                res |= CDRF_NEWFONT;
            } else if (!(cd->nmcd.uItemState & (CDIS_SELECTED | CDIS_HOT))) {
                cd->clrText = n.kind == K_RECENT ? t.dim : t.text;
            }
        } else if (cd->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT && n.kind != K_HDR && t.icons != ICON_SYSTEM) {
            COLORREF c = current ? t.accent : n.kind == K_FAV && !n.path.empty() ? t.accent : n.kind == K_RECENT ? t.faint : t.folder;
            TreeGlyph(cd->nmcd.hdc, g_tree, (HTREEITEM)cd->nmcd.dwItemSpec, GlyphOf(n), c);
        }
        return;
    }
    }
}

LRESULT CALLBACK TreeProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    switch (m) {
    case WM_MBUTTONUP: {
        POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        ClientToScreen(h, &pt);
        if (Node* n = NodeAt(pt)) Go(*n, true);
        return 0;
    }
    case WM_KEYDOWN: {
        Node* n = NodeOf(TreeView_GetSelection(h));
        if (!n) break;
        const Node c = *n;
        if (w == VK_RETURN) { Go(c, GetKeyState(VK_CONTROL) < 0); return 0; }
        if (w == VK_F2 && (c.kind == K_FAV || c.kind == K_PIN)) { TreeView_EditLabel(h, TreeView_GetSelection(h)); return 0; }
        if (w == VK_DELETE) {
            if (c.kind == K_FAV) { FavRemove(c.fav); FavsChanged(); }
            else if (c.kind == K_PIN) TogglePin(c.path);
            else if (c.kind == K_RECENT) { g_visits.erase(Lower(c.path)); RebuildSection(S_RECENT); }
            return 0;
        }
        break;
    }
    case WM_MOUSEMOVE:
        if (g_dragNode >= 0) {
            int target, where;
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            SetCursor(LoadCursor(nullptr, DragTarget(pt, &target, &where) ? IDC_ARROW : IDC_NO));
            return 0;
        }
        break;
    case WM_LBUTTONUP:
        if (g_dragNode >= 0) {
            POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            DragDrop(pt);
            g_dragNode = -1;
            ReleaseCapture();
            return 0;
        }
        break;
    case WM_CAPTURECHANGED:
        if (g_dragNode >= 0) {
            g_dragNode = -1;
            TreeView_SetInsertMark(h, nullptr, FALSE);
            TreeView_SelectDropTarget(h, nullptr);
        }
        break;
    case WM_CTLCOLOREDIT:  // rename box
        SetTextColor((HDC)w, g_theme->text);
        SetBkColor((HDC)w, g_theme->input);
        return (LRESULT)ThemeBrush(g_theme->input);
    }
    return DefSubclassProc(h, m, w, l);
}

LRESULT CALLBACK SideProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_SIZE: {
        RECT rc; GetClientRect(h, &rc);
        MoveWindow(g_tree, 0, S(6), rc.right, std::max(0, (int)rc.bottom - S(kUsageH) - S(6)), TRUE);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        RECT top = rc; top.bottom = S(6);
        Fill(dc, top, g_theme->sidebar);
        RECT u = rc; u.top = rc.bottom - S(kUsageH);
        PaintUsage(dc, u);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, g_theme->text);
        SetBkColor((HDC)w, g_theme->input);
        return (LRESULT)ThemeBrush(g_theme->input);
    case WM_NOTIFY: {
        LRESULT res = 0;
        if (((NMHDR*)l)->hwndFrom == g_tree) OnNotify((NMHDR*)l, res);
        return res;
    }
    case WM_SB_REBUILD: RebuildSection((int)w); return 0;
    case WM_SB_CHILDREN: {
        std::unique_ptr<Children> c((Children*)l);
        if (c->gen != g_folderGen) return 0;
        Node* n = NodeOf(c->item);
        if (!n || n->kind != K_DIR) return 0;
        wstring parent = n->path;
        SendMessageW(g_tree, WM_SETREDRAW, FALSE, 0);
        ClearChildren(c->item);
        for (auto& name : c->names) Add(c->item, name, {K_DIR, Join(parent, name), S_FOLDERS}, true);
        if (c->names.empty()) {  // no subfolders: drop the expand button
            TVITEMW it{TVIF_CHILDREN};
            it.hItem = c->item;
            it.cChildren = 0;
            TreeView_SetItem(g_tree, &it);
        } else {
            TreeView_Expand(g_tree, c->item, TVE_EXPAND);  // the tree can't open an item before it has children
        }
        SendMessageW(g_tree, WM_SETREDRAW, TRUE, 0);
        InvalidateRect(g_tree, nullptr, TRUE);
        return 0;
    }
    case WM_APP_DRIVEINFO: {
        std::unique_ptr<DriveInfo> d((DriveInfo*)l);
        if (d->label.empty() || g_driveLabels[d->root] == d->label) return 0;
        g_driveLabels[d->root] = d->label;
        for (HTREEITEM c = TreeView_GetChild(g_tree, g_hdr[S_FOLDERS]); c; c = TreeView_GetNextSibling(g_tree, c)) {
            Node* n = NodeOf(c);
            if (!n || !IEq(n->path, d->root)) continue;
            Item di;
            di.name = n->path;
            di.label = d->label;
            di.dtype = GetDriveTypeW(n->path.c_str());
            wstring text = DriveDisplay(di);
            TVITEMW st{TVIF_TEXT};
            st.hItem = c;
            st.pszText = (LPWSTR)text.c_str();
            TreeView_SetItem(g_tree, &st);
        }
        if (IEq(d->root, g_usage.root)) UpdateUsage();
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

}  // namespace

void SidebarCreate(HWND parent) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = SideProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"NxSide";
    RegisterClassW(&wc);
    g_side = CreateWindowExW(0, L"NxSide", L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10, 10, parent, nullptr, g_inst, nullptr);
    g_tree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_FULLROWSELECT |
                             TVS_EDITLABELS | TVS_NOHSCROLL | TVS_TRACKSELECT, 0, 0, 10, 10, g_side, nullptr, g_inst, nullptr);
    TreeView_SetExtendedStyle(g_tree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);
    SetWindowSubclass(g_tree, TreeProc, 1, 0);

    const KNOWNFOLDERID* ids[] = {&FOLDERID_Desktop, &FOLDERID_Downloads, &FOLDERID_Documents, &FOLDERID_Pictures, &FOLDERID_Videos, &FOLDERID_Music};
    const wchar_t* glyphs[] = {L"\uE7F4", L"\uE896", L"\uE8A5", L"\uEB9F", L"\uE714", L"\uE8D6"};
    for (int i = 0; i < 6; i++) {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*ids[i], 0, nullptr, &p))) g_knownGlyph[Lower(p)] = glyphs[i];
        CoTaskMemFree(p);
    }
    PinsLoad();
    FavLoad();
    for (int i = 0;; i++) {
        wstring v = IniGet(L"Recent", (L"r" + std::to_wstring(i)).c_str());
        if (v.empty()) break;
        size_t a = v.find(L'\t'), b = v.find(L'\t', a + 1);
        if (a == wstring::npos || b == wstring::npos) continue;
        Visit x{v.substr(0, a), _wtof(v.substr(a + 1, b - a - 1).c_str()), _wtoi64(v.substr(b + 1).c_str())};
        g_visits[Lower(x.path)] = x;
    }
    SidebarApplyTheme();
}

HWND SidebarHwnd() { return g_side; }
bool SidebarHasFocus() { HWND f = GetFocus(); return f && (f == g_side || IsChild(g_side, f)); }
void SidebarRefreshDrives() { RebuildSection(S_FOLDERS); UpdateUsage(); }
void SidebarFavsChanged() { if (g_tree) { RebuildSection(S_QUICK); RebuildSection(S_FAVS); RebuildSection(S_RECENT); } }

void SidebarApplyTheme() {
    const Theme& t = *g_theme;
    ThemeWindow(g_tree);
    TreeView_SetBkColor(g_tree, t.sidebar);
    TreeView_SetTextColor(g_tree, t.text);
    TreeView_SetLineColor(g_tree, t.border);
    TreeView_SetInsertMarkColor(g_tree, t.accent);
    SendMessageW(g_tree, WM_SETFONT, (WPARAM)g_font, FALSE);
    TreeView_SetItemHeight(g_tree, S(t.monoUI ? 24 : 28));
    TreeView_SetIndent(g_tree, S(14));
    if (t.icons == ICON_SYSTEM) {
        HIMAGELIST old = TreeView_SetImageList(g_tree, SysImageList(), TVSIL_NORMAL);
        if (old && old != SysImageList()) ImageList_Destroy(old);
    } else {
        TreeIconSlot(g_tree);
    }
    RebuildAll();
    FavManagerTheme();
    UpdateUsage();
    InvalidateRect(g_side, nullptr, TRUE);
}

void SidebarSetCurrent() {
    Pane* p = ActivePane();
    if (!p || !g_tree || !p->TabCount()) return;
    wstring path = p->InSearch() ? SearchRoot(p->Path()) : p->Path();
    wstring cur = Lower(path);
    if (cur != g_current) { g_current = cur; InvalidateRect(g_tree, nullptr, FALSE); }
    if (g_usage.root.empty() || !IsFsPath(path) || !IStarts(path, g_usage.root)) UpdateUsage();
}

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
    FavSave();  // group open / closed states
}

bool IsFavourite(const wstring& p) { return !p.empty() && IsFav(p); }

void AddFavourite(const wstring& p) {
    if (p.empty() || IsFav(p)) return;
    FavAdd(p);
    FavsChanged();
}

void ToggleFavourite(const wstring& p) {
    if (Fav* f = FavFindPath(p)) { FavRemove(f->id); FavsChanged(); return; }
    AddFavourite(p);
}

void TrackVisit(const wstring& p) {
    Visit& v = g_visits[Lower(p)];
    v.path = p;
    v.count += 1;
    v.time = time(nullptr);
    if (TopRecent(8) != g_lastTop) PostMessageW(g_side, WM_SB_REBUILD, S_RECENT, 0);
}

vector<wstring> RecentPaths(size_t n) {
    vector<const Visit*> v;
    for (auto& kv : g_visits) v.push_back(&kv.second);
    std::sort(v.begin(), v.end(), [](auto a, auto b) { return a->time > b->time; });
    vector<wstring> r;
    for (size_t i = 0; i < v.size() && r.size() < n; i++) r.push_back(v[i]->path);
    return r;
}

// folders dragged from a pane: onto a folder = move / copy into it; onto QUICK ACCESS = pin; onto FAVOURITES or a group = bookmark
bool SidebarDropTarget(POINT screen, wstring& dir, bool& pin) {
    HTREEITEM h = nullptr;
    Node* n = NodeAt(screen, &h);
    pin = false;
    g_pinGroup = -1;
    if (!n || (n->kind == K_HDR && n->sec != S_QUICK && n->sec != S_FAVS)) { TreeView_SelectDropTarget(g_tree, nullptr); return false; }
    TreeView_SelectDropTarget(g_tree, h);
    if (n->kind == K_HDR) { pin = true; g_pinGroup = n->sec == S_QUICK ? -2 : 0; return true; }
    if (n->kind == K_FAV && n->path.empty()) { pin = true; g_pinGroup = n->fav; return true; }
    dir = n->path;
    return true;
}

wstring SidebarPinText() { return g_pinGroup == -2 ? L"\u2605  Pin to Quick access" : L"\u2605  Add to Favourites"; }

void SidebarPinDropped(const vector<wstring>& paths) {
    bool favs = false;
    for (auto& p : paths) {
        if (!IsDir(p)) continue;
        if (g_pinGroup == -2) { if (!IsPinned(p)) TogglePin(p); }
        else if (!IsFav(p)) { FavAdd(p, std::max(0, g_pinGroup)); favs = true; }
    }
    if (favs) FavsChanged();
}

void SidebarClearDropHi() { if (g_tree) TreeView_SelectDropTarget(g_tree, nullptr); }
