#include "fileops.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <cstdint>
#include <zlib.h>

namespace {

struct Job { OpType type; vector<wstring> src; wstring dst; };

std::mutex g_mx;
std::condition_variable g_cv;
std::deque<Job> g_queue;
bool g_started = false;
std::atomic<bool> g_busy{false}, g_cancel{false};

// progress (written by worker, read by UI timer)
std::mutex g_pmx;
wstring g_pName, g_pVerb;
std::atomic<ULONGLONG> g_doneBytes{0}, g_totalBytes{0};
std::atomic<int> g_doneFiles{0}, g_totalFiles{0};
DWORD g_jobStart = 0;
HWND g_prog = nullptr, g_pTitle, g_pName_, g_pBar, g_pCancel;

enum { R_RETRY = 101, R_REPLACE = 101, R_SKIP = 102, R_KEEPBOTH = 103, R_CANCEL = IDCANCEL };
enum { WM_PROG_START = WM_APP + 50, WM_PROG_END };

struct Ask { bool conflict; wstring a, b, c; int result = R_CANCEL; bool all = false; };

// per-job decision memory
int g_conflictAll = 0;
bool g_skipErrAll = false;

void SetName(const wstring& verb, const wstring& name) {
    std::lock_guard<std::mutex> lk(g_pmx);
    g_pVerb = verb; g_pName = name;
}

int AskUser(Ask& q) {
    if (g_cancel) return R_CANCEL;
    SendMessageW(g_prog, WM_APP_ASK, 0, (LPARAM)&q);
    if (q.result == R_CANCEL) g_cancel = true;
    return q.result;
}

wstring Describe(const wstring& p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return p;
    ULONGLONG sz = ((ULONGLONG)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    return FmtSize(sz) + L", modified " + FmtTime(d.ftLastWriteTime);
}

int Conflict(const wstring& src, const wstring& dst) {
    if (g_conflictAll) return g_conflictAll;
    Ask q{true, FileName(dst), L"Source:  " + Describe(src) + L"\nTarget:  " + Describe(dst) + L"\nIn:  " + Parent(dst)};
    int r = AskUser(q);
    if (q.all && r != R_CANCEL) g_conflictAll = r;
    return r;
}

int Error(const wstring& what, const wstring& path, DWORD err) {
    if (g_skipErrAll) return R_SKIP;
    Ask q{false, what + L" \"" + FileName(path) + L"\"", ErrText(err) + L"\n\n" + path};
    int r = AskUser(q);
    if (q.all && r == R_SKIP) g_skipErrAll = true;
    return r;
}

bool IsUnder(const wstring& child, const wstring& parent) {
    return IEq(child, parent) || (IStarts(child, parent) && child.size() > parent.size() &&
                                  (child[parent.size()] == L'\\' || parent.back() == L'\\'));
}

template <class F> void Enum(const wstring& dir, F f) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(Join(dir, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const wchar_t* n = fd.cFileName;
        if (n[0] == L'.' && (!n[1] || (n[1] == L'.' && !n[2]))) continue;
        f(fd);
    } while (!g_cancel && FindNextFileW(h, &fd));
    FindClose(h);
}

void Scan(const wstring& p, int depth) {
    DWORD a = GetFileAttributesW(p.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return;
    if (a & FILE_ATTRIBUTE_DIRECTORY) {
        if (depth > 64) return;
        Enum(p, [&](WIN32_FIND_DATAW& fd) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) Scan(Join(p, fd.cFileName), depth + 1);
            else { g_totalBytes += ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow; g_totalFiles++; }
        });
    } else {
        WIN32_FILE_ATTRIBUTE_DATA d;
        if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) g_totalBytes += ((ULONGLONG)d.nFileSizeHigh << 32) | d.nFileSizeLow;
        g_totalFiles++;
    }
}

struct CopyCtx { ULONGLONG base; };
DWORD CALLBACK Progress(LARGE_INTEGER, LARGE_INTEGER done, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD, HANDLE, HANDLE, LPVOID ctx) {
    g_doneBytes = ((CopyCtx*)ctx)->base + done.QuadPart;
    return g_cancel ? PROGRESS_CANCEL : PROGRESS_CONTINUE;
}

