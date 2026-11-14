// QuikView 上下文菜单壳扩展公共声明（P-19 / 批次 B）。
// 纯 Win32 裸 COM：IShellExtInit / IContextMenu2（IContextMenu 子接口，
// 多一个 HandleMenuMsg 用于自绘菜单项）。无 Qt、无 ATL/WTL。
//
// 菜单三项（可由设置页的 ctxmenu/* 偏好逐项关闭，见 prefs.h）：
//   浏览（带 QuikView 图标 MIIM_BITMAP）
//   转换（带图标）
//   缩略图预览（MFT_OWNERDRAW，≤256px；WIC 解码超 50ms 预算则不出此项）
//   单选图时浏览项右侧用 \t 右对齐「宽×高 · 文件大小」。
// 命令 ID 按可见项连续分配（优先序 浏览→转换→预览），缺省全开时与
// 0.5.0 的 ID 布局一致（浏览 0 / 转换 1 / 预览 2）；跳过某项时不留 ID
// 空洞，避免与同菜单的后续壳扩展冲突。
#pragma once
#include <windows.h>
#include <objidl.h>
#include <shobjidl.h>

#include "guids.h"
#include "prefs.h"

class QuikContextMenu : public IShellExtInit,
                        public IContextMenu2 {
public:
    QuikContextMenu();
    virtual ~QuikContextMenu();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // IShellExtInit
    STDMETHODIMP Initialize(LPCITEMIDLIST pidlFolder, IDataObject* pdtobj,
                            HKEY hkeyProgID) override;
    // IContextMenu
    STDMETHODIMP QueryContextMenu(HMENU hmenu, UINT indexMenu,
                                  UINT idCmdFirst, UINT idCmdLast,
                                  UINT uFlags) override;
    STDMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO pici) override;
    STDMETHODIMP GetCommandString(UINT_PTR idCmd, UINT uType,
                                  UINT* pwReserved, CHAR* pszName,
                                  UINT cchMax) override;
    // IContextMenu2（自绘项的 measure/draw 回调）
    STDMETHODIMP HandleMenuMsg(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

private:
    bool FindQuikViewExe(wchar_t* exePath, DWORD cch) const;
    HRESULT Launch(bool batch) const;
    void ReleaseBitmaps();
    // 可见项命令表：偏移 → CMD_*（QueryContextMenu 时按可见项重建）；
    // 越界返回 -1
    int CmdAtOffset(UINT offset) const;

    ULONG m_ref;
    wchar_t m_file[MAX_PATH];
    bool m_hasFile;

    // 批次 B 产物（QueryContextMenu 时构造，析构释放）
    HBITMAP m_iconBmp;      // 菜单项小图标（32×32）
    HBITMAP m_thumbBmp;     // 自绘缩略图（≤256px 边），失败为 nullptr
    int m_thumbW;
    int m_thumbH;
    wchar_t m_infoText[128];  // 右对齐「宽×高 · 大小」

    // 偏好驱动的可见项（QueryContextMenu 时重建）
    int m_idMap[3];     // CMD_* → 命令 ID 偏移，-1 = 本项未插入
    int m_visibleCount; // 实际插入的命令项数（不含分隔线）
};
