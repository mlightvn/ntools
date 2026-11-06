#include "columns.h"

const ColDef g_colDefs[C_ALL] = {
    {L"Name", 250, false}, {L"Type", 110, false}, {L"Date modified", 125, false}, {L"Size", 80, true},
    {L"Date created", 140, false}, {L"Date accessed", 140, false}, {L"Extension", 70, false},
    {L"Attributes", 70, false},
    {L"Type", 120, false}, {L"Free space", 100, true}, {L"Total size", 100, true},
    {L"Original location", 260, false}, {L"Date deleted", 125, false}, {L"Folder", 240, false},
};
bool g_colShown[C_FILECOLS] = {true, true, true, true, false, false, false, false};
int g_colW[C_ALL] = {250, 110, 125, 80, 125, 125, 70, 70, 120, 100, 100, 260, 125, 240};

vector<int> VisibleColumns(int mode) {
    if (mode == 1) return {C_NAME, C_DTYPE, C_FREE, C_TOTAL};
    if (mode == 2) return {C_NAME, C_ORIGIN, C_DELETED, C_SIZE, C_TYPE};
    if (mode == 3) return {C_NAME, C_FOLDER, C_MTIME, C_SIZE, C_TYPE};
    vector<int> v;
    for (int c = 0; c < C_FILECOLS; c++) if (c == C_NAME || g_colShown[c]) v.push_back(c);
    return v;
}

wstring CellText(const Item& it, int col) {
    switch (col) {
    case C_NAME: return it.dtype ? DriveDisplay(it) : it.name;
    case C_TYPE: case C_DTYPE: return TypeOf(it);
    case C_MTIME: return FmtTime(it.mt);
    case C_CTIME: return FmtTime(it.ct);
    case C_ATIME: return FmtTime(it.at);
    case C_SIZE: return it.isDir() && it.real.empty() ? L"" : FmtSize(it.size);  // recycled folders know their size
    case C_ORIGIN: case C_FOLDER: return it.label;
    case C_DELETED: return FmtTime(it.mt);
    case C_EXT: { if (it.isDir()) return L""; const wchar_t* e = PathFindExtensionW(it.name.c_str()); return *e ? e + 1 : L""; }
    case C_ATTR: {
        wstring s;
        if (it.attr & FILE_ATTRIBUTE_READONLY) s += L'R';
        if (it.attr & FILE_ATTRIBUTE_HIDDEN) s += L'H';
        if (it.attr & FILE_ATTRIBUTE_SYSTEM) s += L'S';
        if (it.attr & FILE_ATTRIBUTE_ARCHIVE) s += L'A';
        if (it.attr & FILE_ATTRIBUTE_REPARSE_POINT) s += L'L';
        return s;
    }
    case C_FREE: return it.size ? FmtSize(it.free) : L"";
    case C_TOTAL: return it.size ? FmtSize(it.size) : L"";
    }
    return L"";
}

int ColumnMenu(HWND owner, POINT pt) {
    HMENU m = CreatePopupMenu();
    for (int c = 1; c < C_FILECOLS; c++)
        AppendMenuW(m, MF_STRING | (g_colShown[c] ? MF_CHECKED : 0), 100 + c, g_colDefs[c].name);
    int r = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(m);
    if (r < 100) return -1;
    g_colShown[r - 100] = !g_colShown[r - 100];
    return r - 100;
}
