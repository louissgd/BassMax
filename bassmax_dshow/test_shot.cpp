// 渲染设置对话框并截图, 用于检查排版/中文
#include <windows.h>
#include <commctrl.h>
#include <stdio.h>

static INT_PTR CALLBACK DlgProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_INITDIALOG) {
        SendDlgItemMessageW(h, 1001, TBM_SETRANGE, TRUE, MAKELPARAM(0, 240));
        SendDlgItemMessageW(h, 1002, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
        SendDlgItemMessageW(h, 1001, TBM_SETPOS, TRUE, 165);   // +4.5 dB
        SendDlgItemMessageW(h, 1002, TBM_SETPOS, TRUE, 0);
        SetDlgItemTextW(h, 1003, L"+4.5 dB");
        SetDlgItemTextW(h, 1004, L"0 %");
        return TRUE;
    }
    if (m == WM_CLOSE) { EndDialog(h, 0); return TRUE; }
    return FALSE;
}

static bool SaveBmp(HWND hwnd, const wchar_t* path) {
    RECT rc; GetWindowRect(hwnd, &rc);
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC hScreen = GetDC(nullptr);
    HDC hMem = CreateCompatibleDC(hScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hScreen, w, h);
    HGDIOBJ old = SelectObject(hMem, hBmp);
    BOOL ok = PrintWindow(hwnd, hMem, 0);
    SelectObject(hMem, old);

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;      // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    int stride = w * 4;
    BYTE* bits = new BYTE[stride * h];
    GetDIBits(hScreen, hBmp, 0, h, bits, &bi, DIB_RGB_COLORS);

    BITMAPFILEHEADER fh = {};
    BITMAPINFOHEADER ih = {};
    ih.biSize = sizeof(ih); ih.biWidth = w; ih.biHeight = -h;
    ih.biPlanes = 1; ih.biBitCount = 32; ih.biCompression = BI_RGB;
    ih.biSizeImage = stride * h;
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih);
    fh.bfSize = fh.bfOffBits + ih.biSizeImage;

    FILE* f = _wfopen(path, L"wb");
    if (!f) return false;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    fwrite(bits, stride * h, 1, f);
    fclose(f);

    delete[] bits;
    DeleteObject(hBmp);
    DeleteDC(hMem);
    ReleaseDC(nullptr, hScreen);
    return ok != FALSE;
}

int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: test_shot <ax> <out.bmp>\n"); return 2; }
    InitCommonControls();
    HMODULE hDll = LoadLibraryA(argv[1]);
    if (!hDll) { printf("LoadLibrary FAILED\n"); return 1; }

    // 用 .ax 里的资源创建"设置"弹窗 (IDD_BASSMAX_POPUP = 102)
    HWND hDlg = CreateDialogW(hDll, MAKEINTRESOURCEW(102), nullptr, DlgProc);
    if (!hDlg) { printf("CreateDialog FAILED %lu\n", GetLastError()); return 1; }
    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    MSG msg;
    for (int i = 0; i < 30; ++i) {           // 让控件完成首次绘制
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(20);
    }

    RECT rc; GetWindowRect(hDlg, &rc);
    printf("窗口尺寸: %ld x %ld\n", rc.right - rc.left, rc.bottom - rc.top);
    if (SaveBmp(hDlg, L"shot.bmp")) printf("已保存 shot.bmp\n");
    else printf("截图失败\n");

    DestroyWindow(hDlg);
    FreeLibrary(hDll);
    return 0;
}
