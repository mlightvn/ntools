// Shared includes, messages and small helpers.
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#undef WINVER
#define WINVER 0x0A00
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <dbt.h>
#include <uxtheme.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

using std::wstring;
using std::vector;

#define APP_NAME L"nExplorer"

// Custom messages
enum {
    WM_APP_LISTED = WM_APP + 1,  // lParam = ListResult*
    WM_APP_DRIVEINFO,            // lParam = DriveInfo*
    WM_APP_FSCHANGE,             // wParam = watch token (sent to pane)
    WM_APP_OPDONE,               // lParam = vector<wstring>* affected dirs
    WM_APP_SUGGEST,              // lParam = Suggest* (sent to addrbar)
    WM_APP_ASK,                  // worker -> progress window, lParam = Ask*
    WM_APP_SELNAME,              // select name part in label edit
    WM_APP_STATUS,               // deferred status bar update (pane)
    WM_APP_EJECTED,              // lParam = eject result (drives.cpp)
    WM_APP_FOCUSLIST,            // focus the active pane's list (posted, after a control finishes its click)
    WM_APP_MESSAGE,              // lParam = wstring* to show in a message box (from worker threads)
};

extern "C" void WINAPI SHUpdateRecycleBinIcon(void);  // shell32 export, not declared in MinGW headers

extern HINSTANCE g_inst;
extern HWND g_main;
extern int g_dpi;
extern HFONT g_font, g_fontBold, g_iconFont, g_iconFontSmall;
inline int S(int v) { return MulDiv(v, g_dpi, 96); }

// ---- string / path helpers ----
inline wstring Lower(wstring s) { if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size()); return s; }
inline bool IEq(const wstring& a, const wstring& b) { return CompareStringOrdinal(a.c_str(), (int)a.size(), b.c_str(), (int)b.size(), TRUE) == CSTR_EQUAL; }
inline bool IStarts(const wstring& s, const wstring& p) { return s.size() >= p.size() && CompareStringOrdinal(s.c_str(), (int)p.size(), p.c_str(), (int)p.size(), TRUE) == CSTR_EQUAL; }
inline bool IsRoot(const wstring& p) { return p.size() == 3 && p[1] == L':' && p[2] == L'\\'; }
// Virtual locations: "" = This PC, "::bin" = Recycle Bin (all drives)
#define BIN_PATH L"::bin"
inline bool IsVirtual(const wstring& p) { return p.size() > 1 && p[0] == L':' && p[1] == L':'; }
inline bool IsFsPath(const wstring& p) { return !p.empty() && !IsVirtual(p); }  // a real folder
inline wstring Join(const wstring& dir, const wstring& name) {
    if (dir.empty()) return name;
    return dir.back() == L'\\' ? dir + name : dir + L"\\" + name;
}
inline wstring Parent(const wstring& p) {  // "" means "This PC"
    if (p.empty() || IsRoot(p)) return L"";
    size_t i = p.find_last_of(L'\\');
    if (i == wstring::npos || i < 2) return L"";
    wstring r = p.substr(0, i);
    if (r.size() == 2 && r[1] == L':') return r + L'\\';
    if (r.rfind(L"\\\\", 0) == 0 && r.find(L'\\', 2) == wstring::npos) return L"";  // "\\server"
    return r;
}
inline wstring FileName(const wstring& p) {
    if (IsRoot(p)) return p.substr(0, 2);
    size_t i = p.find_last_of(L'\\');
    return i == wstring::npos ? p : p.substr(i + 1);
}
inline wstring DisplayName(const wstring& path) {  // tab titles, tooltips
    if (path.empty()) return L"This PC";
    if (IsVirtual(path)) return L"Recycle Bin";
    return FileName(path);
}
inline bool IsDir(const wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
inline bool Exists(const wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
inline wstring ErrText(DWORD e) {
    wchar_t* buf = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, (LPWSTR)&buf, 0, nullptr);
    wstring s = buf ? buf : L"Error";
    if (buf) LocalFree(buf);
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L' ')) s.pop_back();
    return s;
}
inline wstring FmtSize(ULONGLONG n) { wchar_t b[64]; StrFormatByteSizeW((LONGLONG)n, b, 64); return b; }
inline wstring FmtTime(const FILETIME& ft) {
    if (!ft.dwLowDateTime && !ft.dwHighDateTime) return L"";
    SYSTEMTIME u, l; FileTimeToSystemTime(&ft, &u); SystemTimeToTzSpecificLocalTime(nullptr, &u, &l);
    wchar_t d[64], t[64];
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &l, nullptr, d, 64, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &l, nullptr, t, 64);
    return wstring(d) + L" " + t;
}
// Non-existing name in dir: "base (2).ext", or "base - Copy" when copySuffix.
wstring UniqueName(const wstring& dir, const wstring& name, bool copySuffix);
// Resolve text typed in address bar into an existing folder (+ file to select).
bool ResolveUserPath(wstring in, const wstring& cwd, wstring& dir, wstring& select);
