#include "fs.h"
#include <thread>
#include <mutex>
#include <sddl.h>

bool g_showHidden = false;

// ---------- icon / type cache (shared by UI and worker threads) ----------
static std::mutex g_cacheMx;
static std::unordered_map<wstring, std::pair<int, wstring>> g_extCache;
static int g_folderIcon = -1;

static std::pair<int, wstring> ExtInfo(const wstring& ext) {
    std::lock_guard<std::mutex> lk(g_cacheMx);
    auto it = g_extCache.find(ext);
    if (it != g_extCache.end()) return it->second;
    SHFILEINFOW sfi{};
    wstring fake = L"x" + ext;
    SHGetFileInfoW(fake.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                   SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_TYPENAME);
    wstring type = sfi.szTypeName;
    if (type.empty()) type = ext.empty() ? L"File" : ext.substr(1) + L" File";
    return g_extCache[ext] = {sfi.iIcon, type};
}

HIMAGELIST SysImageList() {
    SHFILEINFOW sfi{};
    return (HIMAGELIST)SHGetFileInfoW(L"x.txt", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi),
                                      SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
}

int FolderIcon() {
    if (g_folderIcon < 0) {
        SHFILEINFOW sfi{};
        SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof(sfi),
                       SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
        g_folderIcon = sfi.iIcon;
    }
    return g_folderIcon;
}

int DriveIcon(UINT t) {
    SHSTOCKICONID id = SIID_DRIVEFIXED;
    switch (t) {
    case DRIVE_REMOVABLE: id = SIID_DRIVEREMOVE; break;
    case DRIVE_REMOTE: id = SIID_DRIVENET; break;
    case DRIVE_CDROM: id = SIID_DRIVECD; break;
    case DRIVE_RAMDISK: id = SIID_DRIVERAM; break;
    }
    SHSTOCKICONINFO sii{sizeof(sii)};
    SHGetStockIconInfo(id, SHGSI_SYSICONINDEX, &sii);
    return sii.iSysImageIndex;
}

int PathIcon(const wstring& path) {
    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_SYSICONINDEX | SHGFI_SMALLICON)) return sfi.iIcon;
    return FolderIcon();
}

int IconOf(const Item& it) {
    if (it.dtype) return DriveIcon(it.dtype);
    if (it.attr & FILE_ATTRIBUTE_DIRECTORY) return FolderIcon();
    return ExtInfo(Lower(PathFindExtensionW(it.name.c_str()))).first;
}

wstring TypeOf(const Item& it) {
    if (it.dtype) return DriveTypeText(it.dtype);
    if (it.attr & FILE_ATTRIBUTE_DIRECTORY) return L"File folder";
    return ExtInfo(Lower(PathFindExtensionW(it.name.c_str()))).second;
}

wstring DriveTypeText(UINT t) {
    switch (t) {
    case DRIVE_REMOVABLE: return L"Removable Disk";
    case DRIVE_REMOTE: return L"Network Drive";
    case DRIVE_CDROM: return L"CD Drive";
    case DRIVE_RAMDISK: return L"RAM Disk";
    default: return L"Local Disk";
    }
}

wstring DriveDisplay(const Item& it) {
    wstring l = it.label.empty() ? DriveTypeText(it.dtype) : it.label;
    return l + L" (" + it.name.substr(0, 2) + L")";
}

// ---------- sorting ----------
static int Cmp3(ULONGLONG a, ULONGLONG b) { return a < b ? -1 : a > b ? 1 : 0; }
static int CmpFt(const FILETIME& a, const FILETIME& b) { return CompareFileTime(&a, &b); }

