// Profiles: named pane / tab layouts. A layout is saved only on demand (Ctrl+S);
// the app always opens the current profile's saved layout (all panes at This PC if never saved).
#include "app.h"
#include "pane.h"

namespace {

struct Layout {
    int panes = 2, active = 0;
    double frac[3] = {1, 1, 1};
    vector<wstring> tabs[3];  // "" = This PC
    int cur[3] = {0, 0, 0};
};

vector<wstring> g_names;
wstring g_cur;
wstring g_saved;  // serialized layout of the current profile as last saved
bool g_dirty = false;

wstring Sec(const wstring& name) { return L"Profile." + name; }
wstring Key(const wchar_t* k, int i) { return k + std::to_wstring(i); }
wstring Num(double d) { wchar_t b[32]; swprintf(b, 32, L"%.3f", d); return b; }

void Normalize(Layout& L) {
    L.panes = std::max(1, std::min(3, L.panes));
    L.active = std::max(0, std::min(L.active, L.panes - 1));
    for (int i = 0; i < 3; i++) {
        if (L.tabs[i].empty()) L.tabs[i].push_back(L"");
        L.cur[i] = std::max(0, std::min(L.cur[i], (int)L.tabs[i].size() - 1));
    }
}

wstring JoinTabs(const vector<wstring>& v) {
    wstring s;
    for (size_t i = 0; i < v.size(); i++) s += (i ? L"|" : L"") + (v[i].empty() ? wstring(L"*") : v[i]);
    return s;
}

vector<wstring> SplitTabs(const wstring& s) {
    vector<wstring> v;
    size_t b = 0;
    while (!s.empty() && b <= s.size()) {
        size_t e = s.find(L'|', b);
        wstring p = s.substr(b, e == wstring::npos ? wstring::npos : e - b);
        v.push_back(p == L"*" ? L"" : p);
        if (e == wstring::npos) break;
        b = e + 1;
    }
    return v;
}

wstring ToStr(const Layout& L) {
    wstring s = L"panes=" + std::to_wstring(L.panes) + L"\nactive=" + std::to_wstring(L.active);
    for (int i = 0; i < 3; i++)
        s += L"\nfrac" + std::to_wstring(i) + L"=" + Num(L.frac[i]) + L"\np" + std::to_wstring(i) + L"=" + JoinTabs(L.tabs[i]) +
             L"\nc" + std::to_wstring(i) + L"=" + std::to_wstring(L.cur[i]);
    return s;
}

Layout Read(const wstring& name) {
    Layout L;
    wstring sec = Sec(name);
    if (!IniGet(sec.c_str(), L"panes").empty()) {
        L.panes = _wtoi(IniGet(sec.c_str(), L"panes").c_str());
        L.active = _wtoi(IniGet(sec.c_str(), L"active", L"0").c_str());
        for (int i = 0; i < 3; i++) {
            L.frac[i] = std::max(0.1, _wtof(IniGet(sec.c_str(), Key(L"frac", i).c_str(), L"1").c_str()));
            L.tabs[i] = SplitTabs(IniGet(sec.c_str(), Key(L"p", i).c_str()));
            L.cur[i] = _wtoi(IniGet(sec.c_str(), Key(L"c", i).c_str(), L"0").c_str());
        }
    }
    Normalize(L);
    return L;
}

// returns false (GetLastError set) if the INI can't be written, e.g. the drive is locked or read-only
bool Write(const wstring& name, const Layout& L) {
    wstring sec = Sec(name);
    if (!IniSet(sec.c_str(), L"panes", std::to_wstring(L.panes))) return false;
    IniClearSection(sec.c_str());
    IniSet(sec.c_str(), L"panes", std::to_wstring(L.panes));
    IniSet(sec.c_str(), L"active", std::to_wstring(L.active));
    for (int i = 0; i < 3; i++) {
        IniSet(sec.c_str(), Key(L"frac", i).c_str(), Num(L.frac[i]));
        IniSet(sec.c_str(), Key(L"p", i).c_str(), JoinTabs(L.tabs[i]));
        IniSet(sec.c_str(), Key(L"c", i).c_str(), std::to_wstring(L.cur[i]));
    }
    return true;
}

Layout Capture() {
    Layout L;
    L.panes = g_paneCount;
    L.active = g_active;
    for (int i = 0; i < 3; i++) {
        L.frac[i] = g_frac[i];
        for (int t = 0; t < g_panes[i]->TabCount(); t++) L.tabs[i].push_back(g_panes[i]->TabPath(t));
        L.cur[i] = g_panes[i]->CurTab();
    }
    Normalize(L);
    return L;
}

void Apply(const Layout& L) {
    g_paneCount = L.panes;
    for (int i = 0; i < 3; i++) {
        g_frac[i] = L.frac[i];
        g_panes[i]->ResetTabs(L.tabs[i], L.cur[i]);
    }
    g_active = L.active;
    Relayout();
    for (int i = 0; i < 3; i++) g_panes[i]->PaintBorder();
    ActivePane()->FocusList();
}

void SaveList() {
    wstring s;
    for (size_t i = 0; i < g_names.size(); i++) s += (i ? L"|" : L"") + g_names[i];
    IniSet(L"Profiles", L"list", s);
    IniSet(L"Profiles", L"current", g_cur);
}

void UpdateUi() {
    g_dirty = ToStr(Capture()) != g_saved;
    wstring title = wstring(APP_NAME) + L"  —  " + g_cur + (g_dirty ? L"  ●" : L"");
    SetWindowTextW(g_main, title.c_str());
    ToolbarRefresh();
}

int Find(const wstring& name) {
    for (size_t i = 0; i < g_names.size(); i++) if (IEq(g_names[i], name)) return (int)i;
    return -1;
}

// trims and checks a new profile name; `except` is the name being renamed
bool Valid(wstring& name, const wstring& except) {
    while (!name.empty() && iswspace(name.back())) name.pop_back();
    while (!name.empty() && iswspace(name[0])) name.erase(0, 1);
    const wchar_t* err = nullptr;
    if (name.empty()) err = L"Please enter a name.";
    else if (name.find_first_of(L"|[]=") != wstring::npos) err = L"A profile name can't contain | [ ] =";
    else if (name.size() > 40) err = L"The name is too long (max 40 characters).";
    else if (Find(name) >= 0 && !IEq(name, except)) err = L"A profile with this name already exists.";
    if (err) MessageBoxW(g_main, err, L"Profile", MB_ICONWARNING);
    return !err;
}

// ---------- actions ----------
void SwitchTo(const wstring& name) {
    if (IEq(name, g_cur)) return;
    UpdateUi();
    if (g_dirty) {
        wstring q = L"Profile \"" + g_cur + L"\" has unsaved changes.\n\nSave them before switching?";
        int r = MessageBoxW(g_main, q.c_str(), L"Profile", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (r == IDCANCEL) return;
        if (r == IDYES) ProfileSave();
    }
    g_cur = name;
    Layout L = Read(g_cur);
    Apply(L);
    g_saved = ToStr(L);
    SaveList();
    UpdateUi();
}

void SaveAs() {
    wstring name = L"Profile " + std::to_wstring(g_names.size() + 1);
    do {
        if (!InputBox(L"Save as new profile", L"Name of the new profile:", name)) return;
    } while (!Valid(name, L""));
    g_names.push_back(name);
    g_cur = name;
    SaveList();
    ProfileSave();
}

void Rename() {
    wstring name = g_cur;
    do {
        if (!InputBox(L"Rename profile", L"New name:", name)) return;
    } while (!Valid(name, g_cur));
    if (name == g_cur) return;
    if (!IniGet(Sec(g_cur).c_str(), L"panes").empty()) Write(name, Read(g_cur));
    IniClearSection(Sec(g_cur).c_str());
    g_names[Find(g_cur)] = name;
    g_cur = name;
    SaveList();
    UpdateUi();
}

void Delete() {
    if (g_names.size() <= 1) return;
    wstring q = L"Delete profile \"" + g_cur + L"\"?";
    if (MessageBoxW(g_main, q.c_str(), L"Profile", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) return;
    IniClearSection(Sec(g_cur).c_str());
    g_names.erase(g_names.begin() + Find(g_cur));
    g_cur = g_names[0];
    Layout L = Read(g_cur);
    Apply(L);
    g_saved = ToStr(L);
    SaveList();
    UpdateUi();
}

}  // namespace

void ProfilesInit() {
    for (auto& n : SplitTabs(IniGet(L"Profiles", L"list")))
        if (!n.empty() && Find(n) < 0) g_names.push_back(n);
    if (g_names.empty()) g_names.push_back(L"Default");
    g_cur = IniGet(L"Profiles", L"current");
    if (Find(g_cur) < 0) g_cur = g_names[0];
    Layout L = Read(g_cur);
    Apply(L);
    g_saved = ToStr(L);
    SaveList();
    UpdateUi();
}

void ProfileSave() {
    Layout L = Capture();
    if (!Write(g_cur, L)) {  // keep the unsaved marker and tell the user why
        wstring msg = L"Could not save profile \"" + g_cur + L"\".\n\n" + ErrText(GetLastError()) + L"\n\nSettings file:  nexplorer.ini (next to nexplorer.exe)";
        MessageBoxW(g_main, msg.c_str(), L"Profile", MB_ICONERROR);
        return;
    }
    g_saved = ToStr(L);
    UpdateUi();
}

void ProfileTick() {
    if (!IsWindowVisible(g_main)) return;
    if ((ToStr(Capture()) != g_saved) != g_dirty) UpdateUi();
}

wstring ProfileLabel() { return g_cur + (g_dirty ? L"  ●" : L""); }

void ProfileMenu(const RECT* anchor) {
    UpdateUi();
    enum { M_SAVE = 1, M_SAVEAS, M_RENAME, M_DELETE, M_RESET, M_FIRST = 100 };
    HMENU m = CreatePopupMenu();
    for (size_t i = 0; i < g_names.size(); i++)
        AppendMenuW(m, MF_STRING | (IEq(g_names[i], g_cur) ? MF_CHECKED : 0), M_FIRST + i, g_names[i].c_str());
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_SAVE, g_dirty ? L"Save profile  ●\tCtrl+S" : L"Save profile\tCtrl+S");
    AppendMenuW(m, MF_STRING, M_SAVEAS, L"Save as new profile…");
    AppendMenuW(m, MF_STRING, M_RENAME, L"Rename profile…");
    AppendMenuW(m, MF_STRING | (g_names.size() > 1 ? 0 : MF_GRAYED), M_DELETE, L"Delete profile");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_RESET, L"Reset layout to This PC");
    POINT pt;
    if (anchor) pt = {anchor->right, anchor->bottom};
    else { RECT r; GetWindowRect(ActivePane()->hwnd, &r); pt = {r.left + S(20), r.top + S(60)}; }
    int c = TrackPopupMenu(m, TPM_RETURNCMD | (anchor ? TPM_RIGHTALIGN : 0), pt.x, pt.y, 0, g_main, nullptr);
    DestroyMenu(m);
    switch (c) {
    case 0: break;
    case M_SAVE: ProfileSave(); break;
    case M_SAVEAS: SaveAs(); break;
    case M_RENAME: Rename(); break;
    case M_DELETE: Delete(); break;
    case M_RESET: { Layout L; Normalize(L); Apply(L); UpdateUi(); break; }
    default:
        if (c >= M_FIRST && c < M_FIRST + (int)g_names.size()) SwitchTo(g_names[c - M_FIRST]);
    }
    ActivePane()->FocusList();
}
