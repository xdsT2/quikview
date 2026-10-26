# Windows 安装包（NSIS）

用 [NSIS](https://nsis.sourceforge.io) 把 `build/` 出来的程序打成标准安装向导 EXE。

## 打包步骤

```bash
# 1. 编译 + 部署 payload（windeployqt 只带 Qt 自身依赖）
cmake --build build
mkdir staging && cp build/quikview.exe build/quikview.ico staging/
windeployqt --release --compiler-runtime --no-system-d3d-compiler --no-opengl-sw staging/quikview.exe

# 2. 补齐 mingw 运行库闭包（windeployqt 不会带 libheif/libraw/icu 等）
#    用 objdump 递归收集, 脚本见下; 缺 DLL 时程序 exit 127
for f in staging/*.exe staging/*.dll; do
  objdump -p "$f" | grep -i "DLL Name" | awk '{print $3}'
done   # 把列出的 mingw DLL 从 <mingw64>/bin 拷入 staging, 递归到无新增

# 3. 编译安装包（注意: 脚本必须带 UTF-8 BOM）
makensis /DSRC="staging" /DOUTFILE="QuikView-setup.exe" packaging/quikview.nsi
```

## 说明

- 安装向导为简体中文，默认安装到 `D:\Entertainment\QuikView`（可在目录页修改），带开始菜单组、桌面快捷方式（可勾选）与标准卸载器。
- 程序翻译在编译期嵌入资源 `:/i18n`，payload 无需 `.qm` 文件。
- `usp10.dll` 为系统组件，无需收集。
- 安装器未做代码签名，SmartScreen 会提示「更多信息 → 仍要运行」。
- 产物建议命名 `QuikView-<版本>-setup.exe`，挂到 GitHub Release。
