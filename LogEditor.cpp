// PersonLog - single-file C++ Win32 work-log editor.
// Log storage: LOG/YYYY/MM/DD.DIL  (RTF, GBK/CP936).
// Build: build.bat (MinGW) or MSVC. Zero external deps beyond Win32 SDK.
#define UNICODE
#define _UNICODE
// MINGW: UNICODE must be defined before any includes, otherwise SendMessage
// maps to SendMessageA and all LB_ADDSTRING/W-API calls will mangle WCHAR* data.
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cwctype>
#include "resource.h"

#define IDC_DATEPICKER      1001
#define IDC_RICHEDIT        1002
#define IDC_BTN_SAVE        1003
#define IDC_SEARCH          1004
#define IDC_RESULTLIST      1005
#define IDC_BTN_SEARCH      1006
#define IDC_BTN_CLOSERES    1007
#define IDC_BTN_FONT        1008
#define IDC_BTN_EXPORT      1009

#define IDC_EX_FROM         2001
#define IDC_EX_TO           2002

HINSTANCE   g_hInst;
HWND        g_hDatePicker, g_hRichEdit, g_hBtnSave;
HWND        g_hSearch, g_hBtnSearch, g_hResultList, g_hBtnCloseRes, g_hBtnFont, g_hBtnExport;
HMODULE     g_hRichEditLib;
const WCHAR* g_szRichEditClass;
WCHAR       g_szLogDir[MAX_PATH];
BOOL        g_bModified;
BOOL        g_bResultsOn;
SYSTEMTIME  g_curDate;
#define RESULT_W 100

// Search cache
BOOL                  g_bCacheLoaded  = FALSE;
struct LogEntry { SYSTEMTIME date; WCHAR path[MAX_PATH]; std::wstring plain; };
std::vector<LogEntry> g_cache;
std::vector<int>      g_results;
WNDPROC               g_oldSearchProc, g_oldListProc;
#define MAX_RESULTS 500

// Forward
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void    InitControls(HWND hWnd);
void    ReLayout(HWND hWnd, int cx, int cy);
void    OnSave(HWND hWnd);
void    OnFont(HWND hWnd);
void    OnExport(HWND hWnd);
INT_PTR CALLBACK ExportDlgProc(HWND, UINT, WPARAM, LPARAM);
BOOL    LoadLogFile(const SYSTEMTIME* pst);
BOOL    SaveLogFile(const SYSTEMTIME* pst);
void    BuildFilePath(const SYSTEMTIME* pst, WCHAR* buf, int n);
void    BuildDirPath(const SYSTEMTIME* pst, WCHAR* buf, int n);
BOOL    EnsureDir(const WCHAR* path);
void    SetMod(BOOL m);
BOOL    PromptSave(HWND hWnd);

struct StreamCookie { std::vector<BYTE>* data; size_t pos; };
DWORD   CALLBACK StreamInCB(DWORD_PTR, LPBYTE, LONG, LONG*);
DWORD   CALLBACK StreamOutCB(DWORD_PTR, LPBYTE, LONG, LONG*);

void    BuildCache();
std::wstring RtfToPlain(const std::vector<BYTE>& rtf);
void    DoSearch();
void    ShowResults(BOOL show);
void    NavToResult(int idx);
LRESULT CALLBACK SearchProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK ListProc(HWND, UINT, WPARAM, LPARAM);

// File I/O
static std::vector<BYTE> ReadFileToBuf(const WCHAR* path)
{
    std::vector<BYTE> buf;
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return buf;
    DWORD sz = GetFileSize(h, NULL);
    if (sz > 0) { buf.resize(sz); DWORD rd = 0; ReadFile(h, buf.data(), sz, &rd, NULL); }
    CloseHandle(h);
    return buf;
}

static BOOL WriteBufToFile(const WCHAR* path, const std::vector<BYTE>& buf)
{
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0,
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD wr = 0;
    WriteFile(h, buf.data(), (DWORD)buf.size(), &wr, NULL);
    CloseHandle(h);
    return TRUE;
}

static void MsgBox(LPCWSTR text) { MessageBoxW(NULL, text, L"\u63D0\u793A", MB_OK | MB_ICONINFORMATION); }

// ================================================================
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    g_hInst = hInstance;
    GetModuleFileNameW(NULL, g_szLogDir, MAX_PATH);
    WCHAR* p = wcsrchr(g_szLogDir, L'\\');
    if (p) *p = 0;
    wcscat(g_szLogDir, L"\\LOG");

    // Load RichEdit 4.1 (Vista+) or fall back to RichEdit 2.0 (Win2k+)
    g_hRichEditLib = LoadLibraryW(L"Msftedit.dll");
    g_szRichEditClass = g_hRichEditLib ? L"RICHEDIT50W" : L"RichEdit20W";
    if (!g_hRichEditLib) g_hRichEditLib = LoadLibraryW(L"RichEd20.dll");

    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_DATE_CLASSES };
    InitCommonControlsEx(&icex);
    GetLocalTime(&g_curDate);

    HICON hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_MAIN));

    WNDCLASSEXW wc; memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hIcon         = hIcon;
    wc.hIconSm       = hIcon;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"LogEditorWnd";
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = 800, wh = 600;
    HWND hWnd = CreateWindowExW(0, L"LogEditorWnd",
        L"\u5DE5\u4F5C\u65E5\u5FD7\u7F16\u8F91\u5668",
        WS_OVERLAPPEDWINDOW,
        (sw - ww) / 2, (sh - wh) / 2, ww, wh,
        NULL, NULL, hInstance, NULL);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    if (g_hRichEditLib) FreeLibrary(g_hRichEditLib);
    return (int)msg.wParam;
}