void SortItems(vector<Item>& v, int col, bool asc) {
    std::unordered_map<wstring, wstring> types;  // precompute for type sort
    if (col == C_TYPE)
        for (auto& it : v) if (!it.isDir()) { auto e = Lower(PathFindExtensionW(it.name.c_str())); if (!types.count(e)) types[e] = TypeOf(it); }
    auto key = [&](const Item& it) { return types[Lower(PathFindExtensionW(it.name.c_str()))]; };
    std::stable_sort(v.begin(), v.end(), [&](const Item& a, const Item& b) {
        if (a.isDir() != b.isDir()) return a.isDir();  // folders first, regardless of direction
        int r = 0;
        switch (col) {
        case C_TYPE: if (!a.isDir()) r = StrCmpLogicalW(key(a).c_str(), key(b).c_str()); break;
        case C_MTIME: r = CmpFt(a.mt, b.mt); break;
        case C_CTIME: r = CmpFt(a.ct, b.ct); break;
        case C_ATIME: r = CmpFt(a.at, b.at); break;
        case C_SIZE: case C_TOTAL: r = Cmp3(a.size, b.size); break;
        case C_FREE: r = Cmp3(a.free, b.free); break;
        case C_EXT: r = StrCmpLogicalW(PathFindExtensionW(a.name.c_str()), PathFindExtensionW(b.name.c_str())); break;
        case C_ATTR: r = Cmp3(a.attr, b.attr); break;
        case C_DTYPE: r = Cmp3(a.dtype, b.dtype); break;
        case C_ORIGIN: case C_FOLDER: r = StrCmpLogicalW(a.label.c_str(), b.label.c_str()); break;
        case C_DELETED: r = CmpFt(a.mt, b.mt); break;
        }
        if (r == 0) r = StrCmpLogicalW(a.name.c_str(), b.name.c_str());
        return asc ? r < 0 : r > 0;
    });
}

