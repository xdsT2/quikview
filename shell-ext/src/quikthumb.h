// QuikView 缩略图壳扩展公共声明。
// 纯 Win32：无 Qt、无 ATL/WTL。IInitializeWithStream / IThumbnailProvider
// 为系统接口，为规避 thumbscheme.h / propsys.h 在各工具链的头差异，此处自带声明。
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

// ---- IInitializeWithStream（Explorer 用它把文件流塞进 handler）----
class IInitializeWithStream : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE Load(IStream* pStm, DWORD grfMode) = 0;
};

// ---- IThumbnailProvider ----
class IThumbnailProvider : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE GetThumbnail(UINT cx, HBITMAP* phbm,
                                                   WTS_ALPHATYPE* pat) = 0;
};

// ---- 模块锁计数（module.cpp 提供；对象存活期间阻止 DLL 卸载）----
void DllAddLock();
void DllReleaseLock();

// ---- 缩略图提供程序（方法实现在 thumbnail.cpp）----
class QuikThumbProvider : public IInitializeWithStream,
                          public IThumbnailProvider {
public:
    QuikThumbProvider();    // 引用计数 1，占用一个模块锁
    virtual ~QuikThumbProvider();   // 虚：Release 归零时 delete this

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;
    // IInitializeWithStream
    STDMETHODIMP Load(IStream* pStm, DWORD grfMode) override;
    // IThumbnailProvider
    STDMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbm, WTS_ALPHATYPE* pat) override;

private:
    ULONG m_ref;
    IStream* m_stream;
};
