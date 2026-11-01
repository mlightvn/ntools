// Clipboard: multi-format file copy (Explorer, Office, text) and paste.
#pragma once
#include "common.h"

// linkOnly: only HTML hyperlinks + text (no file objects), so Office always pastes links.
void ClipSetFiles(HWND owner, const vector<wstring>& paths, bool cut, bool linkOnly);
void ClipSetText(HWND owner, const wstring& text);
bool ClipGetFiles(HWND owner, vector<wstring>& out, bool& move);
bool ClipGetText(HWND owner, wstring& out);
void ClipClear(HWND owner);