// ================================================================
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        InitControls(hWnd);
        LoadLogFile(&g_curDate);
        break;

    case WM_SIZE:
        ReLayout(hWnd, LOWORD(lParam), HIWORD(lParam));
        break;

    case WM_NOTIFY:
    {
        LPNMHDR pnm = (LPNMHDR)lParam;
        if (pnm->idFrom == IDC_DATEPICKER && pnm->code == DTN_DATETIMECHANGE)
        {
            if (!PromptSave(hWnd))
            {
                SendMessage(g_hDatePicker, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&g_curDate);
                return 0;
            }
            LPNMDATETIMECHANGE pdt = (LPNMDATETIMECHANGE)lParam;
            if (pdt->dwFlags == GDT_VALID)
            {
                g_curDate = pdt->st;
                LoadLogFile(&g_curDate);
            }
        }
        break;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_BTN_SAVE && HIWORD(wParam) == BN_CLICKED)
            OnSave(hWnd);
        else if (LOWORD(wParam) == IDC_RICHEDIT && HIWORD(wParam) == EN_CHANGE)
            SetMod(TRUE);
        else if (LOWORD(wParam) == IDC_BTN_SEARCH && HIWORD(wParam) == BN_CLICKED)
            DoSearch();
        else if (LOWORD(wParam) == IDC_BTN_FONT && HIWORD(wParam) == BN_CLICKED)
            OnFont(hWnd);
        else if (LOWORD(wParam) == IDC_BTN_EXPORT && HIWORD(wParam) == BN_CLICKED)
            OnExport(hWnd);
        else if (LOWORD(wParam) == IDC_BTN_CLOSERES && HIWORD(wParam) == BN_CLICKED)
            ShowResults(FALSE);
        else if (LOWORD(wParam) == IDC_RESULTLIST && HIWORD(wParam) == LBN_SELCHANGE)
        {
            int idx = (int)SendMessage(g_hResultList, LB_GETCURSEL, 0, 0);
            if (idx >= 0) NavToResult(idx);
        }
        break;

    case WM_CTLCOLORLISTBOX:
        if ((HWND)lParam == g_hResultList)
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        break;

    case WM_KEYDOWN:
        if (wParam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000))
        { SetFocus(g_hSearch); SendMessage(g_hSearch, EM_SETSEL, 0, -1); return 0; }
        if (wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000))
        { OnSave(hWnd); return 0; }
        break;

    case WM_CLOSE:
        if (!PromptSave(hWnd)) return 0;
        DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// ================================================================
void InitControls(HWND hWnd)
{
    int y0 = 6, hBar = 24, gap = 6, x = gap;

    // Layout: [DatePicker] [Save] [Search____] [Query]

    // [1] DatePicker
    int dpW = 130;
    g_hDatePicker = CreateWindowExW(0, DATETIMEPICK_CLASSW, NULL,
        WS_CHILD | WS_VISIBLE | DTS_SHORTDATECENTURYFORMAT,
        x, y0, dpW, hBar, hWnd, (HMENU)IDC_DATEPICKER, g_hInst, NULL);
    SendMessage(g_hDatePicker, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&g_curDate);
    x += dpW + gap;

    // [2] Save
    g_hBtnSave = CreateWindowExW(0, L"BUTTON", L"\u4FDD\u5B58",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y0, 55, hBar, hWnd, (HMENU)IDC_BTN_SAVE, g_hInst, NULL);
    x += 55 + gap;

    // [3] Search box
    int srW = 180;
    g_hSearch = CreateWindowExW(0, L"EDIT", NULL,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        x, y0, srW, hBar, hWnd, (HMENU)IDC_SEARCH, g_hInst, NULL);
    SendMessage(g_hSearch, EM_SETCUEBANNER, TRUE, (LPARAM)L"\u641C\u7D22\u65E5\u5FD7... Ctrl+F");
    x += srW + gap;

    // [4] Query button
    g_hBtnSearch = CreateWindowExW(0, L"BUTTON", L"\u67E5\u8BE2",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y0, 50, hBar, hWnd, (HMENU)IDC_BTN_SEARCH, g_hInst, NULL);
    x += 50 + gap;

    // [5] Font button
    g_hBtnFont = CreateWindowExW(0, L"BUTTON", L"\u5B57\u4F53",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y0, 45, hBar, hWnd, (HMENU)IDC_BTN_FONT, g_hInst, NULL);
    x += 45 + gap;

    // [6] Export button
    g_hBtnExport = CreateWindowExW(0, L"BUTTON", L"\u5BFC\u51FA",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        x, y0, 45, hBar, hWnd, (HMENU)IDC_BTN_EXPORT, g_hInst, NULL);

    // [7] Close results X button (initially hidden)
    int listY = y0 + hBar + gap;
    g_hBtnCloseRes = CreateWindowExW(0, L"BUTTON", L"\u00D7",
        WS_CHILD | BS_PUSHBUTTON | BS_FLAT,
        gap, listY, RESULT_W, 20, hWnd, (HMENU)IDC_BTN_CLOSERES, g_hInst, NULL);

    // [8] Result list (initially hidden)
    // LBS_HASSTRINGS is critical: without it the listbox stores lParam
    // as a raw pointer; since line goes out of scope each loop iteration,
    // all entries end up as dangling pointers painting garbage.
    g_hResultList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", NULL,
        WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS,
        gap, listY + 20, RESULT_W, 100, hWnd, (HMENU)IDC_RESULTLIST, g_hInst, NULL);

    // [9] RichEdit
    g_hRichEdit = CreateWindowExW(WS_EX_CLIENTEDGE, g_szRichEditClass, NULL,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        gap, listY, 100, 100, hWnd, (HMENU)IDC_RICHEDIT, g_hInst, NULL);

    SendMessage(g_hRichEdit, EM_SETEVENTMASK, 0, ENM_CHANGE);
    SendMessage(g_hRichEdit, EM_LIMITTEXT, 0x7FFFFFFF, 0);

    CHARFORMAT2W cf; memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_FACE | CFM_SIZE | CFM_CHARSET;
    cf.yHeight = 200;
    cf.bCharSet = GB2312_CHARSET;
    wcscpy(cf.szFaceName, L"\u5B8B\u4F53");
    SendMessage(g_hRichEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);

    // Set listbox font to CJK-capable
    HFONT hListFont = CreateFontW(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        GB2312_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        DEFAULT_QUALITY, FF_DONTCARE, L"\u5B8B\u4F53");
    SendMessage(g_hResultList, WM_SETFONT, (WPARAM)hListFont, TRUE);

    // Subclass search edit and result list to handle Enter/Escape/Arrow keys
    g_oldSearchProc = (WNDPROC)SetWindowLongPtrW(g_hSearch, GWLP_WNDPROC, (LONG_PTR)SearchProc);
    g_oldListProc   = (WNDPROC)SetWindowLongPtrW(g_hResultList, GWLP_WNDPROC, (LONG_PTR)ListProc);
}

