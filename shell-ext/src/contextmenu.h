// QuikView 上下文菜单壳扩展公共声明（交办单 D2 阶段 1：IContextMenu）。
// 纯 Win32 裸 COM：IShellExtInit / IContextMenu / CMINVOKECOMMANDINFO(EX)
// 直接用 shobjidl.h 的系统声明（quikthumb.h 的流接口已改名 QvInitializeWithStream，
// 不会再与 shobjidl→propsys.h 的同名类冲突）。无 Qt、无 ATL/WTL。
// 模块锁/实例句柄/支持扩展名等共享声明见 quikthumb.h。
#pragma once
#include <windows.h>
#include <objidl.h>
#include <shobjidl.h>

#include "guids.h"

// ---- 上下文菜单处理器（方法实现在 contextmenu.cpp）----
//
// 菜单两项：「用 QuikView 浏览」= quikview.exe <path>；
//          「用 QuikView 转换」= quikview.exe --batch <path>。
// 多选策略：只处理第一个选中文件（交办单允许二选一，见 README）。
// 过滤：第一个选中文件扩展名不在支持列表内时不注入菜单项。
class QuikContextMenu : public IShellExtInit,
                        public IContextMenu {
public:
    QuikContextMenu();    // 引用计数 1，占用一个模块锁
    virtual ~QuikContextMenu();   // 虚：Release 归零时 delete this

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

private:
    // 解析 quikview.exe 全路径：DLL 同目录 → 注册表 InstallDir/InstallPath。
    bool FindQuikViewExe(wchar_t* exePath, DWORD cch) const;
    // CreateProcessW 启动 quikview.exe（batch = true 加 --batch）。
    HRESULT Launch(bool batch) const;

    ULONG m_ref;
    wchar_t m_file[MAX_PATH];   // 只保留第一个选中文件（多选策略）
    bool m_hasFile;
};
