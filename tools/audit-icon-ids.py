"""Audit icon ids: unmapped references and dead mapping entries.

Two questions, both answered against the real sources:
  * every id the code hands to an icon API must exist in src/AppIcon.cpp;
  * every id in the table must be referenced somewhere, otherwise its SVG files
    would be committed for nothing (dispatch forbids that).

Reference detection is line-based: only lines that mention an icon API or the
menu catalog are scanned, so config values ("dark", "fit", "lanczos") do not
turn into false hits. AppIcon.cpp itself is excluded from the "referenced"
side because the table literals live there.
"""
import pathlib
import re

REPO = pathlib.Path(__file__).resolve().parent.parent
SRC = REPO / "src"
APPICON = SRC / "AppIcon.cpp"

ROW = re.compile(r'\{"([^"]+)",\s*"([^"]+)",\s*(nullptr|"[^"]+")')
LITERAL = re.compile(r'"([a-z][a-z0-9\-]*)"')
MARKERS = ("MenuIcons", "AppIcon", "setActionIcon", "kIconProp", "drawIcon",
           "addHamburgerAction", "addCheckableItem", "addSubmenu", "decorate(",
           "appmenu", "iconId", "MenuCatalog")


def main():
    known = {r[0] for r in ROW.findall(APPICON.read_text(encoding="utf-8"))}
    files = {p.name: p.read_text(encoding="utf-8")
             for p in list(SRC.glob("*.cpp")) + list(SRC.glob("*.h")) if p != APPICON}

    referenced, live = set(), set()
    for name, text in files.items():
        for line in text.split("\n"):
            # 跳过 --smoke 命令分派行：那里的字面量是命令名（appmenureset 之类），
            # 不是图标 id。
            if "--smoke" in line or "c ==" in line or "c.startsWith" in line:
                continue
            if any(m in line for m in MARKERS):
                for tok in LITERAL.findall(line):
                    referenced.add(tok)
        for ident in known:
            if '"%s"' % ident in text or "'%s'" % ident in text:
                live.add(ident)

    unknown = sorted(i for i in referenced if i not in known)
    dead = sorted(known - live)

    print("table ids: %d" % len(known))
    print("ids referenced from icon call sites: %d" % len(referenced & known))
    if unknown:
        print("UNMAPPED ids referenced by code (%d):" % len(unknown))
        for i in unknown:
            where = sorted(n for n, t in files.items() if '"%s"' % i in t)
            print("  %-20s %s" % (i, ", ".join(where)))
    else:
        print("no unmapped icon references")
    if dead:
        print("table entries never referenced (%d): %s" % (len(dead), ", ".join(dead)))
    else:
        print("every table entry is referenced")


if __name__ == "__main__":
    main()
