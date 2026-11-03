// ctx_fake_quikview：thumb_selftest 用的 quikview.exe 替身（EXCLUDE_FROM_ALL，
// 非交付物）。上下文菜单 Handler 用 CreateProcessW 真启动它，它把自身命令行
// 原样追加进环境变量 QUIKVIEW_FAKE_MARK 指向的文件（UTF-16LE），供 selftest
// 断言 InvokeCommand 展开的命令行（是否带 --batch、文件参数是否为第一个选中
// 文件），随后立即退出。GUI 子系统，运行不弹任何窗口。
#include <windows.h>

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    wchar_t mark[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"QUIKVIEW_FAKE_MARK", mark, MAX_PATH)) {
        return 0;
    }
    HANDLE h = CreateFileW(mark, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 1;
    DWORD written = 0;
    const wchar_t* cmd = GetCommandLineW();
    WriteFile(h, cmd, (DWORD)(lstrlenW(cmd) * sizeof(wchar_t)), &written,
              nullptr);
    WriteFile(h, L"\n", (DWORD)sizeof(wchar_t), &written, nullptr);
    CloseHandle(h);
    return 0;
}