// ================================================================
// ---------------------------------------------------------------
//  Layout: when results panel is on, push RichEdit right by RESULT_W.
//  When off, RichEdit takes full width. Called on WM_SIZE and toggle.
// ---------------------------------------------------------------
void ReLayout(HWND hWnd, int cx, int cy)
{
    int gap = 6, y0 = 6, hBar = 24;
    int reY = y0 + hBar + gap;

    (void)hWnd;

    if (g_bResultsOn)
    {
        // Results panel on left: [X button] [listbox]
        int rY = reY;
        int rH = cy - rY - gap;
        if (rH < 50) rH = 50;

        ShowWindow(g_hBtnCloseRes, SW_SHOW);
        ShowWindow(g_hResultList, SW_SHOW);

        MoveWindow(g_hBtnCloseRes, gap, rY, RESULT_W, 20, TRUE);
        MoveWindow(g_hResultList, gap, rY + 20, RESULT_W, rH - 20, TRUE);

        // RichEdit shifted right
        int reX = gap + RESULT_W + gap;
        int reW = cx - reX - gap;
        MoveWindow(g_hRichEdit, reX, reY, reW, rH, TRUE);
    }
    else
    {
        ShowWindow(g_hBtnCloseRes, SW_HIDE);
        ShowWindow(g_hResultList, SW_HIDE);

        int reW = cx - gap * 2;
        int reH = cy - reY - gap;
        if (reH < 50) reH = 50;
        MoveWindow(g_hRichEdit, gap, reY, reW, reH, TRUE);
    }
}

// ================================================================
void OnSave(HWND hWnd)
{
    if (SaveLogFile(&g_curDate))
    {
        SetMod(FALSE);
        g_bCacheLoaded = FALSE;
        g_cache.clear();
    }
    (void)hWnd;
}