// Returns true only if everything was copied (nothing skipped / failed).
bool CopyItem(const wstring& src, const wstring& dst, int depth = 0) {
    if (g_cancel) return false;
    WIN32_FILE_ATTRIBUTE_DATA sd;
    while (!GetFileAttributesExW(src.c_str(), GetFileExInfoStandard, &sd)) {
        if (Error(L"Cannot read", src, GetLastError()) != R_RETRY) return false;
    }
    if (sd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (IsUnder(dst, src)) { Error(L"Cannot copy a folder into itself:", src, ERROR_INVALID_PARAMETER); return false; }
        if (depth > 64) return false;
        DWORD da = GetFileAttributesW(dst.c_str());
        if (da == INVALID_FILE_ATTRIBUTES) {
            while (!CreateDirectoryExW(src.c_str(), dst.c_str(), nullptr) && !CreateDirectoryW(dst.c_str(), nullptr)) {
                if (Error(L"Cannot create folder", dst, GetLastError()) != R_RETRY) return false;
            }
        } else if (!(da & FILE_ATTRIBUTE_DIRECTORY)) {
            Error(L"A file with this name already exists:", dst, ERROR_ALREADY_EXISTS);
            return false;
        }
        bool ok = true;
        Enum(src, [&](WIN32_FIND_DATAW& fd) { ok &= CopyItem(Join(src, fd.cFileName), Join(dst, fd.cFileName), depth + 1); });
        return ok && !g_cancel;
    }
    ULONGLONG size = ((ULONGLONG)sd.nFileSizeHigh << 32) | sd.nFileSizeLow;
    wstring target = dst;
    if (Exists(target)) {
        int r = Conflict(src, target);
        if (r == R_SKIP || r == R_CANCEL) { g_doneBytes += size; g_doneFiles++; return false; }
        if (r == R_KEEPBOTH) target = Join(Parent(dst), UniqueName(Parent(dst), FileName(dst), false));
        else SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    SetName(L"Copying", FileName(src));
    CopyCtx ctx{g_doneBytes};
    DWORD flags = size > (512ull << 20) ? COPY_FILE_NO_BUFFERING : 0;
    while (!CopyFileExW(src.c_str(), target.c_str(), Progress, &ctx, nullptr, flags)) {
        DWORD e = GetLastError();
        if (g_cancel || e == ERROR_REQUEST_ABORTED) return false;
        int r = Error(L"Cannot copy", src, e);
        if (r != R_RETRY) { g_doneBytes = ctx.base + size; g_doneFiles++; return false; }
    }
    g_doneBytes = ctx.base + size;
    g_doneFiles++;
    return true;
}

bool DeleteTree(const wstring& p) {
    if (g_cancel) return false;
    DWORD a = GetFileAttributesW(p.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (a & FILE_ATTRIBUTE_READONLY) SetFileAttributesW(p.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
    if (a & FILE_ATTRIBUTE_DIRECTORY) {
        bool ok = true;
        if (!(a & FILE_ATTRIBUTE_REPARSE_POINT))  // never follow junctions / symlinks
            Enum(p, [&](WIN32_FIND_DATAW& fd) { ok &= DeleteTree(Join(p, fd.cFileName)); });
        if (!ok) return false;
        while (!RemoveDirectoryW(p.c_str())) {
            if (Error(L"Cannot delete folder", p, GetLastError()) != R_RETRY) return false;
        }
        return true;
    }
    SetName(L"Deleting", FileName(p));
    while (!DeleteFileW(p.c_str())) {
        if (Error(L"Cannot delete", p, GetLastError()) != R_RETRY) return false;
    }
    g_doneFiles++;
    return true;
}

bool SameVolume(const wstring& a, const wstring& b) {
    wchar_t va[MAX_PATH], vb[MAX_PATH];
    if (!GetVolumePathNameW(a.c_str(), va, MAX_PATH) || !GetVolumePathNameW(b.c_str(), vb, MAX_PATH)) return false;
    return IEq(va, vb);
}

bool MoveItem(const wstring& src, const wstring& dst) {
    if (g_cancel) return false;
    SetName(L"Moving", FileName(src));
    DWORD sa = GetFileAttributesW(src.c_str()), da = GetFileAttributesW(dst.c_str());
    if (sa == INVALID_FILE_ATTRIBUTES) return false;
    if ((sa & FILE_ATTRIBUTE_DIRECTORY) && IsUnder(dst, src)) { Error(L"Cannot move a folder into itself:", src, ERROR_INVALID_PARAMETER); return false; }
    if (da == INVALID_FILE_ATTRIBUTES) {
        while (!MoveFileExW(src.c_str(), dst.c_str(), 0)) {
            if (Error(L"Cannot move", src, GetLastError()) != R_RETRY) return false;
        }
        return true;
    }
    bool sDir = sa & FILE_ATTRIBUTE_DIRECTORY, dDir = da & FILE_ATTRIBUTE_DIRECTORY;
    if (sDir && dDir) {  // merge folders
        bool ok = true;
        vector<wstring> names;
        Enum(src, [&](WIN32_FIND_DATAW& fd) { names.push_back(fd.cFileName); });
        for (auto& n : names) ok &= MoveItem(Join(src, n), Join(dst, n));
        if (ok) RemoveDirectoryW(src.c_str());
        return ok;
    }
    if (sDir != dDir) { Error(L"An item with this name already exists:", dst, ERROR_ALREADY_EXISTS); return false; }
    int r = Conflict(src, dst);
    if (r == R_SKIP || r == R_CANCEL) return false;
    wstring target = dst;
    DWORD flags = 0;
    if (r == R_KEEPBOTH) target = Join(Parent(dst), UniqueName(Parent(dst), FileName(dst), false));
    else { SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL); flags = MOVEFILE_REPLACE_EXISTING; }
    while (!MoveFileExW(src.c_str(), target.c_str(), flags)) {
        if (Error(L"Cannot move", src, GetLastError()) != R_RETRY) return false;
    }
    return true;
}

// ---------- ZIP writer (deflate via zlib, UTF-8 names, ZIP64 for big files / archives) ----------
struct ZEntry { std::string name; uint32_t crc = 0; uint64_t csize = 0, usize = 0, offset = 0; uint16_t method = 0, time = 0, date = 0; uint32_t attr = 0; };

struct ZipOut {
    HANDLE h;
    uint64_t pos = 0;
    vector<ZEntry> entries;
    bool Put(const void* p, size_t n) { DWORD w; if (!WriteFile(h, p, (DWORD)n, &w, nullptr) || w != n) return false; pos += n; return true; }
};

void P16(std::string& b, uint16_t v) { b.append((const char*)&v, 2); }
void P32(std::string& b, uint32_t v) { b.append((const char*)&v, 4); }
void P64(std::string& b, uint64_t v) { b.append((const char*)&v, 8); }

std::string ToUtf8(const wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

void DosTime(const FILETIME& ft, uint16_t& t, uint16_t& d) {
    FILETIME lft; SYSTEMTIME st;
    FileTimeToLocalFileTime(&ft, &lft);
    FileTimeToSystemTime(&lft, &st);
    if (st.wYear < 1980) st.wYear = 1980;
    t = (uint16_t)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2));
    d = (uint16_t)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
}

bool AlreadyCompressed(const wstring& name) {  // store these as-is: deflate would only waste time
    static const wchar_t* exts[] = {L".mp4", L".mkv", L".mov", L".avi", L".wmv", L".webm", L".m4v", L".mp3", L".m4a", L".aac", L".flac", L".ogg",
                                    L".jpg", L".jpeg", L".png", L".gif", L".webp", L".heic", L".zip", L".7z", L".rar", L".gz", L".xz", L".bz2",
                                    L".docx", L".xlsx", L".pptx", L".pdf", L".iso"};
    const wchar_t* e = PathFindExtensionW(name.c_str());
    for (auto x : exts) if (!lstrcmpiW(e, x)) return true;
    return false;
}

const uint64_t kBig = 0xF0000000ull;  // files this large get ZIP64 sizes in the local header (deflate may grow data a bit)

bool ZipFile(ZipOut& z, const wstring& path, const std::string& name, const WIN32_FILE_ATTRIBUTE_DATA& fa) {
    ZEntry e;
    e.name = name;
    e.usize = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
    e.attr = fa.dwFileAttributes & 0xFF;
    e.method = AlreadyCompressed(path) || e.usize == 0 ? 0 : 8;
    DosTime(fa.ftLastWriteTime, e.time, e.date);
    HANDLE in;
    while ((in = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)) == INVALID_HANDLE_VALUE) {
        if (Error(L"Cannot read", path, GetLastError()) != R_RETRY) { g_doneBytes += e.usize; g_doneFiles++; return true; }  // skip file
    }
    SetName(L"Compressing", FileName(path));
    bool z64 = e.usize >= kBig;
    e.offset = z.pos;
    std::string hdr;
    P32(hdr, 0x04034b50); P16(hdr, z64 ? 45 : 20); P16(hdr, 0x0800); P16(hdr, e.method); P16(hdr, e.time); P16(hdr, e.date);
    P32(hdr, 0); P32(hdr, z64 ? 0xFFFFFFFF : 0); P32(hdr, z64 ? 0xFFFFFFFF : 0);  // crc / sizes patched afterwards
    P16(hdr, (uint16_t)name.size()); P16(hdr, z64 ? 20 : 0);
    hdr += name;
    if (z64) { P16(hdr, 1); P16(hdr, 16); P64(hdr, 0); P64(hdr, 0); }
    if (!z.Put(hdr.data(), hdr.size())) { CloseHandle(in); return false; }

    const size_t CH = 1 << 20;
    vector<unsigned char> ibuf(CH), obuf(CH + 1024);
    z_stream zs{};
    if (e.method == 8) deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    uLong crc = crc32(0, nullptr, 0);
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(in, ibuf.data(), (DWORD)CH, &got, nullptr)) { ok = false; Error(L"Cannot read", path, GetLastError()); g_cancel = true; break; }
        if (g_cancel) { ok = false; break; }
        crc = crc32(crc, ibuf.data(), got);
        g_doneBytes += got;
        if (e.method == 0) {
            if (got && !z.Put(ibuf.data(), got)) { ok = false; break; }
            e.csize += got;
        } else {
            zs.next_in = ibuf.data();
            zs.avail_in = got;
            int flush = got == 0 ? Z_FINISH : Z_NO_FLUSH;
            do {
                zs.next_out = obuf.data();
                zs.avail_out = (uInt)obuf.size();
                deflate(&zs, flush);
                size_t n = obuf.size() - zs.avail_out;
                if (n && !z.Put(obuf.data(), n)) { ok = false; break; }
                e.csize += n;
            } while (ok && zs.avail_out == 0);
            if (!ok) break;
        }
        if (got == 0) break;
    }
    if (e.method == 8) deflateEnd(&zs);
    CloseHandle(in);
    if (!ok) return false;
    e.crc = (uint32_t)crc;
    if (!z64 && e.csize >= 0xFFFFFFFFull) return false;  // cannot happen with the kBig margin
    // patch crc / sizes in the local header, then continue at the end
    LARGE_INTEGER at, end;
    at.QuadPart = (LONGLONG)e.offset + 14;
    end.QuadPart = (LONGLONG)z.pos;
    std::string fix;
    P32(fix, e.crc); P32(fix, z64 ? 0xFFFFFFFF : (uint32_t)e.csize); P32(fix, z64 ? 0xFFFFFFFF : (uint32_t)e.usize);
    DWORD w;
    SetFilePointerEx(z.h, at, nullptr, FILE_BEGIN);
    WriteFile(z.h, fix.data(), (DWORD)fix.size(), &w, nullptr);
    if (z64) {
        std::string x; P64(x, e.usize); P64(x, e.csize);
        at.QuadPart = (LONGLONG)e.offset + 30 + name.size() + 4;
        SetFilePointerEx(z.h, at, nullptr, FILE_BEGIN);
        WriteFile(z.h, x.data(), (DWORD)x.size(), &w, nullptr);
    }
    SetFilePointerEx(z.h, end, nullptr, FILE_BEGIN);
    z.entries.push_back(e);
    g_doneFiles++;
    return true;
}

