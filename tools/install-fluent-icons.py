"""Install the Fluent SVGs that AppIcon actually maps, and regenerate ui.qrc.

Read-only against the curated pack; writes only into the repo's
resources/icons/fluent and resources/ui.qrc. Idempotent: re-running produces the
same file set, and it prunes fluent files that no longer appear in the mapping.

Size policy (dispatch section 5):
  16px - context menus, compact lists, small status icons
  20px - top toolbar, bottom island, settings nav, normal buttons
  24px - large standalone entries (only the empty-state "image" glyph today)
"""
import pathlib
import re
import shutil

REPO = pathlib.Path(r"E:\Agent\Tencent\WorkBuddy\Workspace\quikview")
PACK = pathlib.Path(r"E:\A1\XiaoMiFeng\管理\QuikView-图标系统-20261118\icons")
PACK_LICENSE = PACK.parent / "LICENSE-Microsoft-Fluent.txt"
APPICON = REPO / "src" / "AppIcon.cpp"
DEST = REPO / "resources" / "icons" / "fluent"
QRC = REPO / "resources" / "ui.qrc"

ROW = re.compile(r'\{"([^"]+)",\s*"([^"]+)",\s*(nullptr|"[^"]+")')


def parse_table():
    text = APPICON.read_text(encoding="utf-8", errors="strict")
    rows = ROW.findall(text)
    if len(rows) < 50:
        raise SystemExit("mapping table looks truncated: %d rows" % len(rows))
    regular, filled = set(), set()
    for _id, family, fill in rows:
        regular.add(family)
        if fill != "nullptr":
            filled.add(fill.strip('"'))
    return regular, filled


def main():
    regular, filled = parse_table()
    plan = []
    for fam in sorted(regular):
        plan.append((fam, "regular", 16))
        plan.append((fam, "regular", 20))
    for fam in sorted(regular & {"image"}):
        plan.append((fam, "regular", 24))
    for fam in sorted(filled):
        plan.append((fam, "filled", 16))
        plan.append((fam, "filled", 20))

    if DEST.exists():
        shutil.rmtree(DEST)
    DEST.mkdir(parents=True, exist_ok=True)
    # 许可证随图标目录一起分发（NOTICE 第二节引用这个文件名），不通过 qrc 打包。
    shutil.copy2(PACK_LICENSE, DEST / "LICENSE-Microsoft-Fluent.txt")
    copied = []
    missing = []
    for fam, style, size in plan:
        src = PACK / str(size) / style / ("ic_fluent_%s_%d_%s.svg" % (fam, size, style))
        if not src.exists():
            missing.append(str(src))
            continue
        dst = DEST / str(size) / style / src.name
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        copied.append(dst)

    rel = sorted(p.relative_to(REPO / "resources").as_posix() for p in copied)
    generated = "\n".join("        <file>%s</file>" % r for r in rel)

    qrc = """<RCC>
    <!-- 注意：prefix="/" + file 路径自带 ui/ → 资源路径 :/ui/xxx.png。
         不要写成 prefix="/ui" + file "ui/xxx.png"，那会变成 :/ui/ui/xxx.png。 -->
    <qresource prefix="/">
        <file>ui/check.png</file>
        <file>ui/check-light.png</file>
        <file>ui/chevron-down.png</file>
        <file>ui/chevron-right.png</file>
        <!-- 应用图标：路径 :/quikview.ico。main.cpp 图标链的最后一层兜底，
             保证安装目录里没有外部 .ico 时窗口/任务栏仍有图标。 -->
        <file>quikview.ico</file>
        <!-- Fluent 功能图标（图标派单 2026-11-18，唯一来源：src/AppIcon.cpp 映射表）。
             这一段由 tools/install-fluent-icons.py 生成，只登记程序实际使用的
             语义；不要手工增删，也不要在这里放整套 596 个精选 SVG。
             路径规律 :/icons/fluent/<16|20|24>/<regular|filled>/ic_fluent_<family>_<size>_<style>.svg -->
%s
    </qresource>
</RCC>
""" % generated
    QRC.write_text(qrc, encoding="utf-8", newline="\n")

    print("regular families: %d, filled families: %d" % (len(regular), len(filled)))
    print("copied svg files: %d" % len(copied))
    print("size split: 16=%d 20=%d 24=%d filled=%d" % (
        sum(1 for p in copied if p.parent.parent.name == "16" and p.parent.name == "regular"),
        sum(1 for p in copied if p.parent.parent.name == "20" and p.parent.name == "regular"),
        sum(1 for p in copied if p.parent.parent.name == "24"),
        sum(1 for p in copied if p.parent.name == "filled"),
    ))
    if missing:
        print("MISSING from pack:")
        for m in missing:
            print("  " + m)


if __name__ == "__main__":
    main()
