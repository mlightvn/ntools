// App-level shared state and services (implemented in main.cpp / drag.cpp / sidebar.cpp).
#pragma once
#include "common.h"

class Pane;
extern Pane* g_panes[3];
extern int g_paneCount, g_active;
extern double g_frac[3];
void SetPaneCount(int n);

Pane* ActivePane();
void SetActive(Pane* p);
void FocusNextPane(bool fromSidebar);
void RebuildAllColumns();
void Relayout();
void ToggleHidden();
void ThemeChanged();                   // re-apply fonts, colours and layout after a theme switch

// Global toolbar (toolbar.cpp)
void ToolbarCreate(HWND parent);
HWND ToolbarHwnd();
int ToolbarHeight();
void ToolbarRefresh();                 // re-layout (profile name / pane count changed)
void ToolbarFocusSearch();             // Ctrl+F / Ctrl+K
void ToolbarSyncSearch();              // show the active pane's query (or clear) after pane / tab changes

// INI (UTF-16 file next to the exe)
wstring IniGet(const wchar_t* sec, const wchar_t* key, const wchar_t* def = L"");
bool IniSet(const wchar_t* sec, const wchar_t* key, const wstring& val);  // false + GetLastError on failure
void IniClearSection(const wchar_t* sec);

// Items marked by Ctrl+X (drawn faded). Lower-case full paths.
extern std::unordered_set<wstring> g_cutSet;
void SetCutSet(const vector<wstring>& paths);
void ClearCutSet();

// Shell helpers
void OpenInShell(const wstring& path);
void OpenTerminal(const wstring& dir);
void ShowProperties(const wstring& path);
void ReleaseDrive(const wstring& root);   // panes showing this drive go back to This PC
wstring SevenZipExe();                    // 7zG.exe / 7z.exe of an installed 7-Zip, "" if none
// password: "" = none; encryptNames hides the file list too (-mhe)
void SevenZipAdd(const vector<wstring>& items, const wstring& archive, const wstring& password = L"", bool encryptNames = false);
bool IsArchive(const wstring& path);      // zip, 7z, rar, tar, gz ...
void ExtractArchives(const vector<wstring>& archives, bool toOwnFolder);  // async (7-Zip, or tar.exe)

// Dialogs (dialogs.cpp)
bool InputBox(const wchar_t* title, const wchar_t* prompt, wstring& value);
bool PasswordBox(const wchar_t* title, wstring& password, bool& encryptNames);
wstring ArchiveName(const vector<wstring>& items, const wchar_t* ext);  // unique "<first item>.ext" next to the items

// External drives (drives.cpp)
bool IsExternalDrive(const wstring& root);
void EjectDrive(const wstring& root);     // async; reports back via WM_APP_EJECTED
void OnEjected(LPARAM l);

// Drag & drop (drag.cpp)
bool DragActive();
void DragBegin(Pane* src, const vector<wstring>& paths);
void DragUpdate();
void DragEnd(bool drop);
void DropExternal(HDROP h);

// Profiles: saved pane / tab layouts (profiles.cpp)
void ProfilesInit();                  // apply the current profile (call once, after panes exist)
wstring ProfileLabel();                // current profile name (+ unsaved marker)
void ProfileMenu(const RECT* anchor);  // screen rect of the toolbar button; nullptr: near the active pane
void ProfileSave();
void ProfileTick();

// Sidebar (sidebar.cpp)
void SidebarCreate(HWND parent);
HWND SidebarHwnd();
void SidebarRefreshDrives();
void SidebarSave();
bool IsFavourite(const wstring& path);
void ToggleFavourite(const wstring& path);
void AddFavourite(const wstring& path);
void TrackVisit(const wstring& path);
vector<wstring> RecentPaths(size_t n);
bool SidebarDropTarget(POINT screen, wstring& dir, bool& pin);
void SidebarClearDropHi();
bool SidebarHasFocus();
void SidebarSetCurrent();             // highlight the active pane's folder, update the disk usage
void SidebarFavsChanged();            // Quick access / Favourites changed: rebuild those sections
wstring SidebarPinText();             // drag tooltip when dropping onto QUICK ACCESS / FAVOURITES
void SidebarPinDropped(const vector<wstring>& paths);
void SidebarApplyTheme();