bool ZipDirEntry(ZipOut& z, const std::string& name, const WIN32_FILE_ATTRIBUTE_DATA& fa) {
    ZEntry e;
    e.name = name;  // ends with '/'
    e.attr = (fa.dwFileAttributes & 0xFF) | FILE_ATTRIBUTE_DIRECTORY;
    DosTime(fa.ftLastWriteTime, e.time, e.date);
    e.offset = z.pos;
    std::string hdr;
    P32(hdr, 0x04034b50); P16(hdr, 20); P16(hdr, 0x0800); P16(hdr, 0); P16(hdr, e.time); P16(hdr, e.date);
    P32(hdr, 0); P32(hdr, 0); P32(hdr, 0); P16(hdr, (uint16_t)name.size()); P16(hdr, 0);
    hdr += name;
    z.entries.push_back(e);
    return z.Put(hdr.data(), hdr.size());
}

bool ZipTree(ZipOut& z, const wstring& path, const wstring& rel, int depth) {
    if (g_cancel) return false;
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return true;
    wstring zr = rel;
    std::replace(zr.begin(), zr.end(), L'\\', L'/');
    if (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        if (!ZipDirEntry(z, ToUtf8(zr) + "/", fa)) return false;
        if ((fa.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || depth > 64) return true;
        vector<wstring> names;
        Enum(path, [&](WIN32_FIND_DATAW& fd) { names.push_back(fd.cFileName); });
        for (auto& n : names) if (!ZipTree(z, Join(path, n), rel + L"\\" + n, depth + 1)) return false;
        return true;
    }
    return ZipFile(z, path, ToUtf8(zr), fa);
}

bool ZipFinish(ZipOut& z) {
    uint64_t cdStart = z.pos;
    for (auto& e : z.entries) {
        bool big = e.usize >= 0xFFFFFFFFull || e.csize >= 0xFFFFFFFFull || e.offset >= 0xFFFFFFFFull || e.usize >= kBig;
        std::string c;
        P32(c, 0x02014b50); P16(c, 45); P16(c, big ? 45 : 20); P16(c, 0x0800); P16(c, e.method); P16(c, e.time); P16(c, e.date);
        P32(c, e.crc); P32(c, big ? 0xFFFFFFFF : (uint32_t)e.csize); P32(c, big ? 0xFFFFFFFF : (uint32_t)e.usize);
        P16(c, (uint16_t)e.name.size()); P16(c, big ? 28 : 0); P16(c, 0); P16(c, 0); P16(c, 0);
        P32(c, e.attr); P32(c, big ? 0xFFFFFFFF : (uint32_t)e.offset);
        c += e.name;
        if (big) { P16(c, 1); P16(c, 24); P64(c, e.usize); P64(c, e.csize); P64(c, e.offset); }
        if (!z.Put(c.data(), c.size())) return false;
    }
    uint64_t cdSize = z.pos - cdStart, count = z.entries.size();
    std::string t;
    if (count >= 0xFFFF || cdSize >= 0xFFFFFFFFull || cdStart >= 0xFFFFFFFFull) {
        uint64_t z64eocd = z.pos;
        P32(t, 0x06064b50); P64(t, 44); P16(t, 45); P16(t, 45); P32(t, 0); P32(t, 0);
        P64(t, count); P64(t, count); P64(t, cdSize); P64(t, cdStart);
        P32(t, 0x07064b50); P32(t, 0); P64(t, z64eocd); P32(t, 1);
        P32(t, 0x06054b50); P16(t, 0); P16(t, 0); P16(t, 0xFFFF); P16(t, 0xFFFF); P32(t, 0xFFFFFFFF); P32(t, 0xFFFFFFFF); P16(t, 0);
    } else {
        P32(t, 0x06054b50); P16(t, 0); P16(t, 0); P16(t, (uint16_t)count); P16(t, (uint16_t)count);
        P32(t, (uint32_t)cdSize); P32(t, (uint32_t)cdStart); P16(t, 0);
    }
    return z.Put(t.data(), t.size());
}

void RunZip(Job& j) {
    SetName(L"Calculating", L"");
    for (auto& s : j.src) Scan(s, 0);
    HANDLE h = CreateFileW(j.dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { Error(L"Cannot create", j.dst, GetLastError()); return; }
    ZipOut z{h};
    bool ok = true;
    for (auto& s : j.src) {
        if (IsRoot(s) || IEq(s, j.dst)) continue;
        if (!(ok = ZipTree(z, s, FileName(s), 0))) break;
    }
    if (ok && !g_cancel) ok = ZipFinish(z);
    else ok = false;
    DWORD err = GetLastError();
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(j.dst.c_str());  // no half-written archives
        if (!g_cancel) Error(L"Could not write", j.dst, err);
    }
}

void RunJob(Job& j) {
    g_conflictAll = 0; g_skipErrAll = false;
    g_doneBytes = g_totalBytes = 0; g_doneFiles = g_totalFiles = 0;
    auto affected = new vector<wstring>();
    if (!j.dst.empty()) affected->push_back(j.type == OP_ZIP ? Parent(j.dst) : j.dst);
    for (auto& s : j.src) affected->push_back(Parent(s));

    if (j.type == OP_ZIP) {
        RunZip(j);
    } else if (j.type == OP_RECYCLE) {
        SetName(L"Moving to Recycle Bin", j.src.size() == 1 ? FileName(j.src[0]) : std::to_wstring(j.src.size()) + L" items");
        wstring list;
        for (auto& s : j.src) { list += s; list += L'\0'; }
        list += L'\0';
        SHFILEOPSTRUCTW op{};
        op.hwnd = g_main;
        op.wFunc = FO_DELETE;
        op.pFrom = list.c_str();
        op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_WANTNUKEWARNING;
        SHFileOperationW(&op);
    } else if (j.type == OP_DELETE) {
        SetName(L"Deleting", L"");
        for (auto& s : j.src) if (!g_cancel) DeleteTree(s);
    } else {
        bool move = j.type == OP_MOVE;
        SetName(L"Calculating", L"");
        for (auto& s : j.src)
            if (!move || !SameVolume(s, j.dst)) Scan(s, 0);
        if (!g_totalFiles) g_totalFiles = (int)j.src.size();
        for (auto& s : j.src) {
            if (g_cancel) break;
            if (IsRoot(s)) continue;
            wstring name = FileName(s), dst = Join(j.dst, name);
            if (IEq(Parent(s), j.dst)) {
                if (move) continue;  // moving into the same folder: nothing to do
                dst = Join(j.dst, UniqueName(j.dst, name, true));  // paste into same folder -> "name - Copy"
            }
            if (!move) CopyItem(s, dst);
            else if (SameVolume(s, j.dst)) { MoveItem(s, dst); g_doneFiles++; }
            else if (CopyItem(s, dst) && !g_cancel) DeleteTree(s);
        }
    }
    PostMessageW(g_main, WM_APP_OPDONE, 0, (LPARAM)affected);
}

void Worker() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (;;) {
        Job j;
        {
            std::unique_lock<std::mutex> lk(g_mx);
            g_cv.wait(lk, [] { return !g_queue.empty(); });
            j = std::move(g_queue.front());
            g_queue.pop_front();
        }
        g_cancel = false;
        PostMessageW(g_prog, WM_PROG_START, 0, 0);
        RunJob(j);
        bool more;
        { std::lock_guard<std::mutex> lk(g_mx); more = !g_queue.empty(); g_busy = more; }
        if (!more) PostMessageW(g_prog, WM_PROG_END, 0, 0);
    }
}

