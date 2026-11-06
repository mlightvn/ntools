@echo off
rem Build nexplorer.exe with MSYS2 UCRT64 g++ (static, no runtime DLLs).
setlocal
set PATH=C:\msys64\ucrt64\bin;%PATH%
cd /d "%~dp0"
if not exist build mkdir build
windres res\app.rc -O coff -o build\app.res.o || exit /b 1
g++ -std=c++17 -O2 -s -municode -mwindows -static -Wall -Wno-missing-field-initializers ^
  src\main.cpp src\pane.cpp src\addrbar.cpp src\sidebar.cpp src\drag.cpp src\fs.cpp src\columns.cpp src\clip.cpp src\fileops.cpp src\profiles.cpp src\drives.cpp src\dialogs.cpp src\theme.cpp src\toolbar.cpp ^
  build\app.res.o -o nexplorer.exe ^
  -lcomctl32 -lshlwapi -lshell32 -lole32 -luuid -luxtheme -lmpr -lsetupapi -lcfgmgr32 -ldwmapi -lz || exit /b 1
echo Built nexplorer.exe
