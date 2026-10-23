# 界面素材来源与可复现性说明

本文件说明 `resources/skin/` 下每个文件的来源。结论一句话：

> **全部由 `tools/03_gen_skin.py` 用程序化几何绘制生成，不含任何第三方图像素材。**

---

## 1. 生成链

```
tools/03_gen_skin.py
   ├── 纯 PIL 图元绘制（圆角矩形 / 圆 / 线段 / 折线 / 多边形）
   ├── 4x 超采样 → LANCZOS 降采样（边缘抗锯齿）
   ├── 确定性输出（无随机数、无外部输入、无时间戳）
   └── skin/*.png  +  skin/PROVENANCE.json（尺寸 / 字节数 / SHA-256）
```

生成器**不打开任何外部图像**。校验方式：

```powershell
Select-String -Path tools\03_gen_skin.py -Pattern 'Image\.open|skin\.data|resources\\skin'
# 预期：无输出（04_compare.py 是内部比对工具，会读原版；它不参与素材生成）
```

## 2. 复现

```powershell
$py = 'C:\Users\XDS\AppData\Local\Programs\Python\Python312\python.exe'
& $py tools\03_gen_skin.py
& $py -c "import json,hashlib,os; m=json.load(open(r'skin/PROVENANCE.json',encoding='utf-8'))['files']; [print(n, 'OK' if hashlib.sha256(open(os.path.join('skin',n),'rb').read()).hexdigest()==v['sha256'] else 'MISMATCH') for n,v in m.items()]"
```

预期全部 `OK`。换一台机器、换一个 Python 小版本，输出应逐字节相同
（PIL 的绘图与 LANCZOS 实现在 12.x 上稳定；若未来 PIL 改变算法，以本文件记录的
哈希为准，重新生成后对比即可发现差异）。

## 3. 设计参数（自定，不是照抄）

| 项 | 取值 |
|---|---|
| 强调色 | `#3D7EBF`（选中页签底边、标识渐变起点） |
| 标题栏底色 | `#ABABAB`（代码里已有该常量，素材与之对齐） |
| 工具栏渐变 | `#FBFBFB → #E9E9E9`，顶 1px `#FFFFFF`，底 1px `#CFCFCF` |
| 控制栏胶囊 | `#343434 → #1C1C1C`，描边 `#4A4A4A`，圆角 39 |
| 胶囊按钮 | 外圈 `#333333` / 内圈 `#454545`，描边 `#555555` |
| 图标前景 | `#E4E4E4` |
| 滑轨 | 槽 `#262626`，描边 `#0E0E0E`，刻度 `#525252` |
| 关闭按钮 | 常态 `#C75050` / 悬停 `#DE6666` / 按下 `#A33E3E` + 白色 X |
| 设置页签 | 未选中 `#F7F7F7`，选中 `#DFEAF8` + 底边 2px 强调色 |
| 边框 | 半透明黑内阴影，alpha 88→58→32→16→6→0（6px 衰减） |
| 标识 | 圆角方块 + 白色环形 + 右下短尾（抽象 "Q"），非任何现有商标 |

> 能主张的边界：**图形元素（颜色、线宽、几何、图标画法）由本项目自定并自绘**。
> 尺寸、帧序、状态语义（常态/悬停/按下）、控制栏热区布局来自对 Honeyview 5.53
> 公开可观察行为的功能性观察，属于功能参数，不是「独立于原版的设计」。
> 不要对外声称「完全独立设计」或「与 Honeyview 视觉无关」。

## 4. 尺寸与帧数规格

尺寸与帧数是**接口约束**（必须与代码的切帧逻辑一致），不是美术选择：

| 文件 | 尺寸 | 帧 | 代码消费方式 |
|---|---|---|---|
| `border_top/bottom.png` | 60×6 | 1 | 整块拉伸到窗口宽度 |
| `border_left/right.png` | 6×60 | 1 | 整块拉伸到窗口高度 |
| `btn_full/minimize/maximize(_2).png` | 96×21 | 4×24 | `frames = w/h = 4`，取帧 0/1 |
| `btn_close.png` | 200×21 | 4×50 | 代码手工 `/4`，取帧 0/1/2 |
| `btn_logo.png` | 140×31 | 4×35 | 只取帧 0，缩放到 18×16 |
| `static_toolbar1.png` | 30×31 | 1 | 横向平铺（接缝必须无缝） |
| `static_showmode_0..4.png` | 28×31 | 1 | 缩放到 16×18（3/4 当前不可达：视图模式只有 0/1/2） |
| `setting_tab_0/1.png` | 120×30 | 4×30 | 代码手工 `/4`，整块拉伸到 130×30 |
| `static_controlpanel.png` | 460×80 | 1 | 控制栏回退底图 |
| `pill_zoom.png` | 1680×291 | 1 | 控制栏整图，绘制到 460×80 逻辑矩形 |

`setting_tab_*.png` 会被横向拉伸 4.3 倍，因此图内只有「纯填充 + 底部 2px」这类可拉伸图元，
**没有圆角、也没有右侧竖边**（1px 竖边会被拉成约 4px 宽的带）。

## 4b. 应用图标

`resources/quikview.ico`（包内 `assets/quikview.ico`）由 `tools/07_gen_icon.py` 生成：

- 10 个尺寸：16、20、24、32、40、48、64、96、128、256，每档单独 8× 超采样后 LANCZOS 缩小；
- ICO 容器为手工封装（每个尺寸一条 PNG 记录，Windows Vista+ 支持）；
- 图形与 `btn_logo.png` 同一母题（圆角方块 + 白环 + 右下短尾）；
- 替代原先的 `resources/Honeyview.ico`——那个文件与原版 `icons/Honeyview.ico` **SHA-256 相同**。

## 4c. 不在本包内的东西

| 文件 | 位置 | 禁止事项 |
|---|---|---|
| 原版主窗口参考截图 | AgentHome，不在仓库 | 不要复制回仓库、不要贴进 issue/README |
| `internal/compare_before_after.png` | 包内 `internal/` | **禁止入库、禁止分发、禁止上传**（含原版像素） |
| `tools/probe.csv` / `usage.csv` 等探测数据 | 包内 `tools/` | 只含尺寸/哈希/引用位置，不含像素；可随包 |

## 5. 已知限制

- **滑轨滑块未烘焙**：滑块位置必须随缩放值变化，属于代码绘制职责。
  贴图里画死一个滑块会误导用户，所以留空。
- **显示模式图标是简笔几何**：5 个模式（原始 / 适合窗口 / 适合宽度 / 双页 L2R / 双页 R2L）
  用可区分的抽象图形表达，未追求与原版视觉相似。若要更好看的版本，
  建议直接换用开源图标集（MIT / CC0 / Apache-2.0），并在此表登记来源。
- **控制栏按钮无悬停/按下帧**：`ControlBar::paintEvent()` 目前只贴整图。
  接入状态帧时，把 `pill_zoom.png` 改成 4 帧横排即可（需同步改 `paintEvent`）。
