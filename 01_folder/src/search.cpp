#include "search.h"
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace {

bool Combining(wchar_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) ||
           (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F);
}

bool HasWild(const wstring& s) { return s.find_first_of(L"*?") != wstring::npos; }

// '*' = any run of characters, '?' = one character; both strings already folded
bool Glob(const wchar_t* p, const wchar_t* s) {
    const wchar_t *star = nullptr, *mark = nullptr;
    while (*s) {
        if (*p == L'?' || (*p && *p == *s)) { p++; s++; continue; }
        if (*p == L'*') { star = p++; mark = s; continue; }
        if (!star) return false;
        p = star + 1;
        s = ++mark;
    }
    while (*p == L'*') p++;
    return !*p;
}

struct Query {
    vector<wstring> words;   // substrings, all required
    vector<wstring> globs;   // whole-name patterns, all required
    bool exact = false;
    wstring exactName;
    bool empty() const { return words.empty() && globs.empty() && !exact; }
};

wstring Trim(const wstring& s) {
    size_t a = s.find_first_not_of(L" \t"), b = s.find_last_not_of(L" \t");
    return a == wstring::npos ? L"" : s.substr(a, b - a + 1);
}

Query Parse(const wstring& in) {
    Query q;
    wstring s = Trim(in);
    if (s.empty()) return q;
    if (s[0] == L'=') {  // whole name
        wstring n = Trim(s.substr(1));
        if (n.size() >= 2 && n.front() == L'"' && n.back() == L'"') n = n.substr(1, n.size() - 2);
        n = SearchFold(n);
        if (n.empty()) return q;
        if (HasWild(n)) q.globs.push_back(n);
        else { q.exact = true; q.exactName = n; }
        return q;
    }
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) i++;
        if (i >= s.size()) break;
        wstring tok;
        if (s[i] == L'"') {
            size_t e = s.find(L'"', i + 1);
            tok = s.substr(i + 1, e == wstring::npos ? wstring::npos : e - i - 1);
            i = e == wstring::npos ? s.size() : e + 1;
        } else {
            size_t e = s.find_first_of(L" \t", i);
            tok = s.substr(i, e == wstring::npos ? wstring::npos : e - i);
            i = e == wstring::npos ? s.size() : e;
        }
        tok = SearchFold(tok);
        if (tok.empty()) continue;
        (HasWild(tok) ? q.globs : q.words).push_back(tok);
    }
    return q;
}

bool Match(const Query& q, const wstring& folded) {
    if (q.exact) return folded == q.exactName;
    for (auto& w : q.words) if (folded.find(w) == wstring::npos) return false;
    for (auto& g : q.globs) if (!Glob(g.c_str(), folded.c_str())) return false;
    return true;
}

}  // namespace

wstring SearchFold(const wstring& s) {
    bool ascii = true;
    for (wchar_t c : s) if (c >= 0x80) { ascii = false; break; }
    wstring r;
    if (ascii) {
        r = s;
        for (auto& c : r) if (c >= L'A' && c <= L'Z') c += 32;
        return r;
    }
    // compatibility decomposition: "ệ" -> "e" + marks, full-width "Ａ" -> "A", "①" -> "1"
    wstring d;
    int n = NormalizeString(NormalizationKD, s.c_str(), (int)s.size(), nullptr, 0);
    for (int tries = 0; n > 0 && tries < 3; tries++) {
        d.resize(n);
        int got = NormalizeString(NormalizationKD, s.c_str(), (int)s.size(), &d[0], n);
        if (got > 0) { d.resize(got); break; }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) { d.clear(); break; }
        n = -got;
        d.clear();
    }
    if (d.empty()) d = s;
    r.reserve(d.size());
    for (wchar_t c : d) {
        if (Combining(c)) continue;
        if (c == 0x0111 || c == 0x0110) c = L'd';  // Vietnamese đ / Đ has no decomposition
        r += c;
    }
    if (!r.empty()) CharLowerBuffW(&r[0], (DWORD)r.size());
    return r;
}

bool SearchMatches(const wstring& query, const wstring& name) {
    Query q = Parse(query);
    return !q.empty() && Match(q, SearchFold(name));
}

