// Backgrounds: a picture behind the whole UI (any theme), the Glass theme (blurred wallpaper aligned to the
// monitor, like frosted glass over the desktop) and whole-window opacity.
// Every custom-drawn surface calls Surface(): it copies its part of the backdrop and lays the theme colour over it.
#include "theme.h"
#include "app.h"
#include <wincodec.h>
#include <commdlg.h>

namespace {

struct Source { wstring path; HBITMAP bmp = nullptr; int w = 0, h = 0; };
struct Cache { HBITMAP bmp = nullptr; HDC dc = nullptr; int w = 0, h = 0; bool screen = false, blur = false; wstring src; };

Source g_src;
Cache g_bd;
HDC g_tint;            // 1x1 DIB used to alpha-blend a colour
wstring g_picture;
int g_opacity = -1;
bool g_loaded = false;

void LoadSettings() {
    if (g_loaded) return;
    g_loaded = true;
    g_picture = IniGet(L"Window", L"picture");
    g_opacity = std::max(50, std::min(100, _wtoi(IniGet(L"Window", L"opacity", L"100").c_str())));
}

HBITMAP Dib(int w, int h) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    return CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
}

// any format WIC knows (JPG, PNG, BMP, GIF, TIFF, HEIC / WebP with the Store codecs)
bool Decode(const wstring& path, Source& out) {
    IWICImagingFactory* f = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)))) return false;
    bool ok = false;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    UINT w = 0, h = 0;
    if (SUCCEEDED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) &&
        SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(f->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(conv->GetSize(&w, &h)) && w && h) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = (LONG)w;
        bi.bmiHeader.biHeight = -(LONG)h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bmp && SUCCEEDED(conv->CopyPixels(nullptr, w * 4, w * h * 4, (BYTE*)bits))) {
            out.bmp = bmp;
            out.w = (int)w;
            out.h = (int)h;
            out.path = path;
            ok = true;
        } else if (bmp) {
            DeleteObject(bmp);
        }
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    f->Release();
    return ok;
}

wstring SourcePath() {
    if (!g_picture.empty()) return g_picture;
    if (!g_theme->glass) return L"";
    wchar_t wp[MAX_PATH] = L"";  // Glass without a picture: the desktop wallpaper
    SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, wp, 0);
    return wp;
}

void Release(Cache& c) {
    if (c.dc) DeleteDC(c.dc);
    if (c.bmp) DeleteObject(c.bmp);
    c = Cache{};
}

// stretch with smoothing; a small copy blown up again is a cheap, soft blur
void Stretch(HDC dst, int dx, int dy, int dw, int dh, HDC src, int sx, int sy, int sw, int sh) {
    SetStretchBltMode(dst, HALFTONE);
    SetBrushOrgEx(dst, 0, 0, nullptr);
    StretchBlt(dst, dx, dy, dw, dh, src, sx, sy, sw, sh, SRCCOPY);
}

bool Ensure() {
    bool screen = g_theme->glass > 0;
    int w, h;
    if (screen) {
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTONEAREST), &mi);
        w = mi.rcMonitor.right - mi.rcMonitor.left;
        h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    } else {
        RECT rc; GetClientRect(g_main, &rc);
        w = rc.right;
        h = rc.bottom;
    }
    if (w <= 0 || h <= 0) return false;
    wstring path = SourcePath();
    if (g_bd.dc && g_bd.w == w && g_bd.h == h && g_bd.screen == screen && g_bd.src == path) return true;
    Release(g_bd);
    if (!path.empty() && g_src.path != path) {
        if (g_src.bmp) DeleteObject(g_src.bmp);
        g_src = Source{};
        if (!Decode(path, g_src)) g_src.path = path;  // remember the failure, don't retry on every paint
    }
    HDC screenDc = GetDC(nullptr);
    g_bd.dc = CreateCompatibleDC(screenDc);
    ReleaseDC(nullptr, screenDc);
    g_bd.bmp = Dib(w, h);
    SelectObject(g_bd.dc, g_bd.bmp);
    g_bd.w = w;
    g_bd.h = h;
    g_bd.screen = screen;
    g_bd.src = path;
    if (g_src.bmp) {  // "cover": fill the whole area, crop the overflow, keep the aspect ratio
        HDC s = CreateCompatibleDC(g_bd.dc);
        HGDIOBJ old = SelectObject(s, g_src.bmp);
        double k = std::max((double)w / g_src.w, (double)h / g_src.h);
        int cw = (int)(w / k), ch = (int)(h / k);
        int cx = (g_src.w - cw) / 2, cy = (g_src.h - ch) / 2;
        if (screen) {  // Glass: blur by shrinking to 1/24 and stretching back (twice for a smooth result)
            int bw = std::max(8, w / 24), bh = std::max(8, h / 24);
            HDC small = CreateCompatibleDC(g_bd.dc);
            HBITMAP sb = Dib(bw, bh);
            HGDIOBJ os = SelectObject(small, sb);
            Stretch(small, 0, 0, bw, bh, s, cx, cy, cw, ch);
            Stretch(g_bd.dc, 0, 0, w, h, small, 0, 0, bw, bh);
            Stretch(small, 0, 0, bw, bh, g_bd.dc, 0, 0, w, h);
            Stretch(g_bd.dc, 0, 0, w, h, small, 0, 0, bw, bh);
            SelectObject(small, os);
            DeleteObject(sb);
            DeleteDC(small);
        } else {
            Stretch(g_bd.dc, 0, 0, w, h, s, cx, cy, cw, ch);
        }
        SelectObject(s, old);
        DeleteDC(s);
    } else {  // no picture: a soft diagonal gradient in the theme colours
        TRIVERTEX v[2] = {{0, 0, 0, 0, 0, 0}, {w, h, 0, 0, 0, 0}};
        COLORREF a = g_theme->accent, b = g_theme->app;
        v[0].Red = (COLOR16)(GetRValue(a) * 160); v[0].Green = (COLOR16)(GetGValue(a) * 160); v[0].Blue = (COLOR16)(GetBValue(a) * 160);
        v[1].Red = (COLOR16)(GetRValue(b) << 8); v[1].Green = (COLOR16)(GetGValue(b) << 8); v[1].Blue = (COLOR16)(GetBValue(b) << 8);
        GRADIENT_RECT gr{0, 1};
        GradientFill(g_bd.dc, v, 2, &gr, 1, GRADIENT_FILL_RECT_V);
    }
    return true;
}

