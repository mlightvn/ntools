#include "clip.h"
#include <string>

static bool Open(HWND owner) {
    for (int i = 0; i < 20; i++) { if (OpenClipboard(owner)) return true; Sleep(10); }
    return false;
}

static HGLOBAL Alloc(const void* data, size_t n) {
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, n);
    if (h) { memcpy(GlobalLock(h), data, n); GlobalUnlock(h); }
    return h;
}

static void Put(UINT fmt, const void* data, size_t n) {
    if (HGLOBAL h = Alloc(data, n)) if (!SetClipboardData(fmt, h)) GlobalFree(h);
}

static std::string Utf8(const wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::string Esc(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
        case '&': r += "&amp;"; break; case '<': r += "&lt;"; break;
        case '>': r += "&gt;"; break; case '"': r += "&quot;"; break;
        default: r += c;
        }
    }
    return r;
}

// file URL with Unicode kept as-is (UrlCreateFromPath escapes Latin-1 chars as %XX, which Excel keeps literally)
static wstring FileUrl(const wstring& path) {
    wstring u = path.rfind(L"\\\\", 0) == 0 ? L"file:" : L"file:///";  // \\server\share -> file://server/share
    for (wchar_t c : path) {
        if (c == L'\\') u += L'/';
        else if (c == L'%') u += L"%25";
        else if (c == L'#') u += L"%23";
        else if (c == L'?') u += L"%3F";
        else u += c;
    }
    return u;
}

// CF_HTML: header with byte offsets + fragment of <a href="file:///...">name</a>
static std::string BuildHtml(const vector<wstring>& paths) {
    std::string frag;
    for (size_t i = 0; i < paths.size(); i++) {
        wstring u = FileUrl(paths[i]);
        if (i) frag += "<br>";
        frag += "<a href=\"" + Esc(Utf8(u)) + "\">" + Esc(Utf8(FileName(paths[i]))) + "</a>";
    }
    const char* hdr = "Version:0.9\r\nStartHTML:%010d\r\nEndHTML:%010d\r\nStartFragment:%010d\r\nEndFragment:%010d\r\n";
    std::string pre = "<html><body>\r\n<!--StartFragment-->", post = "<!--EndFragment-->\r\n</body></html>";
    char buf[256];
    int hlen = snprintf(buf, sizeof(buf), hdr, 0, 0, 0, 0);
    int sh = hlen, sf = sh + (int)pre.size(), ef = sf + (int)frag.size(), eh = ef + (int)post.size();
    snprintf(buf, sizeof(buf), hdr, sh, eh, sf, ef);
    return buf + pre + frag + post;
}

// CFSTR_SHELLIDLIST (CIDA): parent = desktop (empty pidl), children = absolute pidls.
static void PutIdList(const vector<wstring>& paths) {
    if (paths.size() > 200) return;  // parsing many pidls is slow; HDROP is enough
    vector<PIDLIST_ABSOLUTE> pidls;
    for (auto& p : paths) {
        PIDLIST_ABSOLUTE pidl = nullptr;
        if (FAILED(SHParseDisplayName(p.c_str(), nullptr, &pidl, 0, nullptr))) { for (auto x : pidls) CoTaskMemFree(x); return; }
        pidls.push_back(pidl);
    }
    UINT cidl = (UINT)pidls.size();
    size_t hdr = sizeof(UINT) * (cidl + 2), total = hdr + 2;
    for (auto x : pidls) total += ILGetSize(x);
    vector<BYTE> buf(total, 0);
    UINT* off = (UINT*)buf.data();
    off[0] = cidl;
    off[1] = (UINT)hdr;  // empty parent pidl (2 zero bytes)
    size_t pos = hdr + 2;
    for (UINT i = 0; i < cidl; i++) {
        UINT sz = ILGetSize(pidls[i]);
        off[2 + i] = (UINT)pos;
        memcpy(&buf[pos], pidls[i], sz);
        pos += sz;
        CoTaskMemFree(pidls[i]);
    }
    Put(RegisterClipboardFormatW(CFSTR_SHELLIDLIST), buf.data(), buf.size());
}

void ClipSetFiles(HWND owner, const vector<wstring>& paths, bool cut, bool linkOnly) {
    if (paths.empty() || !Open(owner)) return;
    EmptyClipboard();
    wstring text;
    for (auto& p : paths) text += (text.empty() ? L"" : L"\r\n") + p;
    if (!linkOnly) {
        // CF_HDROP: DROPFILES + double-null-terminated wide list
        wstring list;
        for (auto& p : paths) { list += p; list += L'\0'; }
        list += L'\0';
        vector<BYTE> buf(sizeof(DROPFILES) + list.size() * sizeof(wchar_t));
        DROPFILES* df = (DROPFILES*)buf.data();
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        memcpy(buf.data() + sizeof(DROPFILES), list.data(), list.size() * sizeof(wchar_t));
        Put(CF_HDROP, buf.data(), buf.size());
        DWORD eff = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY | DROPEFFECT_LINK;
        Put(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT), &eff, sizeof(eff));
        PutIdList(paths);
        Put(RegisterClipboardFormatW(CFSTR_FILENAMEW), paths[0].c_str(), (paths[0].size() + 1) * sizeof(wchar_t));
    }
    std::string html = BuildHtml(paths);
    Put(RegisterClipboardFormatW(L"HTML Format"), html.c_str(), html.size() + 1);
    Put(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    CloseClipboard();
}

void ClipSetText(HWND owner, const wstring& text) {
    if (!Open(owner)) return;
    EmptyClipboard();
    Put(CF_UNICODETEXT, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    CloseClipboard();
}

bool ClipGetFiles(HWND owner, vector<wstring>& out, bool& move) {
    out.clear(); move = false;
    if (!IsClipboardFormatAvailable(CF_HDROP) || !Open(owner)) return false;
    if (HDROP h = (HDROP)GetClipboardData(CF_HDROP)) {
        UINT n = DragQueryFileW(h, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++) {
            UINT len = DragQueryFileW(h, i, nullptr, 0);
            wstring s(len, L'\0');
            DragQueryFileW(h, i, &s[0], len + 1);
            out.push_back(s);
        }
    }
    if (HGLOBAL g = GetClipboardData(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT))) {
        if (DWORD* p = (DWORD*)GlobalLock(g)) { move = (*p & DROPEFFECT_MOVE) != 0; GlobalUnlock(g); }
    }
    CloseClipboard();
    return !out.empty();
}

bool ClipGetText(HWND owner, wstring& out) {
    out.clear();
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !Open(owner)) return false;
    if (HGLOBAL g = GetClipboardData(CF_UNICODETEXT)) {
        if (auto p = (const wchar_t*)GlobalLock(g)) { out = p; GlobalUnlock(g); }
    }
    CloseClipboard();
    return !out.empty();
}

void ClipClear(HWND owner) {
    if (Open(owner)) { EmptyClipboard(); CloseClipboard(); }
}
