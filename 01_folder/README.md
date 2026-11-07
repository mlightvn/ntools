**English** | [Tiếng Việt](README.vi.md) | [简体中文](README.zh-CN.md) | [日本語](README.ja.md)

# nExplorer

A lightweight file manager for Windows, built as a replacement for Windows Explorer.

- A single `nexplorer.exe` (~800 KB): no installer, no dependencies beyond Windows itself.
- No shell extensions, no thumbnails. Folders are read on a background thread, so a folder with 100,000 files opens in under a second.
- Settings live in `nexplorer.ini` next to the exe. Delete it to reset.

## Build

Requires MSYS2 UCRT64 g++ (`C:\msys64\ucrt64\bin`). Run:

```
build.bat
```

## Interface

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ [New folder] ✂ ⧉ 📋 ✎ 🗑  [Terminal]                 ▭ ▭▭ ▭▭▭  🎨  [Default ▾] │  toolbar
├ FAVOURITES ───┬ [tab] [tab] [+]       L / ACTIVE ┬ [tab] [+]                 R ┤
│ QUICK ACCESS  │ ← → ↑  This PC › D: › Media ›  ⟳ │ ...                         │
│ DRIVES        │ Media  12 items                ☆ │                             │
│               │ Name        Type    Date    Size │                             │
│               │ 3 items          1 folder · 2 files                           │
├───────────────┴──────────────────────────────────┴─────────────────────────────┤
│ Tab Switch pane  Ctrl C Copy  Ctrl X Cut ...                  ● Hidden files off │  status bar
└──────────────────────────────────────────────────────────────────────────────┘
```

- **Toolbar** (always visible): New folder, Cut, Copy, Paste, Rename, Delete (Shift+click deletes permanently), Terminal (opens Windows Terminal, or cmd, in the current folder). They act on the active pane. On the right: the 1 / 2 / 3 pane layout, the theme picker and the profile menu.
- **Themes**: click the palette button on the toolbar. **Void** (dark navy, the default), **Flux** (dark purple, coloured file tiles with extension badges), **Mono** (black, green accent, monospace everywhere), **Nexus** (futuristic HUD: near-black, neon accent you can switch between cyan, magenta, lime and amber, hexagon file tiles, corner brackets; uses Rajdhani / Orbitron / Share Tech Mono if installed, otherwise Bahnschrift), **Glass** (frosted panels over the blurred desktop wallpaper, lined up with the screen like glass) and **Light** (Windows icons). The theme also colours the title bar, menus and scrollbars. The choice is saved in `nexplorer.ini` (`[Window] theme=`).
  - **Background picture…** (same menu): a picture of your choice behind the whole window, with any theme; panels stay readable on top of it. With Glass, the picture is blurred instead of the wallpaper. **Remove background picture** turns it off.
  - **Window opacity** (same menu): 100% down to 70%, so you can see what is behind the window.
- **Status bar** (bottom): shortcut reminders; click **Hidden files on/off** to show or hide hidden files (same as Ctrl+H).
- 1, 2 or 3 panes: use the layout buttons on the toolbar or Ctrl+1 / 2 / 3. Drag the splitter between panes to resize them. Each pane has its own tabs; a new tab opens at **This PC**. The active pane (the one with the keyboard focus) has a bright accent frame and shows `ACTIVE`. Scrollbars are thin and coloured to match the theme.
- **Tabs**: click the `×` on a tab to close it (or Ctrl+W, middle-click). Right-click a tab for Close tab, Close other tabs, Close tabs to the right, Duplicate tab and New tab. Double-click the empty part of the tab strip to open a new tab.
- **Address bar**:
  - Click a segment to go there; click `›` to pick a subfolder; `▾` opens the history.
  - Click the empty area (or press Ctrl+L) to type a path, with suggestions as you type.
  - Accepts `%USERPROFILE%`, `/`, `\\server\share` and relative paths. Typing the path of a file opens its folder with the file selected.
- **Sidebar**:
  - **QUICK ACCESS**: folders you pin yourself. It starts with Desktop, Downloads, Documents, Pictures, Videos, Music and the Recycle Bin; unpin what you don't need. Pin a folder by right-clicking it (in a pane or the sidebar) → **Pin to Quick access**, or by dropping it onto the QUICK ACCESS heading. Drag to reorder, F2 to rename, Del to unpin.
  - **FAVOURITES**: bookmarks in groups, like a browser. Add with the ☆ in the title row, Ctrl+D, the context menu, or by dropping a folder onto FAVOURITES or onto a group. Drag items to reorder them or to move them into or out of a group; right-click for Rename, Move to group, New group, Open all in new tabs, Delete.
  - **Manage favourites** (right-click FAVOURITES, or the theme button → Manage favourites…): a window with the whole tree: New group, Add current folder, Rename, Edit path, Delete, ▲ ▼, and drag & drop.
  - **RECENT**: the 8 folders you open most (pinned and favourite folders are left out).
  - **FOLDERS**: every drive, with its subfolders loaded when you expand them. The folder shown in the active pane is highlighted.
  - At the bottom: free space of the active pane's drive.
- **Recycle Bin** (sidebar, or type `Recycle Bin` in the address bar): deleted items from every drive, with Name / Original location / Date deleted / Size / Type columns.
  - Del or Shift+Del: delete permanently (asks for confirmation).
  - Right-click: Restore (back to the original location; if the name is taken, both are kept), Delete permanently, Empty Recycle Bin.
  - Dropping files onto the Recycle Bin (sidebar / address bar) recycles them.
- **Profiles** (button at the right end of the toolbar, or Ctrl+P):
  - A profile stores the number of panes, their widths, the active pane, and for each pane its tabs (order, path, selected tab).
  - The layout is **saved only when you press Save (Ctrl+S)**. The app opens the last used profile with its saved layout; a profile that was never saved shows This PC in every pane.
  - A ● next to the profile name (and in the title bar) means the current layout differs from the saved one. Switching profiles with unsaved changes asks whether to save them.
  - Menu: pick a profile to switch; Save profile; Save as new profile; Rename; Delete; Reset layout to This PC (changes only the current view, not the saved profile).
  - Window size, columns, Favourites and Quick access are global settings, not part of a profile.
- **Columns**: right-click a column header to show or hide Date created, Date accessed, Extension and Attributes. Click a header to sort.

## Keyboard shortcuts

| Keys | Action |
|---|---|
| Arrows, Home/End, PgUp/PgDn, type a name | Move / jump to a file |
| Enter, Ctrl+Enter | Open (folders open in the app, files in their default app); open a folder in a new tab |
| Backspace, Alt+← / Alt+→ / Alt+↑ | Back; Back / Forward / Up one level |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / Cut / Paste |
| Ctrl+Shift+C | Copy as link (hyperlink + path text only) |
| Del / Shift+Del | Move to Recycle Bin / Delete permanently |
| F2 | Rename |
| Ctrl+Shift+N / Ctrl+Alt+N | New folder / New text file |
| Ctrl+T / Ctrl+Shift+T / Ctrl+W / Ctrl+Tab | New tab (same folder) / New tab (This PC) / Close tab / Next tab |
| Tab, F6 | Switch to the other pane |
| Ctrl+1 / 2 / 3 | 1 / 2 / 3 panes |
| Ctrl+L, Alt+D, F4 | Edit the address bar |
| Ctrl+F, Ctrl+K | Search the current folder and its subfolders (Esc goes back) |
| Ctrl+S / Ctrl+P | Save the layout to the current profile / Open the profile menu |
| Ctrl+D | Add / remove the current folder in Favourites |
| Ctrl+B | Show / hide the sidebar |
| Ctrl+H | Show / hide hidden files |
| F5, Ctrl+A, Alt+Enter | Refresh, Select all, Properties |

Mouse:
- Double-click to open; middle-click a tab to close it; middle-click or Ctrl+click a sidebar item to open it in a new tab.
- **Shift + mouse wheel** scrolls the file list horizontally. A tilt wheel and horizontal touchpad swipes do too.

## Search

Press **Ctrl+F** or **Ctrl+K**, or click the search box in the middle of the toolbar. Results appear while you type (or press Enter) and stream into the active pane as they are found, nearest folders first. The search covers the current folder and all its subfolders; from This PC it covers every local drive.

| You type | Finds |
|---|---|
| `tai lieu` | names that contain every word, in any order: `Tài liệu học tập.docx`, `lieu-tai.txt` |
| `"bao cao"` | names that contain the exact phrase |
| `*.pdf`, `IMG_20??*` | wildcards match the whole name |
| `=readme.md` | only this exact name |

- Upper / lower case and accents are ignored (`cafe` finds `Café`, `duong` finds `Đường`), and full-width / half-width characters match each other.
- There is no index: every search reads the folders, so results are always current. An SSD scans a few thousand folders per second; a hard disk is slower the first time.
- Results show Name, Folder (relative to the searched folder; `.\` means directly inside it), Date modified, Size and Type. Open, rename, copy, cut, delete and drag work as usual; right-click → **Open file location** jumps to the item. Click a column header to sort.
- Esc in the search box, Back or Backspace returns to the folder. Hidden files are included only when hidden files are shown. Junctions and symbolic links are not followed.
- A profile saves the folder of a search tab, not the search.

## Drag and drop

- Drag between panes, onto a folder in the list, onto a tab (hold 0.5 s to switch to it), onto an address bar segment, or onto the sidebar: the default is **Move**; hold **Ctrl** to **Copy**. Press Esc to cancel.
- Files dropped from Explorer or other apps are copied (hold Shift to move).

## Clipboard

Ctrl+C puts several formats on the clipboard at once, so each target gets what it understands:

| Paste into | Result |
|---|---|
| Explorer, this app | The actual files / folders |
| Excel, Word, Outlook | A hyperlink showing the **name** and pointing to the full path (Excel puts each item on its own row) |
| Notepad, text boxes | The full path |
| The app's address bar | The full path; press Enter to go there |

Tested with Office 2019 (Excel, Word): a plain Ctrl+C already pastes as hyperlinks, including non-ASCII names. Use Ctrl+Shift+C when you want only the link and no file objects.

## Archives / ejecting drives

- **Compress to ZIP file** (context menu): built-in, no external software. Runs in the background with progress and Cancel.
  - Non-ASCII file names are kept (UTF-8); files and archives over 4 GB are supported (ZIP64).
  - Videos, images and already-compressed files (mp4, jpg, zip…) are stored as-is for speed.
  - The archive is named `<first item>.zip` in the current folder ("(2)" is added if the name is taken).
- **Add to .7z archive (7-Zip)**: uses the installed 7-Zip, which shows its own progress window. Greyed out when 7-Zip is not installed.
- **Add to .7z with password…**: AES-256 encryption, optionally encrypting the file names too (the file list can't be seen without the password). If the password contains accented characters, the app asks again, because input methods such as Vietnamese typing tools (Unikey, EVKey…) often turn keystrokes like "a1"/"as" into "á".
- **Extract here / Extract to "<name>\"** (right-click a .zip, .7z, .rar, .tar, .gz, .tgz, .bz2, .xz): uses 7-Zip (it asks for the password of protected archives); without 7-Zip, the `tar.exe` that ships with Windows is used. "Extract to" creates a separate folder ("(2)" is added if the name is taken).
- **Eject** (right-click a USB stick / external drive in This PC or the sidebar): panes showing that drive go back to This PC, then the app asks Windows to "Safely Remove" it. If another program is using the drive, the app names that program.

## Copy / Move / Delete

- Runs in the background. The progress window appears only if an operation takes longer than 0.5 s and shows items, MB, speed and a Cancel button.
- Name conflicts: Replace / Skip / Keep both ("a (2).txt"), with a "Do this for all conflicts" option. Pasting into the source folder creates "a - Copy.txt".
- A move within the same drive is a rename (instant); across drives it copies and then deletes the source.
- **Del (Recycle Bin)**: on internal NTFS drives, items are moved straight into that drive's `$Recycle.Bin` (just a rename), so it is near-instant even for folders with hundreds of thousands of files. Restore works as usual from the Recycle Bin. USB drives with FAT/exFAT, network drives, or folders with open files fall back to the Windows method.
- **Shift+Del (permanent delete)**: `rm -rf`-style, as fast as `rmdir /s /q`. Locked files are skipped, and the files that could not be deleted are reported once at the end.
- Cancelling a copy removes the partially copied file.

## Checklist

Tested:

- [x] A folder with 100,000 files, keyboard navigation, type-to-jump
- [x] Address bar: breadcrumb, `›` menu, suggestions, red border for invalid paths, pasting a path
- [x] Copy / Cut / Paste between panes, conflicts, progress, Cancel
- [x] Drag and drop between panes (Move / Ctrl = Copy); dropping files from Explorer into the app
- [x] Dragging the splitter between panes
- [x] Clipboard to Excel / Word (name → path hyperlinks, including non-ASCII names)
- [x] New folder, F2, Del / Shift+Del
- [x] Favourites (pin, ▲ ▼), Quick access, tabs (×, context menu), 1/2/3 panes, column chooser
- [x] ZIP compression (non-ASCII names, nested / empty folders, a 4.5 GB file → ZIP64; verified with 7-Zip and .NET), Add to .7z, the Layout button, Eject shown only for external drives
- [x] Fast delete: Del of 20,000 files to the Recycle Bin and Restore of all 20,000; Shift+Del of 20,000 files with one locked file (only that file remains, reported once)
- [x] Recycle Bin view: shows the right items (including those deleted by Windows), Restore to the original location, permanent delete (removes both $R and $I)
- [x] Password-protected 7z (7zAES, hidden file names; wrong password rejected), Extract to folder with a protected archive (7-Zip asks for the password)
- [x] Profiles: This PC by default, Ctrl+S then reopening restores the layout, Save as / Rename / Delete, switching profiles, prompt for unsaved changes
- [x] Shift + wheel scrolls horizontally; the plain wheel still scrolls vertically; tilt wheel / touchpad scroll horizontally
- [x] Themes Void / Flux / Mono / Light, switching at runtime; global toolbar (New folder, Cut, Copy, Paste, Rename, Delete, Terminal); status bar
- [x] Search: every word in any order, accent-insensitive, "phrase", wildcards, =exact name; results stream in, parallel scan
- [x] Sidebar: Quick access pins, Favourites groups + manager (drag & drop), Recent, folder tree, disk usage
- [x] Background picture (any theme), Glass theme (blurred wallpaper), window opacity 70–100%
- [x] Nexus theme (HUD look, 4 accent colours)

Not done yet (current limitations):

- [ ] Dragging files from the app into other apps (the other direction works)
- [ ] Excel hyperlinks for file names containing `#` (Excel treats `#` as "a location inside the file"; Word is fine)
- [ ] Properties for several items at once (currently only the first item)
- [ ] Testing with disconnected network drives and DPI changes between monitors
- [ ] Profiles don't store each tab's Back/Forward history
- [ ] Testing Eject on a real USB drive (only the menu and external-drive detection were tested)
- [ ] Testing extraction with `tar.exe` (used only when 7-Zip is not installed)
- [ ] Testing "Empty Recycle Bin" for real (not run on the dev machine to keep its Recycle Bin; it uses the same code path as the tested Delete permanently)