void OnFont(HWND hWnd)
{
    CHARFORMAT2W cf;
    memset(&cf, 0, sizeof(cf));
    cf.cbSize = sizeof(cf);
    // Get current selection formatting as dialog defaults
    SendMessage(g_hRichEdit, EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);

    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet        = cf.bCharSet;
    lf.lfPitchAndFamily = cf.bPitchAndFamily;
    wcscpy(lf.lfFaceName, cf.szFaceName);
    HDC hdc = GetDC(hWnd);
    lf.lfHeight = -MulDiv(cf.yHeight / 20, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    if (cf.dwMask & CFM_BOLD   && cf.dwEffects & CFE_BOLD)      lf.lfWeight    = FW_BOLD;
    if (cf.dwMask & CFM_ITALIC && cf.dwEffects & CFE_ITALIC)    lf.lfItalic    = TRUE;
    if (cf.dwMask & CFM_UNDERLINE && cf.dwEffects & CFE_UNDERLINE) lf.lfUnderline = TRUE;
    if (cf.dwMask & CFM_STRIKEOUT && cf.dwEffects & CFE_STRIKEOUT) lf.lfStrikeOut = TRUE;

    CHOOSEFONTW cfont;
    memset(&cfont, 0, sizeof(cfont));
    cfont.lStructSize = sizeof(cfont);
    cfont.hwndOwner   = hWnd;
    cfont.lpLogFont   = &lf;
    cfont.Flags       = CF_SCREENFONTS | CF_EFFECTS | CF_INITTOLOGFONTSTRUCT;
    cfont.rgbColors   = cf.crTextColor;

    if (!ChooseFontW(&cfont)) { ReleaseDC(hWnd, hdc); return; }
    ReleaseDC(hWnd, hdc);

    // Apply chosen formatting to current selection
    memset(&cf, 0, sizeof(cf));
    cf.cbSize  = sizeof(cf);
    cf.dwMask  = CFM_FACE | CFM_SIZE | CFM_COLOR | CFM_CHARSET
               | CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_STRIKEOUT;
    cf.crTextColor = cfont.rgbColors;
    cf.bCharSet    = lf.lfCharSet;
    cf.bPitchAndFamily = lf.lfPitchAndFamily;
    wcscpy(cf.szFaceName, lf.lfFaceName);
    if (lf.lfWeight >= FW_BOLD)  cf.dwEffects |= CFE_BOLD;
    if (lf.lfItalic)             cf.dwEffects |= CFE_ITALIC;
    if (lf.lfUnderline)          cf.dwEffects |= CFE_UNDERLINE;
    if (lf.lfStrikeOut)          cf.dwEffects |= CFE_STRIKEOUT;

    hdc = GetDC(hWnd);
    cf.yHeight = lf.lfHeight ? (-lf.lfHeight * 1440 / GetDeviceCaps(hdc, LOGPIXELSY)) : 200;
    ReleaseDC(hWnd, hdc);

    SendMessage(g_hRichEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    SetFocus(g_hRichEdit);
    SetMod(TRUE);
}

BOOL SaveLogFile(const SYSTEMTIME* pst)
{
    WCHAR path[MAX_PATH], dir[MAX_PATH];
    BuildFilePath(pst, path, MAX_PATH);
    BuildDirPath(pst, dir, MAX_PATH);
    if (!EnsureDir(dir)) { MsgBox(L"\u65E0\u6CD5\u521B\u5EFA\u76EE\u5F55"); return FALSE; }

    // Use EM_STREAMOUT to get RTF from RichEdit into a buffer,
    // then write the buffer to file as raw bytes.
    StreamCookie ck; std::vector<BYTE> buf;
    ck.data = &buf; ck.pos = 0;
    EDITSTREAM es = { (DWORD_PTR)&ck, 0, StreamOutCB };
    SendMessage(g_hRichEdit, EM_STREAMOUT, SF_RTF, (LPARAM)&es);

    if (!WriteBufToFile(path, buf)) { MsgBox(L"\u65E0\u6CD5\u5199\u5165\u6587\u4EF6"); return FALSE; }
    return TRUE;
}

BOOL LoadLogFile(const SYSTEMTIME* pst)
{
    WCHAR path[MAX_PATH];
    BuildFilePath(pst, path, MAX_PATH);
    std::vector<BYTE> buf = ReadFileToBuf(path);
    SetWindowTextW(g_hRichEdit, L"");
    if (buf.empty()) { SetMod(FALSE); return FALSE; }
    if (buf.size() >= 5)
    {
        std::string head((char*)buf.data(), 5);
        if (head != "{\\rtf") { SetMod(FALSE); return FALSE; }
    }
    StreamCookie ck; ck.data = &buf; ck.pos = 0;
    EDITSTREAM es = { (DWORD_PTR)&ck, 0, StreamInCB };
    SendMessage(g_hRichEdit, EM_STREAMIN, SF_RTF, (LPARAM)&es);
    SetMod(FALSE);
    return TRUE;
}

DWORD CALLBACK StreamInCB(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb)
{
    StreamCookie* sc = (StreamCookie*)dwCookie;
    size_t remain = sc->data->size() - sc->pos;
    LONG n = (LONG)(cb < (LONG)remain ? cb : (LONG)remain);
    if (n > 0) memcpy(pbBuff, sc->data->data() + sc->pos, n);
    sc->pos += n; *pcb = n;
    return 0;
}

DWORD CALLBACK StreamOutCB(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb)
{
    StreamCookie* sc = (StreamCookie*)dwCookie;
    sc->data->insert(sc->data->end(), pbBuff, pbBuff + cb);
    *pcb = cb;
    return 0;
}

void BuildFilePath(const SYSTEMTIME* pst, WCHAR* buf, int n)
{
    _snwprintf(buf, n, L"%s\\%04d\\%02d\\%02d.DIL",
        g_szLogDir, pst->wYear, pst->wMonth, pst->wDay);
}

void BuildDirPath(const SYSTEMTIME* pst, WCHAR* buf, int n)
{
    if (buf && n > 0)
        _snwprintf(buf, n, L"%s\\%04d\\%02d", g_szLogDir, pst->wYear, pst->wMonth);
}

// Recursively create intermediate directories (mkdir -p equivalent)
BOOL EnsureDir(const WCHAR* path)
{
    WCHAR tmp[MAX_PATH]; wcscpy(tmp, path);
    for (WCHAR* s = tmp; *s; s++)
    {
        if (*s == L'\\' || *s == L'/')
        {
            WCHAR saved = *s; *s = 0;
            if (GetFileAttributesW(tmp) == INVALID_FILE_ATTRIBUTES)
                CreateDirectoryW(tmp, NULL);
            *s = saved;
        }
    }
    if (GetFileAttributesW(tmp) == INVALID_FILE_ATTRIBUTES)
        CreateDirectoryW(tmp, NULL);
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

void SetMod(BOOL m) { g_bModified = m; }

BOOL PromptSave(HWND hWnd)
{
    if (!g_bModified) return TRUE;
    int ret = MessageBoxW(hWnd,
        L"\u5F53\u524D\u65E5\u5FD7\u5DF2\u4FEE\u6539\uFF0C\u662F\u5426\u4FDD\u5B58\uFF1F",
        L"\u63D0\u793A", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (ret == IDYES) return SaveLogFile(&g_curDate);
    if (ret == IDNO)  { SetMod(FALSE); return TRUE; }
    return FALSE;
}

// ================================================================
// RTF -> Plain text
// ================================================================
// ---------------------------------------------------------------
//  RTF parser — strips markup from LOG/YYYY/MM/DD.DIL files.
//  Underlying encoding is GBK (CP936). Output is native WCHAR.
//  Three non-obvious pitfalls:
//   1. Control words like \deff0 carry a numeric suffix; must skip
//      letters AND digits, else "0" leaks into text.
//   2. \fonttbl { ... } group contains hex escapes and plain ASCII
//      that are NOT user content. Use brace-depth (depth<=1) guard.
//   3. \pard is a control word, NOT \par + 'd'. Check boundary after
//      "par" to avoid matching substrings.
// ---------------------------------------------------------------
std::wstring RtfToPlain(const std::vector<BYTE>& rtf)
{
    if (rtf.size() < 5) return L"";
    std::string gbk;
    const char* p = (const char*)rtf.data();
    const char* end = p + rtf.size();
    int depth = 0;

    while (p < end)
    {
        if (*p == '{') { depth++; p++; continue; }
        if (*p == '}') { depth--; p++; continue; }
        if (*p == '\r' || *p == '\n') { p++; continue; }
        if (*p == '\\')
        {
            p++;
            if (p >= end) break;
            if (*p == '\'')    // hex escape: \'xx
            {
                if (p + 2 < end && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2]))
                {
                    if (depth <= 1)  // skip fonttbl glyph data
                    {
                        char hex[3] = { p[1], p[2], 0 };
                        gbk += (char)strtol(hex, NULL, 16);
                    }
                    p += 3;
                }
                else { p++; }
            }
            else if (*p == 'p' && (p + 2 < end) && p[1] == 'a' && p[2] == 'r'
                && (p + 3 >= end || !isalpha((unsigned char)p[3]))) // \par but not \pard
            {
                if (depth <= 1) gbk += "\r\n";
                p += 3;
                if (p < end && *p == ' ') p++;
            }
            else if (*p == 't' && (p + 2 < end) && p[1] == 'a' && p[2] == 'b'
                && (p + 3 >= end || !isalpha((unsigned char)p[3]))) // \tab but not \table
            {
                if (depth <= 1) gbk += '\t';
                p += 3;
                if (p < end && *p == ' ') p++;
            }
            else   // generic control word: skip letters + numeric param
            {
                while (p < end && isalpha((unsigned char)*p)) p++;
                while (p < end && isdigit((unsigned char)*p)) p++;
                if (p < end && *p == '-') { p++; while (p < end && isdigit((unsigned char)*p)) p++; }
                if (p < end && *p == ' ') p++;
            }
        }
        else
        {
            if (depth <= 1) gbk += *p;  // plain ASCII, only at top level
            p++;
        }
    }

    // Convert GBK byte-stream -> native WCHAR (CP 936 = GBK)
    if (gbk.empty()) return L"";
    int wlen = MultiByteToWideChar(936, 0, gbk.c_str(), -1, NULL, 0);
    if (wlen <= 0) return L"";
    std::wstring result(wlen, L'\0');
    MultiByteToWideChar(936, 0, gbk.c_str(), -1, &result[0], wlen);
    while (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}

// ================================================================
// Lazy cache build
// ================================================================
// ---------------------------------------------------------------
//  Build search cache by scanning LOG/YYYY/MM/*.DIL recursively.
//  Called lazily on first search query (not on startup).
//  Cache is invalidated every time the user saves a file.
// ---------------------------------------------------------------
void BuildCache()
{
    if (g_bCacheLoaded) return;
    g_cache.clear();
    g_cache.reserve(4000);

    WCHAR basePat[MAX_PATH];
    _snwprintf(basePat, MAX_PATH, L"%s\\*", g_szLogDir);
    WIN32_FIND_DATAW fdY;
    HANDLE hY = FindFirstFileW(basePat, &fdY);
    if (hY == INVALID_HANDLE_VALUE) { g_bCacheLoaded = TRUE; return; }

    do {
        if (!(fdY.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fdY.cFileName[0] == L'.') continue;
        int yr = _wtoi(fdY.cFileName);
        if (yr < 2000 || yr > 2100) continue;

        WCHAR monPat[MAX_PATH];
        _snwprintf(monPat, MAX_PATH, L"%s\\%s\\*", g_szLogDir, fdY.cFileName);
        WIN32_FIND_DATAW fdM;
        HANDLE hM = FindFirstFileW(monPat, &fdM);
        if (hM == INVALID_HANDLE_VALUE) continue;

        do {
            if (!(fdM.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (fdM.cFileName[0] == L'.') continue;
            int mon = _wtoi(fdM.cFileName);
            if (mon < 1 || mon > 12) continue;

            WCHAR dayPat[MAX_PATH];
            _snwprintf(dayPat, MAX_PATH, L"%s\\%s\\%s\\*.DIL", g_szLogDir, fdY.cFileName, fdM.cFileName);
            WIN32_FIND_DATAW fdD;
            HANDLE hD = FindFirstFileW(dayPat, &fdD);
            if (hD == INVALID_HANDLE_VALUE) continue;

    // Walk year/month/day directory tree: LOG -> YYYY -> MM -> DD.DIL
    do {
                if (fdD.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                WCHAR nm[3] = { fdD.cFileName[0], fdD.cFileName[1], 0 };
                int day = _wtoi(nm);
                if (day < 1 || day > 31) continue;

                LogEntry e; memset(&e.date, 0, sizeof(e.date));
                e.date.wYear = (WORD)yr; e.date.wMonth = (WORD)mon; e.date.wDay = (WORD)day;
                _snwprintf(e.path, MAX_PATH, L"%s\\%s\\%s\\%s",
                    g_szLogDir, fdY.cFileName, fdM.cFileName, fdD.cFileName);

                std::vector<BYTE> rtf = ReadFileToBuf(e.path);
                e.plain = RtfToPlain(rtf);
                g_cache.push_back(e);
            } while (FindNextFileW(hD, &fdD));
            FindClose(hD);
        } while (FindNextFileW(hM, &fdM));
        FindClose(hM);
    } while (FindNextFileW(hY, &fdY));
    FindClose(hY);

    g_bCacheLoaded = TRUE;
}

// ================================================================
void DoSearch()
{
    if (!g_bCacheLoaded) BuildCache();

    WCHAR qraw[256]; GetWindowTextW(g_hSearch, qraw, 256);
    std::wstring q(qraw);
    for (auto& c : q) c = (WCHAR)towlower(c);

    SendMessage(g_hResultList, LB_RESETCONTENT, 0, 0);
    g_results.clear();

    if (q.empty()) { ShowResults(FALSE); return; }

    for (int i = 0; i < (int)g_cache.size() && (int)g_results.size() < MAX_RESULTS; i++)
    {
        std::wstring txt = g_cache[i].plain;
        for (auto& c : txt) c = (WCHAR)towlower(c);
        if (txt.find(q) == std::wstring::npos) continue;

        g_results.push_back(i);

        WORD yr = g_cache[i].date.wYear, mo = g_cache[i].date.wMonth, dy = g_cache[i].date.wDay;
        WCHAR d[5];
        d[0]=(WCHAR)(L'0'+yr/1000); d[1]=(WCHAR)(L'0'+(yr/100)%10);
        d[2]=(WCHAR)(L'0'+(yr/10)%10); d[3]=(WCHAR)(L'0'+yr%10); d[4]=0;
        std::wstring line; line += d; line += L'\\';
        d[0]=(WCHAR)(L'0'+mo/10); d[1]=(WCHAR)(L'0'+mo%10); d[2]=0;
        line += d; line += L'\\';
        d[0]=(WCHAR)(L'0'+dy/10); d[1]=(WCHAR)(L'0'+dy%10); d[2]=0;
        line += d;

        SendMessage(g_hResultList, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }

    ShowResults(!g_results.empty());
}

void ShowResults(BOOL show)
{
    g_bResultsOn = show;
    HWND hWnd = GetParent(g_hRichEdit);
    RECT rc; GetClientRect(hWnd, &rc);
    ReLayout(hWnd, rc.right, rc.bottom);
    InvalidateRect(hWnd, NULL, TRUE);
}

void NavToResult(int idx)
{
    if (idx < 0 || idx >= (int)g_results.size()) return;
    const LogEntry& e = g_cache[g_results[idx]];
    HWND hWnd = GetParent(g_hDatePicker);

    if (memcmp(&e.date, &g_curDate, sizeof(SYSTEMTIME)) != 0)
    {
        if (!PromptSave(hWnd)) return;
        g_curDate = e.date;
        SendMessage(g_hDatePicker, DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&g_curDate);
        LoadLogFile(&g_curDate);
    }

    SetFocus(g_hRichEdit);
}

// ================================================================
LRESULT CALLBACK SearchProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        { SetWindowTextW(hWnd, L""); ShowResults(FALSE); SetFocus(g_hRichEdit); return 0; }
        if (wParam == VK_RETURN)
        { DoSearch(); return 0; }
        if (wParam == VK_DOWN)
        { if (g_bResultsOn && !g_results.empty()) SetFocus(g_hResultList); return 0; }
        break;
    }
    return CallWindowProcW(g_oldSearchProc, hWnd, msg, wParam, lParam);
}

LRESULT CALLBACK ListProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN)
        { int idx = (int)SendMessage(hWnd, LB_GETCURSEL, 0, 0); if (idx >= 0) NavToResult(idx); return 0; }
        if (wParam == VK_ESCAPE)
        { SetWindowTextW(g_hSearch, L""); ShowResults(FALSE); SetFocus(g_hRichEdit); return 0; }
        if (wParam == VK_UP)
        { int idx = (int)SendMessage(hWnd, LB_GETCURSEL, 0, 0); if (idx <= 0) SetFocus(g_hSearch); return 0; }
        break;
    }
    return CallWindowProcW(g_oldListProc, hWnd, msg, wParam, lParam);
}

// ================================================================
//  Export dialog — date range picker
// ================================================================
static SYSTEMTIME g_exFrom, g_exTo;
static BOOL      g_dlgDone;

LRESULT CALLBACK ExportWndProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        CreateWindowExW(0, L"STATIC", L"\u4ECE:",
            WS_CHILD | WS_VISIBLE, 10, 14, 30, 22, hDlg, NULL, g_hInst, NULL);
        CreateWindowExW(0, DATETIMEPICK_CLASSW, NULL,
            WS_CHILD | WS_VISIBLE | DTS_SHORTDATECENTURYFORMAT,
            42, 10, 150, 22, hDlg, (HMENU)IDC_EX_FROM, g_hInst, NULL);
        CreateWindowExW(0, L"STATIC", L"\u5230:",
            WS_CHILD | WS_VISIBLE, 10, 44, 30, 22, hDlg, NULL, g_hInst, NULL);
        CreateWindowExW(0, DATETIMEPICK_CLASSW, NULL,
            WS_CHILD | WS_VISIBLE | DTS_SHORTDATECENTURYFORMAT,
            42, 40, 150, 22, hDlg, (HMENU)IDC_EX_TO, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"\u786E\u5B9A",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            40, 75, 60, 24, hDlg, (HMENU)IDOK, g_hInst, NULL);
        CreateWindowExW(0, L"BUTTON", L"\u53D6\u6D88",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            120, 75, 60, 24, hDlg, (HMENU)IDCANCEL, g_hInst, NULL);

        SendMessage(GetDlgItem(hDlg, IDC_EX_FROM), DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&g_exFrom);
        SendMessage(GetDlgItem(hDlg, IDC_EX_TO),   DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)&g_exTo);
        return 0;
    }
    case WM_CLOSE:
        g_dlgDone = TRUE;
        DestroyWindow(hDlg);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK)
        {
            SYSTEMTIME stFrom, stTo;
            DWORD r1 = (DWORD)SendMessage(GetDlgItem(hDlg, IDC_EX_FROM), DTM_GETSYSTEMTIME, 0, (LPARAM)&stFrom);
            DWORD r2 = (DWORD)SendMessage(GetDlgItem(hDlg, IDC_EX_TO),   DTM_GETSYSTEMTIME, 0, (LPARAM)&stTo);
            if (r1 != GDT_VALID || r2 != GDT_VALID) break;

            FILETIME ft1, ft2;
            SystemTimeToFileTime(&stFrom, &ft1);
            SystemTimeToFileTime(&stTo,   &ft2);
            if (CompareFileTime(&ft1, &ft2) > 0)
            {
                MessageBoxW(hDlg, L"\u5F00\u59CB\u65E5\u671F\u4E0D\u80FD\u665A\u4E8E\u7ED3\u675F\u65E5\u671F\u3002",
                    L"\u63D0\u793A", MB_OK | MB_ICONWARNING);
                break;
            }
            g_exFrom = stFrom; g_exTo = stTo;
            g_dlgDone = TRUE;
            DestroyWindow(hDlg);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL)
        {
            g_exFrom.wYear = 0;  // signal cancelled
            g_dlgDone = TRUE;
            DestroyWindow(hDlg);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hDlg, msg, wp, lp);
}

