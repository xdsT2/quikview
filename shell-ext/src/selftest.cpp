// thumb_selftest：无注册表加载自测。
// LoadLibrary(DLL) → DllGetClassObject 直接拿类厂 → CreateInstance →
// SHCreateStreamOnFileW 喂图片流 → GetThumbnail → 校验 HBITMAP 尺寸。
// 全程不写注册表、不经 COM 激活，验证「编译链接 + 加载 + 解码」闭环。
#include <shlwapi.h>
#include <stdio.h>

#include "quikthumb.h"

typedef HRESULT(STDAPICALLTYPE* PFN_DllGetClassObject)(REFCLSID, REFIID,
                                                       void**);
typedef HRESULT(STDAPICALLTYPE* PFN_DllCanUnloadNow)(void);

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);   // 崩溃也能看到已打印的部分
    if (argc < 3) {
        printf("usage: thumb_selftest.exe <quikthumb.dll> <image> [image...]\n");
        return 2;
    }

    HMODULE hDll = LoadLibraryExA(argv[1], nullptr,
                                  LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hDll) {
        printf("FAIL: LoadLibrary(%s) gle=%lu\n", argv[1], GetLastError());
        return 1;
    }
    printf("OK: LoadLibrary %s\n", argv[1]);

    const PFN_DllGetClassObject pGetClassObject =
        (PFN_DllGetClassObject)GetProcAddress(hDll, "DllGetClassObject");
    const PFN_DllCanUnloadNow pCanUnloadNow =
        (PFN_DllCanUnloadNow)GetProcAddress(hDll, "DllCanUnloadNow");
    if (!pGetClassObject || !pCanUnloadNow) {
        printf("FAIL: missing exports (DllGetClassObject=%p DllCanUnloadNow=%p)\n",
               (void*)pGetClassObject, (void*)pCanUnloadNow);
        return 1;
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        printf("FAIL: CoInitializeEx\n");
        return 1;
    }

    IClassFactory* cf = nullptr;
    HRESULT hr = pGetClassObject(CLSID_QuikThumbProvider, IID_IClassFactory,
                                 (void**)&cf);
    if (FAILED(hr) || !cf) {
        printf("FAIL: DllGetClassObject hr=0x%08lX\n", (unsigned long)hr);
        return 1;
    }
    printf("OK: DllGetClassObject -> IClassFactory\n");

    // 未知 CLSID 必须拒绝（借 IID_IUnknown 当不存在的 CLSID 用）
    IClassFactory* bogus = nullptr;
    hr = pGetClassObject(IID_IUnknown, IID_IClassFactory, (void**)&bogus);
    if (hr != CLASS_E_CLASSNOTAVAILABLE || bogus != nullptr) {
        printf("FAIL: unknown CLSID not rejected (hr=0x%08lX)\n",
               (unsigned long)hr);
        return 1;
    }
    printf("OK: unknown CLSID rejected\n");

    int fails = 0, passes = 0;
    for (int i = 2; i < argc; i++) {
        wchar_t wpath[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, wpath, MAX_PATH);

        IInitializeWithStream* init = nullptr;
        hr = cf->CreateInstance(nullptr, IID_IInitializeWithStream,
                                (void**)&init);
        if (FAILED(hr) || !init) {
            printf("FAIL: %s CreateInstance hr=0x%08lX\n", argv[i],
                   (unsigned long)hr);
            fails++;
            continue;
        }

        IStream* st = nullptr;
        hr = SHCreateStreamOnFileW(wpath, STGM_READ | STGM_SHARE_DENY_WRITE,
                                   &st);
        if (FAILED(hr) || !st) {
            printf("FAIL: %s open stream hr=0x%08lX\n", argv[i],
                   (unsigned long)hr);
            init->Release();
            fails++;
            continue;
        }
        hr = init->Load(st, STGM_READ);
        st->Release();   // handler 自持引用
        if (FAILED(hr)) {
            printf("FAIL: %s Load hr=0x%08lX\n", argv[i], (unsigned long)hr);
            init->Release();
            fails++;
            continue;
        }

        IThumbnailProvider* tp = nullptr;
        hr = init->QueryInterface(IID_IThumbnailProvider, (void**)&tp);
        if (FAILED(hr) || !tp) {
            printf("FAIL: %s QI(IThumbnailProvider) hr=0x%08lX\n", argv[i],
                   (unsigned long)hr);
            init->Release();
            fails++;
            continue;
        }

        HBITMAP hbm = nullptr;
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        hr = tp->GetThumbnail(256, &hbm, &alpha);
        if (SUCCEEDED(hr) && hbm) {
            BITMAP bm = {};
            if (GetObjectW(hbm, sizeof(bm), &bm) && bm.bmWidth > 0 &&
                bm.bmHeight > 0 && bm.bmBitsPixel == 32) {
                printf("PASS: %s -> %ldx%ld alpha=%d\n", argv[i], bm.bmWidth,
                       bm.bmHeight, (int)alpha);
                passes++;
            } else {
                printf("FAIL: %s bad BITMAP (w=%ld h=%ld bpp=%d)\n", argv[i],
                       bm.bmWidth, bm.bmHeight, bm.bmBitsPixel);
                fails++;
            }
            DeleteObject(hbm);
        } else {
            printf("FAIL: %s GetThumbnail hr=0x%08lX\n", argv[i],
                   (unsigned long)hr);
            fails++;
        }
        tp->Release();
        init->Release();
    }

    cf->Release();
    CoUninitialize();

    if (pCanUnloadNow() != S_OK) {
        printf("FAIL: DllCanUnloadNow != S_OK (引用计数泄漏)\n");
        fails++;
    } else {
        printf("OK: DllCanUnloadNow == S_OK\n");
    }
    FreeLibrary(hDll);

    printf("RESULT: %s (pass=%d fail=%d)\n", fails ? "FAIL" : "PASS", passes,
           fails);
    return fails ? 1 : 0;
}
