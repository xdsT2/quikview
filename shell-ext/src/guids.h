// QuikView 壳扩展共享 GUID 定义。
// 本头文件只声明（extern）；真正实例化在 guids.cpp（先 include <initguid.h>）。

// {D8A1C4E2-7B35-4F69-92A4-5C6D7E8F1A2B} QuikView 缩略图提供程序 COM 类
DEFINE_GUID(CLSID_QuikThumbProvider,
    0xd8a1c4e2, 0x7b35, 0x4f69, 0x92, 0xa4, 0x5c, 0x6d, 0x7e, 0x8f, 0x1a, 0x2b);

// {C9FFAFAD-7C1C-4CCC-A37C-A075ED1F27AF} QuikView 上下文菜单 COM 类（阶段 1）
DEFINE_GUID(CLSID_QuikContextMenu,
    0xc9ffafad, 0x7c1c, 0x4ccc, 0xa3, 0x7c, 0xa0, 0x75, 0xed, 0x1f, 0x27, 0xaf);

// {B824B49D-22AC-4161-AC8A-9916E8FA3F7F} 系统 IInitializeWithStream
// （与 propsys.h 同 IID；此处自带声明，避免依赖各工具链 SDK 头的差异）
DEFINE_GUID(IID_IInitializeWithStream,
    0xb824b49d, 0x22ac, 0x4161, 0xac, 0x8a, 0x99, 0x16, 0xe8, 0xfa, 0x3f, 0x7f);

// {E357FCCD-A995-4576-B01F-234630154E96} 系统 IThumbnailProvider
// （同时就是资源管理器缩略图 ShellEx 类别 GUID）
DEFINE_GUID(IID_IThumbnailProvider,
    0xe357fccd, 0xa995, 0x4576, 0xb0, 0x1f, 0x23, 0x46, 0x30, 0x15, 0x4e, 0x96);
