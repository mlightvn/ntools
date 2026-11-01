// Column definitions for the details view.
#pragma once
#include "fs.h"

struct ColDef { const wchar_t* name; int width; bool right; };
extern const ColDef g_colDefs[C_ALL];
extern bool g_colShown[C_FILECOLS];   // user-toggled file columns (C_NAME always shown)
extern int g_colW[C_ALL];             // widths at 96 dpi

vector<int> VisibleColumns(bool drives);
wstring CellText(const Item& it, int col);
int ColumnMenu(HWND owner, POINT screenPt);  // returns toggled column or -1