static BOOL ShowExportDialog(HWND hParent)
{
    static BOOL registered = FALSE;
    if (!registered)
    {
        WNDCLASSEXW wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ExportWndProc;
        wc.hInstance   = g_hInst;
        wc.hCursor     = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"ExportDlg";
        RegisterClassExW(&wc);
        registered = TRUE;
    }

    // Both default to today
    GetLocalTime(&g_exFrom);
    g_exTo = g_exFrom;

    RECT rp; GetWindowRect(hParent, &rp);
    int dx = 230, dy = 140;
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"ExportDlg",
        L"\u5BFC\u51FA\u65E5\u5FD7", WS_POPUP | WS_CAPTION | WS_SYSMENU,
        rp.left + ((rp.right - rp.left) - dx) / 2,
        rp.top  + ((rp.bottom - rp.top) - dy) / 2,
        dx, dy, hParent, NULL, g_hInst, NULL);

    EnableWindow(hParent, FALSE);
    ShowWindow(hDlg, SW_SHOW);

    g_dlgDone = FALSE;
    while (!g_dlgDone)
    {
        MSG m;
        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE))
        {
            if (m.message == WM_QUIT) { g_dlgDone = TRUE; break; }
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        WaitMessage();
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);

    return g_exFrom.wYear != 0;
}

