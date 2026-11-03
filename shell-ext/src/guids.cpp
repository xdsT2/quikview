// GUID 实例化：initguid.h 使后续 DEFINE_GUID 变成定义而非声明。
// WIC 的 CLSID/IID/像素格式 GUID 也在此 TU 实例化，免去对导入库的依赖。
#include <initguid.h>
#include <unknwn.h>
#include <wincodec.h>

#include "guids.h"
