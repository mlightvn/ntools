[English](README.md) | [Tiếng Việt](README.vi.md) | **简体中文** | [日本語](README.ja.md)

# nExplorer

一款轻量级的 Windows 文件管理器，用来替代 Windows 资源管理器（Explorer）。

- 只有一个 `nexplorer.exe`（约 800 KB），无需安装，除 Windows 自身外不依赖任何 DLL。
- 不加载 Shell 扩展，不生成缩略图。文件夹内容在后台线程读取，因此含 100,000 个文件的文件夹也能在 1 秒内打开。
- 设置保存在 exe 旁边的 `nexplorer.ini` 中。删除该文件即可重置。

## 构建

需要 MSYS2 UCRT64 g++（`C:\msys64\ucrt64\bin`）。运行：

```
build.bat
```

## 界面

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

- **工具栏**（始终显示）：New folder、Cut、Copy、Paste、Rename、Delete（Shift+点击 = 永久删除）、Terminal（在当前文件夹打开 Windows Terminal 或 cmd）。按钮作用于当前活动窗格。右侧依次是 1 / 2 / 3 窗格布局、主题选择和配置文件菜单。
- **主题**：点击工具栏上的调色板按钮。**Void**（深海军蓝，默认）、**Flux**（深紫色，彩色文件图块并显示扩展名标签）、**Mono**（黑色，绿色强调，全部使用等宽字体）、**Nexus**（未来感 HUD：近黑背景，霓虹强调色可在 cyan、magenta、lime、amber 之间切换，六边形文件图块，边角括线；已安装 Rajdhani / Orbitron / Share Tech Mono 时使用这些字体，否则使用 Bahnschrift）、**Glass**（磨砂玻璃般的面板，叠在模糊处理的桌面壁纸上，并与屏幕位置对齐）和 **Light**（使用 Windows 图标）。主题也会改变标题栏、菜单和滚动条的颜色。选择保存在 `nexplorer.ini`（`[Window] theme=`）。
  - **Background picture…**（同一菜单）：为整个窗口选择一张背景图片，任何主题都可用，文字依然清晰。在 Glass 主题下，会模糊处理这张图片来代替桌面壁纸。**Remove background picture** 关闭背景图片。
  - **Window opacity**（同一菜单）：窗口不透明度 100% 到 70%，可以看到窗口后面的内容。
- **状态栏**（底部）：显示常用快捷键；点击 **Hidden files on/off** 可显示/隐藏隐藏文件（与 Ctrl+H 相同）。
- 1、2 或 3 个窗格（pane）：点击工具栏上的布局按钮，或按 Ctrl+1 / 2 / 3。拖动窗格之间的分隔条可调整宽度。每个窗格有自己的标签页，新标签页打开 **This PC**。活动窗格（拥有键盘焦点的窗格）带有醒目的强调色边框并显示 `ACTIVE`。滚动条纤细，颜色与主题一致。
- **标签页**：点击标签上的 `×` 关闭（或 Ctrl+W、鼠标中键）。右键标签页可选 Close tab、Close other tabs、Close tabs to the right、Duplicate tab、New tab。双击标签栏空白处可新建标签页。
- **地址栏**：
  - 点击某一段即可跳转；点击 `›` 选择子文件夹；`▾` 打开历史记录。
  - 点击空白处（或按 Ctrl+L）输入路径，输入时会给出建议。
  - 支持 `%USERPROFILE%`、`/`、`\\server\share` 和相对路径。输入文件路径时，会打开其所在文件夹并选中该文件。
- **侧边栏**：
  - **QUICK ACCESS**：你自己固定的文件夹。默认包含 Desktop、Downloads、Documents、Pictures、Videos、Music 和回收站，不需要的可以取消固定。右键文件夹（窗格或侧边栏中）→ **Pin to Quick access**，或把文件夹拖到 QUICK ACCESS 标题上即可固定。拖动可调整顺序，F2 重命名，Del 取消固定。
  - **FAVOURITES**：可分组的书签，类似浏览器。点击标题行的 ☆、Ctrl+D、右键菜单，或把文件夹拖到 FAVOURITES 或某个分组上即可添加。拖动可调整顺序、移入或移出分组；右键有 Rename、Move to group、New group、Open all in new tabs、Delete。
  - **Manage favourites**（右键 FAVOURITES，或主题按钮 → Manage favourites…）：显示整棵书签树的窗口，可 New group、Add current folder、Rename、Edit path、Delete、▲ ▼ 和拖放。
  - **RECENT**：最常打开的 8 个文件夹（已固定和已收藏的除外）。
  - **FOLDERS**：所有磁盘，展开时才读取子文件夹。活动窗格当前显示的文件夹会高亮。
  - 最下方：活动窗格所在磁盘的可用空间。
