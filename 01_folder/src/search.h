// Recursive file-name search on a worker thread; results stream back to the pane in chunks.
#pragma once
#include "fs.h"
#include <atomic>
#include <memory>

struct SearchChunk {
    int tabId, gen;
    vector<Item> items;       // new matches since the previous chunk
    ULONGLONG folders = 0;    // folders scanned so far
    bool done = false;
    bool limited = false;     // stopped at the result limit
    DWORD ms = 0;             // elapsed time (final chunk)
    DWORD err = 0;            // the root could not be read
};

using SearchCancel = std::shared_ptr<std::atomic<bool>>;

// Name matching rules (case- and accent-insensitive, so "tai lieu" finds "Tài liệu"):
//   words           every word must appear somewhere in the name, in any order
//   "two words"     the phrase must appear as typed
//   *.pdf  a?c      wildcards match the whole name
//   =name.txt       the whole name must be exactly this
void SearchAsync(HWND notify, int tabId, int gen, const wstring& root, const wstring& query, SearchCancel cancel);
wstring SearchFold(const wstring& s);   // lower case, accents removed (exposed for tests)
bool SearchMatches(const wstring& query, const wstring& name);
