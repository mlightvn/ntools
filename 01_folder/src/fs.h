// File system: items, async listing, drives, icons/types, change watcher.
#pragma once
#include "common.h"

enum Col { C_NAME, C_TYPE, C_MTIME, C_SIZE, C_CTIME, C_ATIME, C_EXT, C_ATTR, C_FILECOLS,
           C_DTYPE = C_FILECOLS, C_FREE, C_TOTAL, C_ORIGIN, C_DELETED, C_FOLDER, C_ALL };

struct Item {
    wstring name;            // file name, or "C:\" for drives
    wstring label;           // drive label; Recycle Bin: original folder
    wstring real;            // Recycle Bin only: the $R... item on disk
    wstring folder;          // search results: the folder that holds the item (label = folder relative to the search root)
    DWORD attr = 0;
    ULONGLONG size = 0;      // drives: total bytes
    ULONGLONG free = 0;      // drives only
    FILETIME ct{}, mt{}, at{};
    UINT dtype = 0;          // GetDriveType value, 0 = not a drive
    bool isDir() const { return dtype || (attr & FILE_ATTRIBUTE_DIRECTORY); }
};

struct ListResult { int tabId, gen; wstring path; vector<Item> items; DWORD err = 0; };
struct DriveInfo { wstring root, label; ULONGLONG free = 0, total = 0; };

extern bool g_showHidden;

void ListAsync(HWND notify, int tabId, int gen, const wstring& path, int sortCol, bool asc);
vector<Item> ListDrives();                       // fast; details come via WM_APP_DRIVEINFO
vector<Item> ListRecycleBin();                   // items of <drive>\$Recycle.Bin\<SID> on local drives
wstring BinInfoPath(const wstring& real);        // the $I record that belongs to a $R item
wstring UserSid();
void DriveInfoAsync(HWND notify, const vector<Item>& drives);
vector<wstring> ListSubdirs(const wstring& dir, const wstring& prefix, size_t max);
void SortItems(vector<Item>& v, int col, bool asc);

HIMAGELIST SysImageList();
int IconOf(const Item& it);
int FolderIcon();
int DriveIcon(UINT dtype);
int PathIcon(const wstring& path);               // real icon (touches disk) for known folders
wstring TypeOf(const Item& it);
wstring DriveTypeText(UINT t);
wstring DriveDisplay(const Item& it);

// Watches one directory; posts WM_APP_FSCHANGE(token) to target, throttled.
class Watcher {
    HANDLE stop_ = nullptr;
    UINT token_ = 0;
public:
    UINT Token() const { return token_; }
    void Watch(HWND target, const wstring& path);
    void Stop();
    ~Watcher() { Stop(); }
};
