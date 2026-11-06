[English](README.md) | **Tiếng Việt** | [简体中文](README.zh-CN.md) | [日本語](README.ja.md)

# nExplorer

Trình quản lý file nhẹ cho Windows, dùng thay Windows Explorer.

- Chỉ có một file `nexplorer.exe` (~800KB), không cần cài đặt, không phụ thuộc DLL ngoài Windows.
- Không nạp shell extension, không tạo thumbnail. Danh sách file được đọc ở luồng nền, nên folder 100.000 file vẫn mở dưới 1 giây.
- Cấu hình lưu ở `nexplorer.ini`, nằm cạnh file exe. Muốn reset thì xóa file này.

## Build

Cần MSYS2 UCRT64 g++ (`C:\msys64\ucrt64\bin`). Chạy:

```
build.bat
```

## Giao diện

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

- **Toolbar** (luôn hiện): New folder, Cut, Copy, Paste, Rename, Delete (Shift+click = xóa vĩnh viễn), Terminal (mở Windows Terminal, hoặc cmd, tại folder hiện tại). Các nút tác động lên pane đang active. Bên phải: chọn 1 / 2 / 3 pane, chọn theme và menu profile.
- **Theme**: bấm nút bảng màu trên toolbar. **Void** (xanh navy tối, mặc định), **Flux** (tím tối, icon file dạng ô màu kèm nhãn đuôi file), **Mono** (đen, nhấn xanh lá, font monospace toàn bộ) và **Light** (icon của Windows). Theme đổi luôn màu thanh tiêu đề, menu và thanh cuộn. Lựa chọn được lưu trong `nexplorer.ini` (`[Window] theme=`).
- **Status bar** (dưới cùng): nhắc phím tắt; bấm **Hidden files on/off** để hiện/ẩn file ẩn (giống Ctrl+H).
- Có 1, 2 hoặc 3 pane: bấm nút layout trên toolbar hoặc Ctrl+1 / 2 / 3. Kéo thanh chia giữa các pane để đổi độ rộng. Mỗi pane có tab riêng, tab mới mở ở **This PC**. Pane đang active hiện chữ `ACTIVE` và vạch màu trên tab hiện tại.
- **Tab**: bấm `×` trên tab để đóng (hoặc Ctrl+W, middle-click). Chuột phải lên tab: Close tab, Close other tabs, Close tabs to the right, Duplicate tab, New tab. Double-click vùng trống của thanh tab để mở tab mới.
- **Address bar**:
  - Click một đoạn để đi tới đó; click `›` để chọn folder con; `▾` mở lịch sử.
  - Click vùng trống (hoặc Ctrl+L) để gõ path; có gợi ý khi gõ.
  - Gõ được `%USERPROFILE%`, `/`, `\\server\share`, path tương đối. Gõ path tới một file thì mở folder chứa nó và chọn sẵn file.
- **Sidebar**:
  - **QUICK ACCESS**: folder bạn tự ghim. Mặc định có Desktop, Downloads, Documents, Pictures, Videos, Music và Recycle Bin; bỏ ghim cái không cần. Ghim folder bằng chuột phải (trong pane hoặc sidebar) → **Pin to Quick access**, hoặc kéo folder thả lên tiêu đề QUICK ACCESS. Kéo để đổi thứ tự, F2 đổi tên, Del bỏ ghim.
  - **FAVOURITES**: bookmark có nhóm, giống trình duyệt. Thêm bằng ☆ ở hàng tiêu đề, Ctrl+D, menu chuột phải, hoặc kéo folder thả lên FAVOURITES hay lên một nhóm. Kéo thả để đổi thứ tự, đưa vào hoặc lấy ra khỏi nhóm; chuột phải có Rename, Move to group, New group, Open all in new tabs, Delete.
  - **Manage favourites** (chuột phải FAVOURITES, hoặc nút theme → Manage favourites…): cửa sổ hiện toàn bộ cây: New group, Add current folder, Rename, Edit path, Delete, ▲ ▼ và kéo thả.
  - **RECENT**: 8 folder bạn mở nhiều nhất (không tính folder đã ghim hoặc đã có trong Favourites).
  - **FOLDERS**: mọi ổ đĩa, folder con được đọc khi bấm mở. Folder đang hiện ở pane active được tô sáng.
  - Dưới cùng: dung lượng trống của ổ đang mở ở pane active.