HDC TintDc() {
    if (!g_tint) {
        HDC screenDc = GetDC(nullptr);
        g_tint = CreateCompatibleDC(screenDc);
        ReleaseDC(nullptr, screenDc);
        SelectObject(g_tint, Dib(1, 1));
    }
    return g_tint;
}

}  // namespace

bool BackdropActive() {
    LoadSettings();
    return g_theme->glass > 0 || !g_picture.empty();
}

bool BackdropFollowsScreen() { return g_theme->glass > 0; }

void Surface(HDC dc, HWND hwnd, const RECT& r, COLORREF c) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    if (!BackdropActive() || !g_main || !Ensure()) { Fill(dc, r, c); return; }
    POINT o{r.left, r.top};
    if (g_bd.screen) {
        ClientToScreen(hwnd, &o);
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(g_main, MONITOR_DEFAULTTONEAREST), &mi);
        o.x -= mi.rcMonitor.left;
        o.y -= mi.rcMonitor.top;
    } else if (hwnd != g_main) {
        MapWindowPoints(hwnd, g_main, &o, 1);
    }
    int w = r.right - r.left, h = r.bottom - r.top;
    BitBlt(dc, r.left, r.top, w, h, g_bd.dc, o.x, o.y, SRCCOPY);
    SetPixelV(TintDc(), 0, 0, c);
    BYTE alpha = (BYTE)(g_theme->glass ? g_theme->glass : 205);  // a picture with a normal theme stays subtle
    BLENDFUNCTION bf{AC_SRC_OVER, 0, alpha, 0};
    AlphaBlend(dc, r.left, r.top, w, h, TintDc(), 0, 0, 1, 1, bf);
}

void BackdropReset() { Release(g_bd); }

wstring BackdropPicture() { LoadSettings(); return g_picture; }

void BackdropChoosePicture() {
    LoadSettings();
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW of{sizeof(of)};
    of.hwndOwner = g_main;
    of.lpstrFilter = L"Pictures\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.tif;*.tiff;*.webp;*.heic\0All files\0*.*\0";
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.lpstrTitle = L"Background picture";
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return;
    Source test;
    if (!Decode(file, test)) {
        MessageBoxW(g_main, L"This picture can't be opened.", L"Background picture", MB_ICONWARNING);
        return;
    }
    DeleteObject(test.bmp);
    g_picture = file;
    IniSet(L"Window", L"picture", g_picture);
    BackdropReset();
    ThemeChanged();
}

void BackdropClearPicture() {
    LoadSettings();
    g_picture.clear();
    IniSet(L"Window", L"picture", L"");
    BackdropReset();
    ThemeChanged();
}

int WindowOpacity() { LoadSettings(); return g_opacity; }

void ApplyWindowOpacity() {
    LoadSettings();
    LONG_PTR ex = GetWindowLongPtrW(g_main, GWL_EXSTYLE);
    if (g_opacity >= 100) {
        if (ex & WS_EX_LAYERED) SetWindowLongPtrW(g_main, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    } else {
        if (!(ex & WS_EX_LAYERED)) SetWindowLongPtrW(g_main, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(g_main, 0, (BYTE)(255 * g_opacity / 100), LWA_ALPHA);
    }
    RedrawWindow(g_main, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void SetWindowOpacity(int pct) {
    LoadSettings();
    g_opacity = std::max(50, std::min(100, pct));
    IniSet(L"Window", L"opacity", std::to_wstring(g_opacity));
    ApplyWindowOpacity();
}
