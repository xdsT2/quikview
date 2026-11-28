# AGENTS.md — SoleView 轻量化开发规则

本文件是本仓唯一的工作规则，替代旧仓那套重型派单体系。内容少、可执行，**冲突时以本文件为准**。

## 一、仓库形态

1. **唯一工作区**：`E:\A1\XiaoMiFeng\`，**唯一代码仓**：`E:\A1\XiaoMiFeng\soleview\`。源码、官网、测试只在本仓内操作；不再有多个并列副本、镜像仓或冷冻仓。
2. **一个分支一件事**：从 `main` 切分支，做完合入即删分支；不留长期挂着的孤分支。
3. **工具链沿用旧址**：`E:\Agent\Tencent\WorkBuddy\Workspace\tools\`（msys64 / qt-official 等），本仓不改工具链。
4. **固定构建目录**：`build\`、`build-shell64\`、`build-shell32\`、`build-libheif\`，不另建 `build-*`。
5. **远端**：`https://github.com/xdsT2/soleview.git`，只保留 `main`（`gh-pages` 仅在 Pages 仍依赖时保留）。

## 二、构建与测试

1. **固定复用 `build/`**：不再按任务新建 `build-*`。例外只有壳扩展的双位数构建 `build-shell64/`、`build-shell32/`，与 `build-libheif/`。
2. 构建前必须让 `tools\msys64\mingw64\bin` 进 `PATH`，否则 `cc1plus.exe` 会报 `0xC0000135`（DLL 找不到）。
   ```powershell
   $env:PATH = 'E:\Agent\Tencent\WorkBuddy\Workspace\tools\msys64\mingw64\bin;' + $env:PATH
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=E:/Agent/Tencent/WorkBuddy/Workspace/tools/msys64/mingw64
   cmake --build build
   cd build; ctest --output-on-failure
   ```
3. **合入门槛**：`cmake --build` 通过 + `ctest` 全绿（当前 29 条）。跑不过不许合。
4. **dev / 测试安装纪律**：本地测试安装只允许写 HKCU 注册项，或安装到固定测试目录；用完必须先运行安装器自带的卸载器走完反注册，再清理文件。**禁止「删目录代替卸载」**——目录删掉后，注册表关联、候选 ProgID 与卸载入口会变成指向死路径的幽灵项（2026-11-28 事故：`.png` 的「打开方式」仍指向已删除的 `install-gui\quikview.exe`）。

## 三、并发

1. **全局同时在做的事 ≤ 3**，**同一代码区（同一批文件）同时只有 1 个写入者**。
2. 动 `main`、动安装包、动注册表这类高风险动作前，先写租约：
   `E:\A1\XiaoMiFeng\.lease\claim-<任务>.json`（目录不存在则先创建；10 分钟 TTL，做完即删）。
3. 别在别人正在构建的 `build/` 上并行编译。

## 四、产物与卫生

1. **测试耗材用完即清**：生成的测试图、录屏、临时样本，任务结束就删；要保留就必须在任务回复里写明路径与体积。
2. **不写绝对路径绑定的临时脚本**：脚本里禁止硬编码 `E:\Agent\...`；路径从参数或环境变量来，脚本放 `Temp/scripts/`。
3. **不留 `build*` 垃圾**：固定目录之外不新建构建目录。
4. **不再写重型派单与回执**：小改直接改、说明改了什么/怎么验/哪个提交即可；跨模块或需别人接手才写一份短方案（≤1 页），不建目录树、不建证据目录。

## 五、提交

1. 提交信息用 `type(scope): 摘要`（`feat` / `fix` / `docs` / `chore` / `refactor`）。
2. 合入用 `--no-ff` 保留合并点，合完删分支（本地 + 远端）。
3. 推送前 `git fetch` + `rev-list --left-right --count origin/main...main` 确认为 0/0 或只有自己领先。

## 六、改名前遗留的兼容面（不要顺手拆）

1. 旧 `QuikView.<ext>` ProgID 与两个旧 COM CLSID 仍注册，用于承接已发布版本的关联与缩略图缓存。
2. 单实例仍监听旧管道名；配置仍从 `%LOCALAPPDATA%\QuikView\QuikView\config.ini` 一次性继承。
3. 已发布 QuikView v0.5.0–v0.6.1 的原包与 `SHA256SUMS` 留在发布产物目录，**只读，不重打、不改名**。

## 七、用户偏好

- 称呼用户“宝宝”，助手自称“简栏”。
- 删除一律先确认；不删用户自己的文件。

## 八、XiaoMiFeng 外围目录约定（2026-11-28 起）

1. **测试大图**一律放 `..\测试素材\`（即 `E:\A1\XiaoMiFeng\测试素材\`），不得放代码仓或 `资料与设计\` / `临时验收\`。
2. **近期验收截图 / 录屏 / 报告**放 `..\临时验收\`（即 `E:\A1\XiaoMiFeng\临时验收\`），发完大版本统一清一次（周转箱口径）。
3. **官网唯一真源**是本仓 `website\`，旧外置副本禁止直接编辑。
4. 禁止往 `资料与设计\` 或 `临时验收\` 放编译中间物或 GB 级样本；构建产物只进本仓固定 `build\`。
5. 全局并发 ≤ 3，同一代码区（同一批文件）同时只有 1 个写入者；合并即删分支。
