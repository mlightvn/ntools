// Favourites: bookmarks in nested groups (like a browser's bookmark manager), and the Quick access pins.
#pragma once
#include "common.h"

struct Fav {
    int id = 0;
    wstring name;            // shown name ("" = folder name of path)
    wstring path;            // "" = a group
    bool open = true;        // group expanded in the sidebar
    vector<Fav> kids;        // groups only
    bool group() const { return path.empty(); }
    wstring Label() const { return !name.empty() ? name : DisplayName(path); }
};

extern vector<Fav> g_favs;   // top level

void FavLoad();                          // INI [Bookmarks] (migrates the old flat [Favourites] list)
void FavSave();
Fav* FavFind(int id);
Fav* FavFindPath(const wstring& path);   // first bookmark of this folder
vector<Fav>* FavListOf(int groupId);     // 0 = top level
int FavParentOf(int id, int* index);     // parent group id (0 = top level), -1 if not found
bool FavIsInside(int id, int ancestor);  // id == ancestor or nested in it
int FavAdd(const wstring& path, int groupId = 0, const wstring& name = L"");
int FavAddGroup(const wstring& name, int groupId = 0);
bool FavRemove(int id);
// where: -1 = before target, 1 = after target, 0 = into target (a group); target 0 + where 0 = end of top level
bool FavMove(int id, int target, int where);
void FavsChanged();                      // save + refresh the sidebar, the stars and the manager window
void FavManager();                       // opens (or focuses) the Manage favourites window
void FavManagerTheme();                  // after a theme switch

// TreeView helpers shared by the sidebar and the manager
void TreeIconSlot(HWND tree);            // an empty image list: room for a drawn glyph left of the text
void TreeGlyph(HDC dc, HWND tree, HTREEITEM h, const wchar_t* glyph, COLORREF c);
// drag feedback for moving favourite `dragging` (pt.x < 0 or dragging 0 just clears it); false if it can't go there
bool FavDropAt(HWND tree, POINT pt, int (*idOf)(HTREEITEM), int dragging, int* target, int* where);

// Quick access: folders the user pinned (the system folders by default)
struct Pin { wstring path, name; };
extern vector<Pin> g_pins;
void PinsLoad();
void PinsSave();
bool IsPinned(const wstring& path);
void TogglePin(const wstring& path);
