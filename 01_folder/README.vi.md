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
┌ sidebar ─────┐┌ pane 1 ──────────────────┐┌ pane 2 ─────────── ┐
│ ★ Favourites ││ [tab] [tab] [+]          ││ ...                │
│ ⚡ Quick acc. ││ ← → ↑ ⟳          [📁+][☆] ││                    │
│ Drives       ││ This PC › D: › Media ›  ▾││                    │
│              ││ Name   Type   Date  Size ││                    │
└──────────────┘└──────────────────────────┘└────────────────────┘
```

- Có 1, 2 hoặc 3 pane: bấm nút **Layout** trên toolbar (bên trái nút New folder) hoặc Ctrl+1 / 2 / 3. Kéo thanh chia giữa các pane để đổi độ rộng. Mỗi pane có tab riêng, tab mới mở ở **This PC**.
- **Tab**: bấm `×` trên tab để đóng (hoặc Ctrl+W, middle-click). Chuột phải lên tab: Close tab, Close other tabs, Close tabs to the right, Duplicate tab, New tab. Double-click vùng trống của thanh tab để mở tab mới.
- **Address bar**:
  - Click một đoạn để đi tới đó; click `›` để chọn folder con; `▾` mở lịch sử.
  - Click vùng trống (hoặc Ctrl+L) để gõ path; có gợi ý khi gõ.
  - Gõ được `%USERPROFILE%`, `/`, `\\server\share`, path tương đối. Gõ path tới một file thì mở folder chứa nó và chọn sẵn file.
- **Sidebar**:
  - Favourites là folder do bạn ghim. Ghim bằng ☆, Ctrl+D, menu chuột phải, hoặc kéo folder thả vào nhóm Favourites.
  - Rê chuột lên một mục để hiện nút ▲ ▼ đổi thứ tự. Chuột phải lên một mục để đổi tên, xóa, Move up / Move down.
  - Quick access gồm các folder hệ thống, **Recycle Bin**, và 8 folder bạn dùng nhiều nhất gần đây.
- **Recycle Bin** (sidebar, hoặc gõ `Recycle Bin` vào address bar): gộp rác của mọi ổ trong máy, các cột Tên / Vị trí gốc / Ngày xóa / Dung lượng / Loại.
  - Del hoặc Shift+Del: xóa vĩnh viễn ngay (có hỏi xác nhận).
  - Chuột phải: Restore (trả về đúng chỗ cũ; trùng tên thì giữ cả hai), Delete permanently, Empty Recycle Bin.
  - Kéo file thả lên Recycle Bin (sidebar / address bar) = đưa vào thùng rác.
- **Profile** (nút ở đầu sidebar, hoặc Ctrl+P):
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
| Ctrl+S / Ctrl+P | Lưu layout vào profile hiện tại / Mở menu profile |
| Ctrl+D | Ghim / bỏ ghim folder hiện tại |
| Ctrl+B | Ẩn/hiện sidebar |
| Ctrl+H | Hiện/ẩn file ẩn |
| F5, Ctrl+A, Alt+Enter | Refresh, Chọn tất cả, Properties |

Chuột:
- Double-click để mở; middle-click lên tab để đóng tab; middle-click hoặc Ctrl+click lên mục ở sidebar để mở trong tab mới.
- **Shift + lăn chuột**: cuộn ngang danh sách file. Lăn nghiêng (tilt wheel) và vuốt ngang trên touchpad cũng cuộn ngang.

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

Chưa làm (giới hạn hiện tại):

- [ ] Kéo file từ app ra app khác (chiều ngược lại thì được)
- [ ] Kéo thả để sắp xếp Favourites (hiện dùng nút ▲ ▼ thay thế)
- [ ] Hyperlink trong Excel với tên file có `#` (Excel coi `#` là "vị trí trong file"; Word không bị)
- [ ] Properties cho nhiều item cùng lúc (hiện chỉ item đầu tiên)
- [ ] Test với ổ mạng bị ngắt và khi đổi DPI giữa hai màn hình
- [ ] Profile chưa lưu lịch sử Back/Forward của từng tab
- [ ] Test Eject thật trên một ổ USB (mới kiểm tra menu và nhận diện ổ gắn ngoài)
- [ ] Test giải nén bằng `tar.exe` (chỉ dùng khi máy không có 7-Zip)
- [ ] Test "Empty Recycle Bin" thật (chưa chạy trên máy dev để giữ rác thật; dùng cùng cơ chế với Delete permanently đã test)
