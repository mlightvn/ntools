// A pane: tab strip + toolbar + address bar + virtual details list + status line.
#pragma once
#include "fs.h"
#include "addrbar.h"

struct Tab {
    int id;
    wstring path;                 // "" = This PC
    vector<Item> items;
    vector<wstring> back, fwd;
    int sortCol = C_NAME;
    bool asc = true;
    int gen = 0;
    bool loading = false;
    DWORD err = 0;
    // view state restored after (re)listing or tab switch
    vector<wstring> selNames;
    wstring focusName;
    int top = 0;
};

class Pane {
public:
    HWND hwnd = nullptr, list = nullptr, tabs = nullptr;
    AddrBar addr;

    void Create(HWND parent, int index);
    Tab* T() { return tabs_[cur_].get(); }
    const wstring& Path() { return T()->path; }
    int TabCount() const { return (int)tabs_.size(); }
    int CurTab() const { return cur_; }
    wstring TabPath(int i) { return tabs_[i]->path; }

    void NewTab(const wstring& path, bool activate = true);
    void ResetTabs(const vector<wstring>& paths, int cur);  // replace all tabs (profile switch)
    void CloseTab(int i);
    void SwitchTab(int i);
    void Navigate(const wstring& path, bool hist = true, const wstring& select = L"");
    void Refresh();
    void GoBack();
    void GoForward();
    void GoUp();
    void FocusList() { SetFocus(list); }
    void RebuildColumns();
    void OnDrivesChanged();
    void Redraw() { InvalidateRect(list, nullptr, FALSE); }
    void PaintBorder() { InvalidateRect(hwnd, nullptr, FALSE); }
    void UpdateStar();

    bool HandleKey(const MSG& m);   // keys while the list has focus
    bool DropTarget(POINT screen, HWND under, wstring& dir);
    void ClearDropHi();
    vector<wstring> Selected();

private:
    vector<std::unique_ptr<Tab>> tabs_;
    int cur_ = 0, index_ = 0;
    HWND plus_ = nullptr, btn_[7]{}, status_ = nullptr, tip_ = nullptr;
    Watcher watcher_;
    vector<int> cols_;
    int colsMode_ = -1;            // 0 files, 1 drives
    int dropHi_ = -1, hoverTab_ = -1;
    DWORD hoverTick_ = 0;
    bool editing_ = false, refreshPending_ = false, statusPending_ = false;
    int closeHot_ = -1;            // tab whose "×" is under the mouse
    bool tabTracking_ = false;

    bool CloseRect(int i, RECT* r);
    int CloseHit(POINT pt);
    void TabMenu(int i, POINT screen);

    void Layout();
    void SaveView();
    void ShowItems(bool restore);
    void UpdateStatus();
    void UpdateTabs();
    void Load();
    void OpenItems(bool newTab);
    void Copy(bool cut, bool link);
    void CopyPath();
    void Paste(const wstring& dir);
    void Delete(bool permanent);
    void NewFolder(bool file);
    void SelectAll();
    void ContextMenu(POINT pt, bool keyboard);
    wstring ItemPath(int i);
    void OnNotify(NMHDR* nm, LRESULT& res);
    void DrawButton(DRAWITEMSTRUCT* d);
    LRESULT Proc(UINT m, WPARAM w, LPARAM l);
    static LRESULT CALLBACK StaticProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK TabsProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
};