// Several threads read folders at the same time: an SSD answers them in parallel and an HDD can reorder the seeks.
void SearchAsync(HWND notify, int tabId, int gen, const wstring& root, const wstring& query, SearchCancel cancel) {
    bool hidden = g_showHidden;
    std::thread([=] {
        const size_t kLimit = 200000;
        const int kWorkers = 4;
        DWORD start = GetTickCount();
        const Query q = Parse(query);
        struct Shared {
            std::mutex m;
            std::condition_variable cv;
            std::deque<wstring> todo;
            int busy = 0;
            vector<Item> found;
            ULONGLONG folders = 0;
            size_t total = 0;
            bool limited = false;
            DWORD err = 0;
        } S;
        if (root.empty()) {  // This PC: every local fixed / removable drive
            for (auto& d : ListDrives())
                if (d.dtype == DRIVE_FIXED || d.dtype == DRIVE_REMOVABLE) S.todo.push_back(d.name);
        } else {
            S.todo.push_back(root);
        }
        if (q.empty()) S.todo.clear();

        auto worker = [&] {
            for (;;) {
                wstring dir;
                {
                    std::unique_lock<std::mutex> lk(S.m);
                    S.cv.wait(lk, [&] { return !S.todo.empty() || S.busy == 0 || *cancel || S.limited; });
                    if (*cancel || S.limited || S.todo.empty()) { S.cv.notify_all(); return; }  // empty + nobody busy = done
                    dir = std::move(S.todo.front());
                    S.todo.pop_front();
                    S.busy++;
                }
                vector<Item> local;
                vector<wstring> subs;
                DWORD err = 0;
                WIN32_FIND_DATAW fd;
                HANDLE f = FindFirstFileExW(Join(dir, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
                if (f == INVALID_HANDLE_VALUE) {
                    err = GetLastError();
                } else {
                    wstring rel = root.empty() ? dir : dir.size() > root.size() ? dir.substr(root.size() + (root.back() == L'\\' ? 0 : 1)) : L"";
                    do {
                        const wchar_t* n = fd.cFileName;
                        if (n[0] == L'.' && (!n[1] || (n[1] == L'.' && !n[2]))) continue;
                        if (!hidden && (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
                        bool isDir = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
                        if (Match(q, SearchFold(n))) {
                            Item it;
                            it.name = n;
                            it.attr = fd.dwFileAttributes;
                            it.size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
                            it.ct = fd.ftCreationTime; it.mt = fd.ftLastWriteTime; it.at = fd.ftLastAccessTime;
                            it.folder = dir;
                            it.label = rel;
                            local.push_back(std::move(it));
                        }
                        // junctions / symlinks are not followed (no loops, no double counting)
                        if (isDir && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) subs.push_back(Join(dir, n));
                    } while (!*cancel && FindNextFileW(f, &fd));
                    FindClose(f);
                }
                {
                    std::lock_guard<std::mutex> lk(S.m);
                    S.folders++;
                    if (err && IEq(dir, root)) S.err = err;
                    for (auto& s : subs) S.todo.push_back(std::move(s));
                    for (auto& it : local) {
                        if (S.total >= kLimit) { S.limited = true; break; }
                        S.found.push_back(std::move(it));
                        S.total++;
                    }
                    S.busy--;
                }
                S.cv.notify_all();
            }
        };
        vector<std::thread> pool;
        for (int i = 0; i < kWorkers; i++) pool.emplace_back(worker);

        // hand the results to the UI every 150 ms
        for (;;) {
            bool finished;
            auto chunk = new SearchChunk{tabId, gen};
            {
                std::unique_lock<std::mutex> lk(S.m);
                S.cv.wait_for(lk, std::chrono::milliseconds(150), [&] { return (S.todo.empty() && S.busy == 0) || S.limited || *cancel; });
                finished = (S.todo.empty() && S.busy == 0) || S.limited || *cancel;
                chunk->items.swap(S.found);
                chunk->folders = S.folders;
                chunk->limited = S.limited;
                chunk->err = S.err;
            }
            chunk->done = finished;
            if (finished) chunk->ms = GetTickCount() - start;
            if (*cancel || !PostMessageW(notify, WM_APP_SEARCH, 0, (LPARAM)chunk)) { delete chunk; *cancel = true; }
            if (finished || *cancel) break;
        }
        S.cv.notify_all();
        for (auto& t : pool) t.join();
    }).detach();
}
