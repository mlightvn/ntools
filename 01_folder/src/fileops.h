// Background copy / move / delete with progress window and conflict dialogs.
#pragma once
#include "common.h"

enum OpType { OP_COPY, OP_MOVE, OP_DELETE, OP_RECYCLE, OP_ZIP };

// Queue a job. Affected folders are posted to g_main as WM_APP_OPDONE when done.
// OP_ZIP: dstDir is the full path of the .zip to create.
void FileOp(OpType type, const vector<wstring>& src, const wstring& dstDir = L"");
bool FileOpBusy();