- **Recycle Bin** (sidebar, hoặc gõ `Recycle Bin` vào address bar): gộp rác của mọi ổ trong máy, các cột Tên / Vị trí gốc / Ngày xóa / Dung lượng / Loại.
  - Del hoặc Shift+Del: xóa vĩnh viễn ngay (có hỏi xác nhận).
  - Chuột phải: Restore (trả về đúng chỗ cũ; trùng tên thì giữ cả hai), Delete permanently, Empty Recycle Bin.
  - Kéo file thả lên Recycle Bin (sidebar / address bar) = đưa vào thùng rác.
- **Profile** (nút ở cuối bên phải toolbar, hoặc Ctrl+P):
  - Mỗi profile lưu: số pane, độ rộng các pane, pane đang active, và trong từng pane là các tab (thứ tự, path, tab đang chọn).
  - Layout **chỉ được lưu khi bạn bấm Save (Ctrl+S)**. Mở app sẽ vào profile dùng gần nhất, với layout đã lưu; profile chưa lưu lần nào thì mọi pane ở This PC.
  - Dấu ● cạnh tên profile (và trên thanh tiêu đề) nghĩa là layout hiện tại khác bản đã lưu. Khi chuyển profile mà còn thay đổi chưa lưu, app sẽ hỏi có lưu không.
  - Menu: chọn profile để chuyển; Save profile; Save as new profile; Rename; Delete; Reset layout to This PC (chỉ đổi layout đang hiển thị, chưa lưu).
  - Kích thước cửa sổ, cột, Favourites, Quick access là cài đặt chung, không thuộc profile.
- **Cột**: chuột phải lên tiêu đề cột để bật/tắt Date created, Date accessed, Extension, Attributes. Click tiêu đề cột để sắp xếp.

## Phím tắt

| Phím | Chức năng |
|---|---|
| Mũi tên, Home/End, PgUp/PgDn, gõ tên | Di chuyển / nhảy tới file |
| Enter, Ctrl+Enter | Mở (folder mở trong app, file mở bằng app mặc định), mở folder trong tab mới |
| Backspace, Alt+← / Alt+→ / Alt+↑ | Back, Back / Forward / Lên thư mục cha |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / Cut / Paste |
| Ctrl+Shift+C | Copy as link (chỉ hyperlink + text path) |
| Del / Shift+Del | Xóa vào Recycle Bin / Xóa vĩnh viễn |
| F2 | Đổi tên |
| Ctrl+Shift+N / Ctrl+Alt+N | Folder mới / file text mới |
| Ctrl+T / Ctrl+Shift+T / Ctrl+W / Ctrl+Tab | Tab mới (cùng folder) / Tab mới (This PC) / Đóng tab / Tab kế |
| Tab, F6 | Chuyển sang pane khác |
| Ctrl+1 / 2 / 3 | 1 / 2 / 3 pane |
| Ctrl+L, Alt+D, F4 | Sửa address bar |
| Ctrl+F, Ctrl+K | Tìm trong folder hiện tại và các folder con (Esc để quay lại) |
| Ctrl+S / Ctrl+P | Lưu layout vào profile hiện tại / Mở menu profile |
| Ctrl+D | Thêm / bỏ folder hiện tại khỏi Favourites |
| Ctrl+B | Ẩn/hiện sidebar |
| Ctrl+H | Hiện/ẩn file ẩn |
| F5, Ctrl+A, Alt+Enter | Refresh, Chọn tất cả, Properties |

Chuột:
- Double-click để mở; middle-click lên tab để đóng tab; middle-click hoặc Ctrl+click lên mục ở sidebar để mở trong tab mới.
- **Shift + lăn chuột**: cuộn ngang danh sách file. Lăn nghiêng (tilt wheel) và vuốt ngang trên touchpad cũng cuộn ngang.

## Tìm kiếm

Bấm **Ctrl+F** hoặc **Ctrl+K**, hoặc bấm vào ô tìm kiếm giữa toolbar. Kết quả hiện ngay khi gõ (hoặc bấm Enter) và đổ dần vào pane đang active, folder gần hơn hiện trước. Tìm trong folder hiện tại và toàn bộ folder con; nếu đang ở This PC thì tìm trên mọi ổ trong máy.

| Gõ | Tìm được |
|---|---|
| `tai lieu` | tên chứa đủ mọi từ, thứ tự bất kỳ: `Tài liệu học tập.docx`, `lieu-tai.txt` |
| `"bao cao"` | tên chứa đúng cụm từ |
| `*.pdf`, `IMG_20??*` | ký tự đại diện, khớp với toàn bộ tên |
| `=readme.md` | chỉ đúng tên này |

