// Small modal dialogs built from plain controls: a text input box and a password box.
#include "app.h"

namespace {

struct Form {
    HWND h = nullptr;
    bool ok = false, done = false;
    std::function<bool()> onOk;              // read the fields; return false to keep the dialog open
    std::function<void(int id)> onCommand;   // checkboxes etc.
    HWND focus = nullptr;
};

LRESULT CALLBACK FormProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto f = (Form*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (f) {
        switch (m) {
        case WM_COMMAND:
            if (LOWORD(w) == IDOK) { if (!f->onOk || f->onOk()) f->ok = f->done = true; return 0; }
            if (LOWORD(w) == IDCANCEL) { f->done = true; return 0; }
            if (f->onCommand) f->onCommand(LOWORD(w));
            return 0;
        case WM_CLOSE: f->done = true; return 0;
        case WM_ACTIVATE:  // keep typing going to the input after an app switch
            if (LOWORD(w) != WA_INACTIVE && f->focus) { SetFocus(f->focus); return 0; }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            SetBkColor((HDC)w, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
    }
    return DefWindowProcW(h, m, w, l);
}

void FormCreate(Form& f, const wchar_t* title, int cw, int ch) {
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = FormProc;
        wc.hInstance = g_inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"NxForm";
        RegisterClassW(&wc);
        reg = true;
    }
    RECT rc{0, 0, cw, ch};
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int w = rc.right - rc.left, hh = rc.bottom - rc.top;
    RECT mr; GetWindowRect(g_main, &mr);
    f.h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"NxForm", title, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                          (mr.left + mr.right - w) / 2, (mr.top + mr.bottom - hh) / 2, w, hh, g_main, nullptr, g_inst, nullptr);
}

HWND Ctl(Form& f, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id = 0, DWORD ex = 0) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, f.h, (HMENU)(INT_PTR)id, g_inst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, FALSE);
    return c;
}

bool FormRun(Form& f) {
    SetWindowLongPtrW(f.h, GWLP_USERDATA, (LONG_PTR)&f);
    EnableWindow(g_main, FALSE);
    ShowWindow(f.h, SW_SHOW);
    if (f.focus) { SetFocus(f.focus); SendMessageW(f.focus, EM_SETSEL, 0, -1); }
    MSG msg;
    while (!f.done) {
        if (GetMessageW(&msg, nullptr, 0, 0) <= 0) { PostQuitMessage((int)msg.wParam); break; }
        if (!IsDialogMessageW(f.h, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(g_main, TRUE);
    DestroyWindow(f.h);
    SetForegroundWindow(g_main);
    return f.ok;
}

wstring Text(HWND c) {
    wchar_t b[1024];
    GetWindowTextW(c, b, 1024);
    return b;
}

}  // namespace

bool InputBox(const wchar_t* title, const wchar_t* prompt, wstring& value) {
    Form f;
    FormCreate(f, title, S(360), S(122));
    Ctl(f, L"STATIC", prompt, 0, S(14), S(12), S(332), S(20));
    HWND edit = Ctl(f, L"EDIT", value.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, S(14), S(36), S(332), S(26), 0, WS_EX_CLIENTEDGE);
    Ctl(f, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, S(182), S(80), S(80), S(28), IDOK);
    Ctl(f, L"BUTTON", L"Cancel", WS_TABSTOP, S(266), S(80), S(80), S(28), IDCANCEL);
    f.focus = edit;
    wstring result;
    f.onOk = [&] { result = Text(edit); return true; };
    if (!FormRun(f)) return false;
    value = result;
    return true;
}

bool PasswordBox(const wchar_t* title, wstring& password, bool& encryptNames) {
    enum { ID_SHOW = 100, ID_NAMES };
    Form f;
    FormCreate(f, title, S(380), S(232));
    Ctl(f, L"STATIC", L"Password:", 0, S(14), S(12), S(352), S(20));
    HWND p1 = Ctl(f, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, S(14), S(34), S(352), S(26), 0, WS_EX_CLIENTEDGE);
    Ctl(f, L"STATIC", L"Confirm password:", 0, S(14), S(68), S(352), S(20));
    HWND p2 = Ctl(f, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, S(14), S(90), S(352), S(26), 0, WS_EX_CLIENTEDGE);
    HWND show = Ctl(f, L"BUTTON", L"Show password", WS_TABSTOP | BS_AUTOCHECKBOX, S(14), S(124), S(352), S(22), ID_SHOW);
    HWND names = Ctl(f, L"BUTTON", L"Encrypt file names (the file list is hidden without the password)", WS_TABSTOP | BS_AUTOCHECKBOX,
                     S(14), S(148), S(352), S(22), ID_NAMES);
    Ctl(f, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, S(202), S(190), S(80), S(28), IDOK);
    Ctl(f, L"BUTTON", L"Cancel", WS_TABSTOP, S(286), S(190), S(80), S(28), IDCANCEL);
    SendMessageW(names, BM_SETCHECK, encryptNames ? BST_CHECKED : BST_UNCHECKED, 0);
    wchar_t dot = (wchar_t)SendMessageW(p1, EM_GETPASSWORDCHAR, 0, 0);
    f.focus = p1;
    f.onCommand = [&](int id) {
        if (id != ID_SHOW) return;
        bool visible = SendMessageW(show, BM_GETCHECK, 0, 0) == BST_CHECKED;
        for (HWND e : {p1, p2}) { SendMessageW(e, EM_SETPASSWORDCHAR, visible ? 0 : dot, 0); InvalidateRect(e, nullptr, TRUE); }
    };
    wstring result;
    bool encrypt = encryptNames;
    f.onOk = [&] {  // read everything here: the controls are gone once the dialog closes
        wstring a = Text(p1), b = Text(p2);
        const wchar_t* err = a.empty() ? L"Please enter a password."
                           : a != b ? L"The passwords do not match."
                           : a.find(L'"') != wstring::npos ? L"The password can't contain the \" character." : nullptr;
        if (err) { MessageBoxW(f.h, err, title, MB_ICONWARNING); SetFocus(p1); return false; }
        // Vietnamese typing tools (Unikey, EVKey...) silently turn "a1"/"as" into "á" etc.
        auto accented = std::find_if(a.begin(), a.end(), [](wchar_t c) { return c > 127; });
        if (accented != a.end()) {
            wstring q = L"The password contains accented / non-ASCII characters (e.g. \"" + wstring(1, *accented) + L"\").\n\n"
                        L"This often happens when Vietnamese typing (Unikey, EVKey...) is on, and the password then differs from what you meant.\n\n"
                        L"Use this password anyway?";
            if (MessageBoxW(f.h, q.c_str(), title, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) { SetFocus(p1); return false; }
        }
        result = a;
        encrypt = SendMessageW(names, BM_GETCHECK, 0, 0) == BST_CHECKED;
        return true;
    };
    if (!FormRun(f)) return false;
    password = result;
    encryptNames = encrypt;
    return true;
}