// ---------- progress window (UI thread) ----------
void ShowAsk(HWND hwnd, Ask* q) {
    TASKDIALOG_BUTTON conflictBtns[] = {{R_REPLACE, L"Replace"}, {R_SKIP, L"Skip"}, {R_KEEPBOTH, L"Keep both"}};
    TASKDIALOG_BUTTON errBtns[] = {{R_RETRY, L"Retry"}, {R_SKIP, L"Skip"}};
    TASKDIALOGCONFIG c{sizeof(c)};
    c.hwndParent = IsWindowVisible(hwnd) ? hwnd : g_main;
    c.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    c.pszWindowTitle = q->conflict ? L"File already exists" : L"Error";
    c.pszMainIcon = q->conflict ? TD_INFORMATION_ICON : TD_WARNING_ICON;
    c.pszMainInstruction = q->a.c_str();
    c.pszContent = q->b.c_str();
    c.pButtons = q->conflict ? conflictBtns : errBtns;
    c.cButtons = q->conflict ? 3 : 2;
    c.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    c.pszVerificationText = q->conflict ? L"Do this for all conflicts" : L"Skip all errors";
    int btn = R_CANCEL; BOOL all = FALSE;
    DWORD t0 = GetTickCount();
    TaskDialogIndirect(&c, &btn, nullptr, &all);
    g_jobStart += GetTickCount() - t0;  // time spent deciding doesn't count for speed
    q->result = btn;
    q->all = all;
}

