// QuikView 缩略图壳扩展公共声明。
// 纯 Win32：无 Qt、无 ATL/WTL。
// IInitializeWithStream / IThumbnailProvider 为系统接口，此处自带声明以规避
// 各工具链 SDK 头差异（MinGW 的 propsys.h 连带自 shobjidl.h，且把方法误写作
// Initialize）。类名用 Qv 前缀避免与系统头同名类撞车；vtable 布局与 IID 与
// 系统接口完全一致。
#pragma once
#include <windows.h>
#include <objidl.h>

#include "guids.h"

// ---- WTS_ALPHATYPE（thumbscheme.h 同名定义）----
enum WTS_ALPHATYPE {
    WTSAT_UNKNOWN = 0,
    WTSAT_RGB = 1,
    WTSAT_ARGB = 2,   // 32bpp，预乘 alpha
};

// ---- QvInitializeWithStream（= 系统 IInitializeWithStream，方法名用官方
//      IDL 的 Load；Explorer 通过 IID_IInitializeWithStream 查询到它）----
class QvInitializeWithStream : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Load(IStream* pStm, DWORD grfMode) = 0;
};

// ---- IThumbnailProvider（thumbscheme.h 同名定义，未被系统头连带，不冲突）----
class IThumbnailProvider : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetThumbnail(UINT cx, HBITMAP* phbm,
                                                   WTS_ALPHATYPE* pat) = 0;
};

// ---- 模块锁计数与实例句柄（module.cpp 提供；对象存活期间阻止 DLL 卸载）----
void DllAddLock();
void DllReleaseLock();
HMODULE DllGetModule();

// ---- 支持的图片扩展名（thumbnail 的 .ext\ShellEx 注册与上下文菜单过滤共用，
//      定义在 module.cpp；改动须同步 scripts/*.ps1 的 $Exts）----
size_t QuikSupportedExtCount();
const wchar_t* QuikSupportedExt(size_t i);

// ---- 缩略图提供程序（方法实现在 thumbnail.cpp）----
class QuikThumbProvider : public QvInitializeWithStream,
                          public IThumbnailProvider {
public:
    QuikThumbProvider();    // 引用计数 1，占用一个模块锁
    virtual ~QuikThumbProvider();   // 虚：Release 归零时 delete this

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // QvInitializeWithStream
    STDMETHODIMP Load(IStream* pStm, DWORD grfMode) override;
    // IThumbnailProvider
    STDMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbm, WTS_ALPHATYPE* pat) override;

private:
    ULONG m_ref;
    IStream* m_stream;
};
