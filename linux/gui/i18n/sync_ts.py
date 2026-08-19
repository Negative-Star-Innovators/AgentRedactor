#!/usr/bin/env python3
"""Sync Qt .ts catalogs for the Linux GUI from the Windows .resw catalogs.

Scans linux/gui/*.cpp for tr("...") sources (one Qt context per class),
matches each source against the English windows/Strings/en/Resources.resw
values, and writes linux/gui/i18n/agentredactor_<locale>.ts for every
Windows language folder, filling translations from that language's resw.
Sources with no Windows counterpart are left type="unfinished" — fill them
with bootstrap_translations.py (machine translation) and native review.

Matching normalizes accelerator markers ('&') and placeholders (%1 <-> {0}).
Existing translations in the .ts files are preserved across runs, so this
script is idempotent and safe to re-run after source strings change.

Run from the repository root:
    python3 linux/gui/i18n/sync_ts.py
"""

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
GUI_SRC = REPO / "linux" / "gui"
I18N_DIR = GUI_SRC / "i18n"
STRINGS_DIR = REPO / "windows" / "Strings"

# tr() source files and the Qt context (class) they belong to. Files listed
# here map wholly to one context except password_dialog.cpp, whose two
# classes are split by tracking the enclosing ClassName:: qualifier.
FILE_CONTEXT = {
    "main_window.cpp": "MainWindow",
    "tray_icon.cpp": "TrayIcon",
    "password_dialog.cpp": None,  # split by qualifier
}


def unescape_cpp(s: str) -> str:
    return (s.replace('\\"', '"').replace("\\n", "\n").replace("\\t", "\t")
             .replace("\\\\", "\\"))


def extract_tr_calls(path: Path):
    """Yield (offset, source_text) for every tr("...") call, concatenating
    adjacent string literals."""
    text = path.read_text(encoding="utf-8")
    results = []
    for m in re.finditer(r"\btr\(\s*", text):
        i = m.end()
        depth = 1
        parts = []
        while i < len(text) and depth > 0:
            ch = text[i]
            if ch == "(":
                depth += 1
                i += 1
            elif ch == ")":
                depth -= 1
                i += 1
            elif ch == '"':
                j = i + 1
                buf = []
                while j < len(text) and text[j] != '"':
                    if text[j] == "\\":
                        buf.append(text[j:j + 2])
                        j += 2
                    else:
                        buf.append(text[j])
                        j += 1
                parts.append("".join(buf))
                i = j + 1
            else:
                i += 1
        if parts:
            results.append((m.start(), unescape_cpp("".join(parts))))
    return results


def collect_inventory():
    """Return {context: [source, ...]} preserving first-seen order."""
    inventory = {}
    for fname, context in FILE_CONTEXT.items():
        path = GUI_SRC / fname
        text = path.read_text(encoding="utf-8")
        for offset, source in extract_tr_calls(path):
            ctx = context
            if ctx is None:
                # Last ClassName:: qualifier before this call names the class.
                qualifiers = re.findall(r"(\w+)::\w+\s*\(", text[:offset])
                ctx = qualifiers[-1] if qualifiers else "PasswordEnableDialog"
            inventory.setdefault(ctx, [])
            if source not in inventory[ctx]:
                inventory[ctx].append(source)
    return inventory


PLACEHOLDER_RE = re.compile(r"%\d+|\{\d+\}")


def normalize(s: str) -> str:
    s = s.replace("&", "")
    s = PLACEHOLDER_RE.sub("#", s)
    return s.strip()


def convert_placeholders(s: str) -> str:
    """Windows {0}/{1} -> Qt %1/%2 (only plain {digit} tokens)."""
    return re.sub(r"\{(\d+)\}", lambda m: f"%{int(m.group(1)) + 1}", s)


def parse_resw(path: Path):
    tree = ET.parse(path)
    out = {}
    for d in tree.getroot().iter("data"):
        v = d.find("value")
        if v is not None and v.text is not None:
            out[d.get("name")] = v.text.strip()
    return out


def qt_locale(tag: str) -> str:
    """BCP-47 tag -> Qt locale/file suffix (zh-CN -> zh_CN)."""
    return tag.replace("-", "_")


def load_existing_translations(ts_path: Path):
    """{(context, source): (translation, finished)} from an existing .ts."""
    if not ts_path.exists():
        return {}
    tree = ET.parse(ts_path)
    out = {}
    for ctx in tree.getroot().iter("context"):
        name = ctx.findtext("name")
        for msg in ctx.iter("message"):
            src = msg.findtext("source") or ""
            tr = msg.find("translation")
            text = tr.text or ""
            finished = tr.get("type") != "unfinished"
            if text:
                out[(name, src)] = (text, finished)
    return out


# Sources whose Windows translations embed OS-specific wording and therefore
# must NOT be reused (they get machine-translated fresh instead).
# TrayMenu_StartOnBoot is "Mit Windows starten" / "Démarrer avec Windows" /
# "Iniciar com o Windows" in de/fr/pt — wrong on Linux.
EXCLUDE_REUSE = {"Start on Boot"}


def main():
    inventory = collect_inventory()
    total = sum(len(v) for v in inventory.values())
    print(f"scanned {total} unique source strings in {len(inventory)} contexts")

    en = parse_resw(STRINGS_DIR / "en" / "Resources.resw")
    # Normalized English value -> resw key (first key wins).
    en_by_norm = {}
    for key, value in en.items():
        en_by_norm.setdefault(normalize(value), key)

    tags = sorted(p.name for p in STRINGS_DIR.iterdir()
                  if (p / "Resources.resw").exists() and p.name != "en")
    print(f"{len(tags)} language catalogs: {', '.join(tags)}")

    I18N_DIR.mkdir(exist_ok=True)
    summary = {}
    for tag in tags:
        resw = parse_resw(STRINGS_DIR / tag / "Resources.resw")
        ts_path = I18N_DIR / f"agentredactor_{qt_locale(tag)}.ts"
        existing = load_existing_translations(ts_path)

        ts = ET.Element("TS", version="2.1", language=qt_locale(tag))
        used = reused = kept = gaps = 0
        for context, sources in inventory.items():
            ctx_el = ET.SubElement(ts, "context")
            ET.SubElement(ctx_el, "name").text = context
            for source in sources:
                msg = ET.SubElement(ctx_el, "message")
                ET.SubElement(msg, "source").text = source
                key = None if source in EXCLUDE_REUSE else en_by_norm.get(normalize(source))
                translation = None
                finished = True
                if key is not None and key in resw:
                    translation = convert_placeholders(resw[key])
                    reused += 1
                elif ((context, source) in existing
                      and source not in EXCLUDE_REUSE):
                    # Previously bootstrapped/reviewed translation survives.
                    translation, finished = existing[(context, source)]
                    kept += 1
                else:
                    gaps += 1
                tr_el = ET.SubElement(msg, "translation")
                if translation:
                    tr_el.text = translation
                    if not finished:
                        tr_el.set("type", "unfinished")
                else:
                    tr_el.set("type", "unfinished")

        ET.indent(ts)
        ET.ElementTree(ts).write(ts_path, encoding="utf-8", xml_declaration=True)
        summary[tag] = (reused, kept, gaps)
        used += reused

    print(f"\nper-language: resw-reused / preserved / gaps")
    for tag, (r, k, g) in summary.items():
        print(f"  {tag:8} {r:3} / {k:3} / {g:3}")
    print(f"\nwrote {len(summary)} .ts files to {I18N_DIR.relative_to(REPO)}")
    print("next: python3 linux/gui/i18n/bootstrap_translations.py  (fills the gaps)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