// ================================================================
static int DaysBetween(const SYSTEMTIME* a, const SYSTEMTIME* b)
{
    FILETIME fa, fb;
    SystemTimeToFileTime(a, &fa);
    SystemTimeToFileTime(b, &fb);
    ULARGE_INTEGER ua, ub;
    ua.LowPart = fa.dwLowDateTime; ua.HighPart = fa.dwHighDateTime;
    ub.LowPart = fb.dwLowDateTime; ub.HighPart = fb.dwHighDateTime;
    return (int)((ub.QuadPart - ua.QuadPart) / 10000000LL / 86400LL);
}

void OnExport(HWND hWnd)
{
    if (!ShowExportDialog(hWnd)) return;

    WCHAR savePath[MAX_PATH] = L"export.rtf";
    OPENFILENAMEW ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hWnd;
    ofn.lpstrFilter = L"RTF Files (*.rtf)\0*.rtf\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile   = savePath;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrDefExt = L"rtf";
    if (!GetSaveFileNameW(&ofn)) return;

    // Hidden RichEdit for merging
    HWND hMerge = CreateWindowExW(0, g_szRichEditClass, NULL,
        WS_CHILD, 0, 0, 10, 10, hWnd, NULL, g_hInst, NULL);

    BOOL hasContent = FALSE;
    SYSTEMTIME cur = g_exFrom;
    int totalDays = DaysBetween(&g_exFrom, &g_exTo);

    for (int d = 0; d <= totalDays; d++)
    {
        WCHAR path[MAX_PATH];
        BuildFilePath(&cur, path, MAX_PATH);

        std::vector<BYTE> rtf = ReadFileToBuf(path);

        // Advance day before continue check so the loop always progresses
        SYSTEMTIME next = cur;
        FILETIME ft;
        SystemTimeToFileTime(&next, &ft);
        ULARGE_INTEGER ul;
        ul.LowPart = ft.dwLowDateTime; ul.HighPart = ft.dwHighDateTime;
        ul.QuadPart += 864000000000LL;
        ft.dwLowDateTime = ul.LowPart; ft.dwHighDateTime = ul.HighPart;
        FileTimeToSystemTime(&ft, &next);

        if (rtf.empty()) { cur = next; continue; }

        // Date header RTF with two \par for spacing
        char hdr[256];
        _snprintf(hdr, sizeof(hdr),
            "{\\rtf1\\ansi\\pard\\fs24\\b %04d/%02d/%02d\\b0\\fs18\\par \\par}",
            cur.wYear, cur.wMonth, cur.wDay);
        std::vector<BYTE> hdrRtf((BYTE*)hdr, (BYTE*)hdr + strlen(hdr));

        if (!hasContent)
        {
            // First entry: date header as base, content appended
            StreamCookie ckHdr; ckHdr.data = &hdrRtf; ckHdr.pos = 0;
            EDITSTREAM esHdr = { (DWORD_PTR)&ckHdr, 0, StreamInCB };
            SendMessage(hMerge, EM_STREAMIN, SF_RTF, (LPARAM)&esHdr);

            SendMessage(hMerge, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
            StreamCookie ck; ck.data = &rtf; ck.pos = 0;
            EDITSTREAM es = { (DWORD_PTR)&ck, 0, StreamInCB };
            SendMessage(hMerge, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
            hasContent = TRUE;
        }
        else
        {
            // Subsequent: go to end, insert header, insert content
            SendMessage(hMerge, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
            StreamCookie ckHdr; ckHdr.data = &hdrRtf; ckHdr.pos = 0;
            EDITSTREAM esHdr = { (DWORD_PTR)&ckHdr, 0, StreamInCB };
            SendMessage(hMerge, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&esHdr);

            SendMessage(hMerge, EM_SETSEL, (WPARAM)-1, (LPARAM)-1);
            StreamCookie ck; ck.data = &rtf; ck.pos = 0;
            EDITSTREAM es = { (DWORD_PTR)&ck, 0, StreamInCB };
            SendMessage(hMerge, EM_STREAMIN, SF_RTF | SFF_SELECTION, (LPARAM)&es);
        }

        cur = next;
    }

    if (!hasContent)
    {
        DestroyWindow(hMerge);
        MessageBoxW(hWnd, L"\u8BE5\u65E5\u671F\u8303\u56F4\u5185\u6CA1\u6709\u65E5\u5FD7\u8BB0\u5F55\u3002",
            L"\u63D0\u793A", MB_OK | MB_ICONINFORMATION);
        return;
    }

    StreamCookie ck;
    std::vector<BYTE> outBuf;
    ck.data = &outBuf; ck.pos = 0;
    EDITSTREAM es = { (DWORD_PTR)&ck, 0, StreamOutCB };
    SendMessage(hMerge, EM_STREAMOUT, SF_RTF, (LPARAM)&es);

    DestroyWindow(hMerge);

    if (!WriteBufToFile(savePath, outBuf))
        MsgBox(L"\u65E0\u6CD5\u5199\u5165\u5BFC\u51FA\u6587\u4EF6\u3002");
    else
        MessageBoxW(hWnd, L"\u5BFC\u51FA\u6210\u529F\u3002",
            L"\u63D0\u793A", MB_OK | MB_ICONINFORMATION);
}