- **回收站**（侧边栏，或在地址栏输入 `Recycle Bin`）：汇总所有磁盘中已删除的项目，列为 名称 / 原位置 / 删除日期 / 大小 / 类型。
  - Del 或 Shift+Del：立即永久删除（会要求确认）。
  - 右键：Restore（还原到原位置；若同名文件已存在则两者都保留）、Delete permanently、Empty Recycle Bin。
  - 把文件拖到回收站（侧边栏 / 地址栏）即放入回收站。
- **配置文件 Profile**（工具栏最右侧的按钮，或 Ctrl+P）：
  - 每个配置文件保存：窗格数量、各窗格宽度、当前活动窗格，以及每个窗格的标签页（顺序、路径、选中的标签页）。
  - 布局**只有在你按 Save（Ctrl+S）时才会保存**。启动时打开最近使用的配置文件及其已保存的布局；从未保存过的配置文件，每个窗格都显示 This PC。
  - 配置文件名旁（以及标题栏中）的 ● 表示当前布局与已保存的不同。在有未保存更改时切换配置文件，会询问是否保存。
  - 菜单：选择配置文件以切换；Save profile；Save as new profile；Rename；Delete；Reset layout to This PC（只改变当前显示，不修改已保存的配置）。
  - 窗口大小、列、Favourites、Quick access 是全局设置，不属于某个配置文件。
- **列**：右键列标题可显示 / 隐藏 Date created、Date accessed、Extension、Attributes。点击列标题可排序。

## 快捷键

| 按键 | 功能 |
|---|---|
| 方向键、Home/End、PgUp/PgDn、输入文件名 | 移动 / 跳转到文件 |
| Enter、Ctrl+Enter | 打开（文件夹在本程序中打开，文件用默认程序打开）；在新标签页打开文件夹 |
| Backspace、Alt+← / Alt+→ / Alt+↑ | 后退；后退 / 前进 / 上一级 |
| Ctrl+C / Ctrl+X / Ctrl+V | 复制 / 剪切 / 粘贴 |
| Ctrl+Shift+C | 复制为链接（仅超链接 + 路径文本） |
| Del / Shift+Del | 移到回收站 / 永久删除 |
| F2 | 重命名 |
| Ctrl+Shift+N / Ctrl+Alt+N | 新建文件夹 / 新建文本文件 |
| Ctrl+T / Ctrl+Shift+T / Ctrl+W / Ctrl+Tab | 新标签页（同一文件夹） / 新标签页（This PC） / 关闭标签页 / 下一个标签页 |
| Tab、F6 | 切换到另一个窗格 |
| Ctrl+1 / 2 / 3 | 1 / 2 / 3 个窗格 |
| Ctrl+L、Alt+D、F4 | 编辑地址栏 |
| Ctrl+F、Ctrl+K | 在当前文件夹及其子文件夹中搜索（Esc 返回） |
| Ctrl+S / Ctrl+P | 把布局保存到当前配置文件 / 打开配置文件菜单 |
| Ctrl+D | 将当前文件夹加入 / 移出 Favourites |
| Ctrl+B | 显示 / 隐藏侧边栏 |
| Ctrl+H | 显示 / 隐藏隐藏文件 |
| F5、Ctrl+A、Alt+Enter | 刷新、全选、属性 |

鼠标：
- 双击打开；中键点击标签页关闭；中键或 Ctrl+点击侧边栏项目在新标签页中打开。
- **Shift + 滚轮**：水平滚动文件列表。倾斜滚轮和触控板横向滑动同样可以水平滚动。

## 搜索

按 **Ctrl+F** 或 **Ctrl+K**，或点击工具栏中间的搜索框。输入时（或按 Enter）立即开始搜索，结果边找边显示在当前活动窗格中，较近的文件夹先出现。搜索范围是当前文件夹及其所有子文件夹；在 This PC 中则搜索所有本地磁盘。