// ---------- listing ----------
void ListAsync(HWND notify, int tabId, int gen, const wstring& path, int sortCol, bool asc) {
    bool hidden = g_showHidden;
    std::thread([=] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        auto r = new ListResult{tabId, gen, path};
        if (path.empty()) {
            r->items = ListDrives();
        } else if (IsBin(path)) {
            r->items = ListRecycleBin();
            SortItems(r->items, sortCol, asc);
        } else {
            WIN32_FIND_DATAW fd;
            HANDLE f = FindFirstFileExW(Join(path, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                                        nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (f == INVALID_HANDLE_VALUE) {
                r->err = GetLastError();
                if (r->err == ERROR_FILE_NOT_FOUND) r->err = 0;  // empty drive root
            } else {
                r->items.reserve(256);
                do {
                    const wchar_t* n = fd.cFileName;
                    if (n[0] == L'.' && (!n[1] || (n[1] == L'.' && !n[2]))) continue;
                    if (!hidden && (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
                    Item it;
                    it.name = n;
                    it.attr = fd.dwFileAttributes;
                    it.size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                    it.ct = fd.ftCreationTime; it.mt = fd.ftLastWriteTime; it.at = fd.ftLastAccessTime;
                    r->items.push_back(std::move(it));
                } while (FindNextFileW(f, &fd));
                FindClose(f);
            }
            SortItems(r->items, sortCol, asc);
        }
        if (!PostMessageW(notify, WM_APP_LISTED, 0, (LPARAM)r)) delete r;
        CoUninitialize();
    }).detach();
}

vector<Item> ListDrives() {
    vector<Item> v;
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; i++) {
        if (!(mask & (1u << i))) continue;
        Item it;
        it.name = wstring(1, wchar_t(L'A' + i)) + L":\\";
        it.dtype = GetDriveTypeW(it.name.c_str());
        if (it.dtype == DRIVE_NO_ROOT_DIR || it.dtype == DRIVE_UNKNOWN) continue;
        it.attr = FILE_ATTRIBUTE_DIRECTORY;
        if (it.dtype == DRIVE_REMOTE) {  // show "\\server\share" until the label arrives
            wchar_t remote[MAX_PATH]; DWORD n = MAX_PATH;
            wstring dev = it.name.substr(0, 2);
            if (WNetGetConnectionW(dev.c_str(), remote, &n) == NO_ERROR) it.label = remote;
        }
        v.push_back(it);
    }
    return v;
}

wstring UserSid() {
    static wstring sid;
    if (!sid.empty()) return sid;
    HANDLE tok;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return L"";
    BYTE buf[256];
    DWORD n = 0;
    if (GetTokenInformation(tok, TokenUser, buf, sizeof(buf), &n)) {
        LPWSTR s = nullptr;
        if (ConvertSidToStringSidW(((TOKEN_USER*)buf)->User.Sid, &s)) { sid = s; LocalFree(s); }
    }
    CloseHandle(tok);
    return sid;
}

wstring BinInfoPath(const wstring& real) { return Join(Parent(real), L"$I" + FileName(real).substr(2)); }

// Each deleted item is a pair: $Rxxxxxx(.ext) = the data, $Ixxxxxx(.ext) = record with original path,
// size and deletion time (v1: fixed 260-char path, v2: length-prefixed path).
vector<Item> ListRecycleBin() {
    vector<Item> v;
    wstring sid = UserSid();
    DWORD mask = GetLogicalDrives();
    for (int d = 0; d < 26 && !sid.empty(); d++) {
        if (!(mask & (1u << d))) continue;
        wstring root = wstring(1, wchar_t(L'A' + d)) + L":\\";
        UINT type = GetDriveTypeW(root.c_str());
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;  // never touch network drives here
        wstring bin = root + L"$Recycle.Bin\\" + sid;
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileExW(Join(bin, L"$I*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
        if (f == INVALID_HANDLE_VALUE) continue;
        do {
            wstring info = Join(bin, fd.cFileName);
            HANDLE h = CreateFileW(info.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) continue;
            BYTE buf[4096] = {};
            DWORD got = 0;
            ReadFile(h, buf, sizeof(buf) - 2, &got, nullptr);
            CloseHandle(h);
            if (got < 28) continue;
            ULONGLONG ver = *(ULONGLONG*)buf;
            wstring orig;
            if (ver == 1) orig = wstring((const wchar_t*)(buf + 24));
            else if (ver == 2) {
                DWORD len = *(DWORD*)(buf + 24);
                if (28 + len * 2 > got || len == 0) continue;
                orig.assign((const wchar_t*)(buf + 28), len - 1);
            } else continue;
            Item it;
            it.real = Join(bin, L"$R" + wstring(fd.cFileName).substr(2));
            WIN32_FILE_ATTRIBUTE_DATA a;
            if (orig.empty() || !GetFileAttributesExW(it.real.c_str(), GetFileExInfoStandard, &a)) continue;  // orphan record
            it.name = FileName(orig);
            it.label = Parent(orig);
            it.attr = a.dwFileAttributes & ~FILE_ATTRIBUTE_HIDDEN;
            it.size = *(ULONGLONG*)(buf + 8);
            it.mt = *(FILETIME*)(buf + 16);  // deletion time
            v.push_back(std::move(it));
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    return v;
}

void DriveInfoAsync(HWND notify, const vector<Item>& drives) {
    for (auto& d : drives) {
        wstring root = d.name;
        UINT type = d.dtype;
        std::thread([=] {  // one thread per drive: a dead network drive must not block others
            auto r = new DriveInfo{root};
            wchar_t label[MAX_PATH] = L"";
            if (GetVolumeInformationW(root.c_str(), label, MAX_PATH, nullptr, nullptr, nullptr, nullptr, 0)) r->label = label;
            if (type == DRIVE_REMOTE && r->label.empty()) {
                wchar_t remote[MAX_PATH]; DWORD n = MAX_PATH;
                if (WNetGetConnectionW(root.substr(0, 2).c_str(), remote, &n) == NO_ERROR) r->label = remote;
            }
            ULARGE_INTEGER fr{}, tot{};
            if (GetDiskFreeSpaceExW(root.c_str(), &fr, &tot, nullptr)) { r->free = fr.QuadPart; r->total = tot.QuadPart; }
            if (!PostMessageW(notify, WM_APP_DRIVEINFO, 0, (LPARAM)r)) delete r;
        }).detach();
    }
}

vector<wstring> ListSubdirs(const wstring& dir, const wstring& prefix, size_t max) {
    vector<wstring> v;
    if (dir.empty()) {
        for (auto& d : ListDrives()) if (IStarts(d.name, prefix)) v.push_back(d.name);
        return v;
    }
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileExW(Join(dir, prefix + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchLimitToDirectories,
                                nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (f == INVALID_HANDLE_VALUE) return v;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        const wchar_t* n = fd.cFileName;
        if (n[0] == L'.' && (!n[1] || (n[1] == L'.' && !n[2]))) continue;
        if (!g_showHidden && (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
        v.push_back(n);
    } while (v.size() < max && FindNextFileW(f, &fd));
    FindClose(f);
    std::sort(v.begin(), v.end(), [](const wstring& a, const wstring& b) { return StrCmpLogicalW(a.c_str(), b.c_str()) < 0; });
    return v;
}

// ---------- watcher ----------
void Watcher::Watch(HWND target, const wstring& path) {
    Stop();
    UINT t = ++token_;
    if (path.empty()) return;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE ev;
    DuplicateHandle(GetCurrentProcess(), stop_, GetCurrentProcess(), &ev, 0, FALSE, DUPLICATE_SAME_ACCESS);
    std::thread([=] {
        HANDLE n = FindFirstChangeNotificationW(path.c_str(), FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE |
            FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES);
        if (n != INVALID_HANDLE_VALUE) {
            HANDLE hs[2] = {ev, n};
            while (WaitForMultipleObjects(2, hs, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
                PostMessageW(target, WM_APP_FSCHANGE, t, 0);
                if (WaitForSingleObject(ev, 250) == WAIT_OBJECT_0) break;  // throttle bursts
                if (!FindNextChangeNotification(n)) break;
            }
            FindCloseChangeNotification(n);
        }
        CloseHandle(ev);
    }).detach();
}

void Watcher::Stop() {
    if (stop_) { SetEvent(stop_); CloseHandle(stop_); stop_ = nullptr; }
}

// ---------- path helpers ----------
wstring UniqueName(const wstring& dir, const wstring& name, bool copySuffix) {
    if (!Exists(Join(dir, name)) && !copySuffix) return name;
    wstring base = name, ext;
    DWORD a = GetFileAttributesW(Join(dir, name).c_str());
    bool isDir = a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
    if (!isDir) {
        size_t dot = name.find_last_of(L'.');
        if (dot != wstring::npos && dot > 0) { base = name.substr(0, dot); ext = name.substr(dot); }
    }
    for (int i = 1; i < 10000; i++) {
        wstring n;
        if (copySuffix) n = base + L" - Copy" + (i > 1 ? L" (" + std::to_wstring(i) + L")" : L"") + ext;
        else n = base + L" (" + std::to_wstring(i + 1) + L")" + ext;
        if (!Exists(Join(dir, n))) return n;
    }
    return name;
}

bool ResolveUserPath(wstring s, const wstring& cwd, wstring& dir, wstring& select) {
    dir.clear(); select.clear();
    while (!s.empty() && (iswspace(s.back()) || s.back() == L'"')) s.pop_back();
    size_t b = 0;
    while (b < s.size() && (iswspace(s[b]) || s[b] == L'"')) b++;
    s = s.substr(b);
    if (s.empty() || IEq(s, L"This PC")) return true;
    if (IEq(s, L"Recycle Bin") || s == BIN_PATH) { dir = BIN_PATH; return true; }
    if (s.rfind(L"file:", 0) == 0) {  // file:///D:/x
        wchar_t p[MAX_PATH * 4]; DWORD n = ARRAYSIZE(p);
        if (SUCCEEDED(PathCreateFromUrlW(s.c_str(), p, &n, 0))) s = p;
    }
    std::replace(s.begin(), s.end(), L'/', L'\\');
    wchar_t ex[32768];
    if (ExpandEnvironmentStringsW(s.c_str(), ex, ARRAYSIZE(ex))) s = ex;
    if (s.size() == 2 && s[1] == L':') s += L'\\';
    if (PathIsRelativeW(s.c_str())) {
        if (cwd.empty()) return false;
        s = Join(cwd, s);
    }
    wchar_t full[32768];
    if (GetFullPathNameW(s.c_str(), ARRAYSIZE(full), full, nullptr)) s = full;
    while (s.size() > 3 && s.back() == L'\\') s.pop_back();
    DWORD a = GetFileAttributesW(s.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return false;
    if (a & FILE_ATTRIBUTE_DIRECTORY) { dir = s; return true; }
    dir = Parent(s); select = FileName(s);
    return true;
}
