// Address bar: clickable breadcrumb, edit mode with folder suggestions.
#pragma once
#include "common.h"

class Pane;

class AddrBar {
public:
    HWND hwnd = nullptr;
    void Create(HWND parent, Pane* owner);
    void SetPath(const wstring& p);
    void BeginEdit();
    bool Editing() const { return edit_ != nullptr; }
    bool DropTarget(POINT screen, wstring& dir);
    void ClearDropHi();

private:
    struct Seg { wstring text, path; RECT r{}, arrow{}; bool more = false; };
    Pane* owner_ = nullptr;
    HWND edit_ = nullptr, sug_ = nullptr;
    wstring path_;
    vector<Seg> segs_;
    vector<wstring> hidden_;  // collapsed leading segments ("«")
    RECT drop_{};
    int hot_ = -1, hotArrow_ = -1, dropHi_ = -1;
    bool err_ = false, tracking_ = false;
    UINT sugReq_ = 0;

    void Layout(HDC dc);
    void Paint(HDC dc);
    void Click(POINT pt);
    int HitSeg(POINT pt, bool* arrow);
    void Commit();
    void EndEdit();
    void RequestSuggest();
    void ShowSuggest(const vector<wstring>& v);
    void HideSuggest();
    void PickSuggest(int i, bool commit);
    void FolderMenu(const Seg& s);
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK SugProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
};