- Không phân biệt hoa thường và dấu (`cafe` ra `Café`, `duong` ra `Đường`); chữ full-width / half-width cũng khớp nhau.
- Không dùng index: mỗi lần tìm đều đọc thư mục thật nên kết quả luôn đúng với hiện tại. SSD quét vài nghìn folder mỗi giây; ổ HDD chậm hơn ở lần đầu.
- Cột kết quả: Name, Folder (đường dẫn tính từ folder đang tìm; `.\` là nằm ngay trong folder đó), Date modified, Size, Type. Mở, đổi tên, copy, cut, xóa, kéo thả như bình thường; chuột phải → **Open file location** để tới chỗ chứa file. Bấm tiêu đề cột để sắp xếp.
- Esc trong ô tìm kiếm, Back hoặc Backspace để quay lại folder. File ẩn chỉ được tìm khi đang bật hiện file ẩn. Không đi theo junction / symbolic link.
- Profile lưu folder của tab tìm kiếm, không lưu kết quả tìm.

## Kéo thả

- Kéo giữa các pane, lên một folder trong danh sách, lên tab (giữ 0,5 giây thì tự chuyển sang tab đó), lên một đoạn của address bar, hoặc lên sidebar: mặc định là **Move**; giữ **Ctrl** thì là **Copy**. Esc để hủy.
- File kéo từ Explorer hoặc app khác thả vào app sẽ được copy (giữ Shift thì move).

## Clipboard

Ctrl+C ghi cùng lúc nhiều định dạng, nên mỗi nơi dán sẽ ra kết quả phù hợp:

| Dán vào | Kết quả |
|---|---|
| Explorer, app này | File/folder thật |
| Excel, Word, Outlook | Hyperlink hiện **tên**, trỏ tới đúng path (Excel đặt mỗi mục vào một dòng) |
| Notepad, ô text | Path đầy đủ |
| Address bar của app | Path đầy đủ; Enter để đi tới |

Đã kiểm tra với Office 2019 (Excel, Word): Ctrl+C thường đã dán ra hyperlink, cả với tên tiếng Việt. Ctrl+Shift+C dùng khi bạn chỉ muốn link, không kèm file thật.

## Nén file / Eject ổ đĩa

- **Compress to ZIP file** (menu chuột phải): app tự nén, không cần phần mềm ngoài. Chạy nền, có tiến trình và Cancel.
  - Tên file tiếng Việt được giữ đúng (UTF-8); hỗ trợ file và archive lớn hơn 4GB (ZIP64).
  - Video, ảnh, file đã nén (mp4, jpg, zip…) được lưu nguyên không nén lại, cho nhanh.
  - Tên archive: `<tên item đầu tiên>.zip` ngay trong folder hiện tại (trùng tên thì thêm "(2)").
- **Add to .7z archive (7-Zip)**: dùng 7-Zip đã cài trên máy, 7-Zip hiện cửa sổ tiến trình riêng. Nếu chưa cài 7-Zip, mục này bị mờ.
- **Add to .7z with password…**: mã hóa AES-256; tùy chọn mã hóa cả tên file (không có mật khẩu thì không xem được danh sách file). Nếu mật khẩu có ký tự có dấu, app hỏi lại, vì bộ gõ tiếng Việt (Unikey, EVKey…) hay tự đổi "a1"/"as" thành "á".
- **Extract here / Extract to "<tên>\"** (chuột phải lên .zip, .7z, .rar, .tar, .gz, .tgz, .bz2, .xz): dùng 7-Zip (tự hỏi mật khẩu nếu file có mật khẩu); không có 7-Zip thì dùng `tar.exe` có sẵn trong Windows. "Extract to" tạo folder riêng; trùng tên thì thêm "(2)".
- **Eject** (chuột phải lên ổ USB / ổ cứng gắn ngoài, trong This PC hoặc sidebar): các pane đang mở ổ đó tự quay về This PC rồi app yêu cầu Windows "Safely Remove". Nếu ổ đang bị chương trình khác dùng, app báo tên chương trình đó.

## Copy / Move / Delete

- Chạy ở luồng nền. Cửa sổ tiến trình chỉ hiện nếu thao tác kéo dài hơn 0,5 giây, gồm số item, MB, tốc độ và nút Cancel.
- Khi trùng tên: Replace / Skip / Keep both ("a (2).txt"), có ô "Do this for all conflicts". Dán vào chính folder nguồn thì tạo "a - Copy.txt".
- Move trong cùng ổ đĩa là đổi tên (tức thì); khác ổ thì copy rồi xóa nguồn.
- **Del (Recycle Bin)**: trên ổ NTFS trong máy, item được chuyển thẳng vào `$Recycle.Bin` của ổ đó (chỉ là đổi tên), nên gần như tức thì dù folder có hàng trăm nghìn file. Vẫn Restore được bình thường từ Recycle Bin. Ổ USB FAT/exFAT, ổ mạng, hoặc khi có file bên trong đang bị mở thì dùng cách của Windows như cũ.
- **Shift+Del (xóa vĩnh viễn)**: xóa kiểu `rm -rf`, tốc độ ngang `rmdir /s /q`. File bị khóa được bỏ qua, xóa xong mới báo một lần danh sách những file không xóa được.
- Hủy giữa chừng sẽ xóa file đang copy dở.

## Checklist

Đã kiểm tra:

- [x] Folder 100.000 file, điều hướng bằng phím, gõ tên để nhảy
- [x] Address bar: breadcrumb, menu `›`, gợi ý, path sai viền đỏ, dán path
- [x] Copy / Cut / Paste giữa pane, trùng tên, tiến trình, Cancel
- [x] Kéo thả giữa pane (Move / Ctrl = Copy); kéo file từ Explorer thả vào app
- [x] Kéo thanh chia giữa các pane
- [x] Clipboard sang Excel / Word (hyperlink tên → path, cả tên tiếng Việt)
- [x] Folder mới, F2, Del / Shift+Del
- [x] Favourites (ghim, ▲ ▼), Quick access, tab (×, menu chuột phải), 1/2/3 pane, chọn cột
- [x] Nén ZIP (tên tiếng Việt, folder lồng nhau / rỗng, file 4,5GB → ZIP64; kiểm tra bằng 7-Zip và .NET), Add to .7z, nút Layout, mục Eject chỉ hiện với ổ gắn ngoài
- [x] Xóa nhanh: Del 20.000 file vào Recycle Bin rồi Restore đủ 20.000 file; Shift+Del 20.000 file với 1 file bị khóa (chỉ file đó còn lại, báo 1 lần)
- [x] Recycle Bin trong app: hiện đúng item (cả item do Windows xóa), Restore về chỗ cũ, xóa vĩnh viễn (xóa cả cặp $R + $I)
- [x] 7z có mật khẩu (7zAES, ẩn tên file; mở sai mật khẩu bị từ chối), Extract to folder riêng với file có mật khẩu (7-Zip hỏi mật khẩu)
- [x] Profile: mặc định This PC, Ctrl+S lưu rồi mở lại app ra đúng layout, Save as / Rename / Delete, chuyển profile, hỏi khi còn thay đổi chưa lưu
- [x] Shift + lăn chuột cuộn ngang; lăn thường vẫn cuộn dọc; lăn nghiêng / touchpad cuộn ngang
- [x] Theme Void / Flux / Mono / Light, đổi theme khi đang chạy; toolbar chung (New folder, Cut, Copy, Paste, Rename, Delete, Terminal); status bar
- [x] Tìm kiếm: mọi từ theo thứ tự bất kỳ, không phân biệt dấu, "cụm từ", ký tự đại diện, =tên chính xác; kết quả hiện dần, quét song song
- [x] Sidebar: ghim Quick access, Favourites có nhóm + cửa sổ quản lý (kéo thả), Recent, cây thư mục, dung lượng ổ

Chưa làm (giới hạn hiện tại):

- [ ] Kéo file từ app ra app khác (chiều ngược lại thì được)
- [ ] Hyperlink trong Excel với tên file có `#` (Excel coi `#` là "vị trí trong file"; Word không bị)
- [ ] Properties cho nhiều item cùng lúc (hiện chỉ item đầu tiên)
- [ ] Test với ổ mạng bị ngắt và khi đổi DPI giữa hai màn hình
- [ ] Profile chưa lưu lịch sử Back/Forward của từng tab
- [ ] Test Eject thật trên một ổ USB (mới kiểm tra menu và nhận diện ổ gắn ngoài)
- [ ] Test giải nén bằng `tar.exe` (chỉ dùng khi máy không có 7-Zip)
- [ ] Test "Empty Recycle Bin" thật (chưa chạy trên máy dev để giữ rác thật; dùng cùng cơ chế với Delete permanently đã test)