void UpdateProgress() {
    wstring verb, name;
    { std::lock_guard<std::mutex> lk(g_pmx); verb = g_pVerb; name = g_pName; }
    ULONGLONG db = g_doneBytes, tb = g_totalBytes;
    wstring title = verb;
    if (g_totalFiles) title += L"  " + std::to_wstring(g_doneFiles.load()) + L" / " + std::to_wstring(g_totalFiles.load()) + L" items";
    if (tb) {
        title += L"   " + FmtSize(db) + L" / " + FmtSize(tb);
        DWORD ms = GetTickCount() - g_jobStart;
        if (ms > 1000) title += L"   " + FmtSize(db * 1000 / ms) + L"/s";
        SendMessageW(g_pBar, PBM_SETPOS, (WPARAM)(db * 1000 / tb), 0);
    } else if (g_totalFiles) {
        SendMessageW(g_pBar, PBM_SETPOS, g_doneFiles * 1000 / g_totalFiles, 0);
    }
    SetWindowTextW(g_pTitle, title.c_str());
    SetWindowTextW(g_pName_, name.c_str());
}

LRESULT CALLBACK ProgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PROG_START:
        g_jobStart = GetTickCount();
        SendMessageW(g_pBar, PBM_SETPOS, 0, 0);
        SetTimer(h, 1, 500, nullptr);   // show only if the job takes a while
        SetTimer(h, 2, 150, nullptr);
        return 0;
    case WM_PROG_END:
        KillTimer(h, 1); KillTimer(h, 2);
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_TIMER:
        if (w == 1) {
            KillTimer(h, 1);
            RECT r; GetWindowRect(g_main, &r);
            RECT pr; GetWindowRect(h, &pr);
            SetWindowPos(h, nullptr, (r.left + r.right - (pr.right - pr.left)) / 2, (r.top + r.bottom - (pr.bottom - pr.top)) / 2, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            ShowWindow(h, SW_SHOWNOACTIVATE);
        }
        UpdateProgress();
        return 0;
    case WM_APP_ASK:
        ShowAsk(h, (Ask*)l);
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkColor((HDC)w, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_COMMAND:
        if (LOWORD(w) == IDCANCEL) g_cancel = true;
        return 0;
    case WM_CLOSE:
        g_cancel = true;
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

void CreateProgressWindow() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = ProgProc;
    wc.hInstance = g_inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"NxProgress";
    RegisterClassW(&wc);
    RECT rc{0, 0, S(460), S(130)};
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW);
    g_prog = CreateWindowExW(WS_EX_TOOLWINDOW, L"NxProgress", L"File operation", WS_CAPTION | WS_SYSMENU | WS_POPUP,
                             0, 0, rc.right - rc.left, rc.bottom - rc.top, g_main, nullptr, g_inst, nullptr);
    g_pTitle = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP, S(14), S(12), S(432), S(20), g_prog, nullptr, g_inst, nullptr);
    g_pName_ = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS, S(14), S(34), S(432), S(20), g_prog, nullptr, g_inst, nullptr);
    g_pBar = CreateWindowW(PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE, S(14), S(60), S(432), S(18), g_prog, nullptr, g_inst, nullptr);
    SendMessageW(g_pBar, PBM_SETRANGE32, 0, 1000);
    g_pCancel = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, S(366), S(92), S(80), S(26), g_prog, (HMENU)IDCANCEL, g_inst, nullptr);
    for (HWND c : {g_pTitle, g_pName_, g_pCancel}) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
}

}  // namespace

void FileOp(OpType type, const vector<wstring>& src, const wstring& dstDir) {
    if (src.empty()) return;
    if (!g_prog) CreateProgressWindow();
    std::lock_guard<std::mutex> lk(g_mx);
    g_queue.push_back({type, src, dstDir});
    g_busy = true;
    if (!g_started) { g_started = true; std::thread(Worker).detach(); }
    g_cv.notify_one();
}

bool FileOpBusy() { return g_busy; }