| 输入 | 找到 |
|---|---|
| `tai lieu` | 名称包含所有词（顺序不限）：`Tài liệu học tập.docx`、`lieu-tai.txt` |
| `"bao cao"` | 名称包含完整短语 |
| `*.pdf`、`IMG_20??*` | 通配符匹配整个名称 |
| `=readme.md` | 只匹配这个完整名称 |

- 不区分大小写，忽略重音符号（`cafe` 可找到 `Café`，`duong` 可找到 `Đường`），全角和半角字符互相匹配。
- 不使用索引：每次搜索都实际读取文件夹，结果总是最新的。SSD 每秒可扫描数千个文件夹；机械硬盘第一次较慢。
- 结果列：Name、Folder（相对于搜索文件夹的路径；`.\` 表示就在该文件夹中）、Date modified、Size、Type。打开、重命名、复制、剪切、删除和拖放照常可用；右键 → **Open file location** 跳到文件所在位置。点击列标题可排序。
- 在搜索框中按 Esc、点 Back 或按 Backspace 返回文件夹。只有显示隐藏文件时才会搜索隐藏文件。不跟随 junction / 符号链接。
- 配置文件只保存搜索标签页所在的文件夹，不保存搜索本身。

## 拖放

- 拖到另一个窗格、列表中的文件夹、标签页（停留 0.5 秒自动切换到该标签页）、地址栏的某一段或侧边栏：默认为**移动**；按住 **Ctrl** 为**复制**。按 Esc 取消。
- 从资源管理器或其他程序拖入的文件会被复制（按住 Shift 则移动）。

## 剪贴板

Ctrl+C 会同时写入多种格式，因此粘贴到不同位置会得到合适的结果：

| 粘贴到 | 结果 |
|---|---|
| 资源管理器、本程序 | 真实的文件 / 文件夹 |
| Excel、Word、Outlook | 显示**名称**、指向完整路径的超链接（Excel 中每项占一行） |
| 记事本、文本框 | 完整路径 |
| 本程序的地址栏 | 完整路径；按 Enter 跳转 |

已在 Office 2019（Excel、Word）中测试：普通的 Ctrl+C 即可粘贴为超链接，非英文文件名也没问题。只想要链接、不需要文件本身时，使用 Ctrl+Shift+C。

## 压缩 / 弹出磁盘

- **Compress to ZIP file**（右键菜单）：程序自带压缩，无需外部软件。在后台运行，显示进度并可取消。
  - 保留非英文文件名（UTF-8）；支持超过 4 GB 的文件和压缩包（ZIP64）。
  - 视频、图片和已压缩的文件（mp4、jpg、zip…）直接存储、不再压缩，以提高速度。
  - 压缩包命名为当前文件夹中的 `<第一个项目>.zip`（重名时加上 "(2)"）。
- **Add to .7z archive (7-Zip)**：使用已安装的 7-Zip，由 7-Zip 显示自己的进度窗口。未安装 7-Zip 时该项为灰色。
- **Add to .7z with password…**：AES-256 加密，可选同时加密文件名（没有密码就看不到文件列表）。如果密码中含有带声调或非 ASCII 的字符，程序会再次确认，因为越南语输入法（Unikey、EVKey 等）常会把 "a1"/"as" 之类的按键变成 "á"。
- **Extract here / Extract to "<名称>\"**（右键 .zip、.7z、.rar、.tar、.gz、.tgz、.bz2、.xz）：使用 7-Zip（加密的压缩包会由 7-Zip 询问密码）；没有 7-Zip 时使用 Windows 自带的 `tar.exe`。"Extract to" 会创建单独的文件夹（重名时加上 "(2)"）。
- **Eject**（在 This PC 或侧边栏中右键 U 盘 / 外接硬盘）：正在显示该磁盘的窗格会先回到 This PC，然后程序请求 Windows "安全删除"。如果磁盘正被其他程序使用，程序会显示该程序的名称。

## 复制 / 移动 / 删除

- 在后台运行。只有操作超过 0.5 秒才会显示进度窗口，包括项目数、MB、速度和 Cancel 按钮。
- 重名时：Replace / Skip / Keep both（"a (2).txt"），并有 "Do this for all conflicts" 选项。粘贴到源文件夹本身会创建 "a - Copy.txt"。
- 同一磁盘内的移动只是重命名（瞬间完成）；跨磁盘则先复制再删除源文件。
- **Del（回收站）**：在内置的 NTFS 磁盘上，项目会被直接移入该磁盘的 `$Recycle.Bin`（只是一次重命名），即使文件夹里有几十万个文件也几乎瞬间完成。仍可照常从回收站还原。FAT/exFAT 的 U 盘、网络驱动器，或文件夹中有文件被占用时，改用 Windows 的方式。
- **Shift+Del（永久删除）**：类似 `rm -rf`，速度与 `rmdir /s /q` 相当。被占用的文件会被跳过，删除结束后一次性列出无法删除的文件。
- 中途取消复制时，会删除复制了一半的文件。

## 检查清单

已测试：

- [x] 含 100,000 个文件的文件夹、键盘导航、输入文件名跳转
- [x] 地址栏：面包屑、`›` 菜单、输入建议、无效路径红框、粘贴路径
- [x] 窗格之间复制 / 剪切 / 粘贴、重名处理、进度、取消
- [x] 窗格之间拖放（移动 / Ctrl = 复制）；从资源管理器拖入文件
- [x] 拖动窗格之间的分隔条
- [x] 剪贴板到 Excel / Word（名称 → 路径的超链接，含非英文名称）
- [x] 新建文件夹、F2、Del / Shift+Del
- [x] Favourites（固定、▲ ▼）、Quick access、标签页（×、右键菜单）、1/2/3 个窗格、列选择
- [x] ZIP 压缩（非英文名称、嵌套 / 空文件夹、4.5 GB 文件 → ZIP64；用 7-Zip 和 .NET 验证）、Add to .7z、Layout 按钮、Eject 只对外接磁盘显示
- [x] 快速删除：Del 把 20,000 个文件移入回收站并完整还原 20,000 个；Shift+Del 删除 20,000 个文件且其中 1 个被占用（只剩该文件，并只报告一次）
- [x] 程序内的回收站：正确显示项目（包括 Windows 删除的）、还原到原位置、永久删除（同时删除 $R 和 $I）
- [x] 加密 7z（7zAES、隐藏文件名；错误密码被拒绝）、对加密压缩包执行 Extract to 文件夹（7-Zip 询问密码）
- [x] 配置文件：默认 This PC、Ctrl+S 保存后重新打开恢复布局、Save as / Rename / Delete、切换配置文件、有未保存更改时提示
- [x] Shift + 滚轮水平滚动；普通滚轮仍为垂直滚动；倾斜滚轮 / 触控板水平滚动
- [x] 主题 Void / Flux / Mono / Light，运行时切换；全局工具栏（New folder、Cut、Copy、Paste、Rename、Delete、Terminal）；状态栏
- [x] 搜索：所有词顺序不限、忽略重音、"短语"、通配符、=完整名称；结果逐步显示，并行扫描
- [x] 侧边栏：Quick access 固定、Favourites 分组 + 管理窗口（拖放）、Recent、文件夹树、磁盘空间
- [x] 自选背景图片（任何主题）、Glass 主题（模糊壁纸）、窗口不透明度 70–100%
- [x] Nexus 主题（HUD 风格，4 种强调色）

尚未完成（当前限制）：

- [ ] 从本程序把文件拖到其他程序（反方向可以）
- [ ] 文件名含 `#` 时的 Excel 超链接（Excel 把 `#` 当作"文件内的位置"；Word 没问题）
- [ ] 同时查看多个项目的属性（目前只显示第一个）
- [ ] 测试网络驱动器断开以及在不同 DPI 的显示器之间切换
- [ ] 配置文件不保存每个标签页的后退 / 前进历史
- [ ] 在真实 U 盘上测试 Eject（目前只测试了菜单和外接磁盘识别）
- [ ] 测试用 `tar.exe` 解压（仅在未安装 7-Zip 时使用）
- [ ] 实际测试 "Empty Recycle Bin"（为保留开发机回收站中的内容而未运行；它与已测试的 Delete permanently 使用相同的代码）
