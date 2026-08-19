#!/usr/bin/env python3
"""Bootstrap the unfinished Linux GUI translations with Google Translate.

Fills every type="unfinished" entry in linux/gui/i18n/agentredactor_*.ts —
these are strings the Windows resw catalogs never had (the typed master
password flow, the Linux tray/quit wording, a few UI labels). Mirrors the
Windows bootstrap convention (windows/generate_new_languages.py): machine
translation now, native review later.

Requires the repo venv (see sync_ts.py header):
    python3 -m venv --without-pip .transvenv && .transvenv/bin/python get-pip.py
    .transvenv/bin/pip install deep-translator

Run from the repository root:
    .transvenv/bin/python linux/gui/i18n/bootstrap_translations.py [tag ...]

With no arguments all languages are processed; pass resw tags (e.g. "de fr")
to limit the run. Placeholders (%1, %2) are verified to survive translation;
entries where they do not are left unfinished for manual review.
"""

import re
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
I18N_DIR = REPO / "linux" / "gui" / "i18n"
STRINGS_DIR = REPO / "windows" / "Strings"

try:
    from deep_translator import GoogleTranslator
except ImportError as e:  # pragma: no cover
    raise ImportError(
        "deep-translator is required. Run:\n"
        "  .transvenv/bin/pip install deep-translator"
    ) from e

# resw tag -> Google Translate code (only the ones that differ).
GOOGLE_CODE = {
    "zh-CN": "zh-CN",
    "zh-TW": "zh-TW",
    "fil": "tl",
    "nb": "no",
    "he": "iw",
    "az-Latn": "az",
    "ha-Latn": "ha",
    "ig-NG": "ig",
    "sr-Latn": "sr",  # Google returns Cyrillic; transliterated below
}

# Serbian Cyrillic -> Latin (1:1 digraph-aware mapping).
SR_CYR_TO_LAT = {
    "а": "a", "б": "b", "в": "v", "г": "g", "д": "d", "ђ": "đ", "е": "e",
    "ж": "ž", "з": "z", "и": "i", "ј": "j", "к": "k", "л": "l", "љ": "lj",
    "м": "m", "н": "n", "њ": "nj", "о": "o", "п": "p", "р": "r", "с": "s",
    "т": "t", "ћ": "ć", "у": "u", "ф": "f", "х": "h", "ц": "c", "ч": "č",
    "џ": "dž", "ш": "š",
    "А": "A", "Б": "B", "В": "V", "Г": "G", "Д": "D", "Ђ": "Đ", "Е": "E",
    "Ж": "Ž", "З": "Z", "И": "I", "Ј": "J", "К": "K", "Л": "L", "Љ": "Lj",
    "М": "M", "Н": "N", "Њ": "Nj", "О": "O", "П": "P", "Р": "R", "С": "S",
    "Т": "T", "Ћ": "Ć", "У": "U", "Ф": "F", "Х": "H", "Ц": "C", "Ч": "Č",
    "Џ": "Dž", "Ш": "Š",
}


def sr_latinize(text: str) -> str:
    return "".join(SR_CYR_TO_LAT.get(ch, ch) for ch in text)


PLACEHOLDER_RE = re.compile(r"%\d+")


def placeholders_ok(source: str, translated: str) -> bool:
    return sorted(PLACEHOLDER_RE.findall(source)) == sorted(PLACEHOLDER_RE.findall(translated))


def main() -> int:
    only = set(sys.argv[1:])
    tags = sorted(p.name for p in STRINGS_DIR.iterdir()
                  if (p / "Resources.resw").exists() and p.name != "en")
    if only:
        unknown = only - set(tags)
        if unknown:
            print(f"unknown tags: {', '.join(sorted(unknown))}")
            return 2
        tags = [t for t in tags if t in only]

    total_filled = total_failed = 0
    for tag in tags:
        ts_path = I18N_DIR / f"agentredactor_{tag.replace('-', '_')}.ts"
        if not ts_path.exists():
            print(f"{tag}: no .ts file (run sync_ts.py first) — skipped")
            continue
        tree = ET.parse(ts_path)
        root = tree.getroot()

        # Collect unfinished entries in order.
        pending = []  # (translation_element, source_text)
        for msg in root.iter("message"):
            tr = msg.find("translation")
            if tr is not None and tr.get("type") == "unfinished":
                pending.append((tr, msg.findtext("source") or ""))
        if not pending:
            print(f"{tag}: nothing to do")
            continue

        target = GOOGLE_CODE.get(tag, tag)
        translator = GoogleTranslator(source="en", target=target)
        print(f"{tag}: translating {len(pending)} strings -> {target}", flush=True)

        filled = failed = 0
        chunk = 20
        for i in range(0, len(pending), chunk):
            batch = pending[i:i + chunk]
            sources = [s for _, s in batch]
            retries = 3
            results = None
            while retries > 0:
                try:
                    results = translator.translate_batch(sources)
                    if len(results) != len(sources):
                        raise RuntimeError(f"batch length mismatch {len(results)} vs {len(sources)}")
                    break
                except Exception as e:  # network hiccup / rate limit
                    print(f"  retry {4 - retries}/3: {e}", flush=True)
                    retries -= 1
                    time.sleep(5)
            if results is None:
                results = sources  # leave as English below (still flagged)

            for (tr, source), translated in zip(batch, results):
                if tag == "sr-Latn":
                    translated = sr_latinize(translated)
                if not translated or translated == source and retries == 0 and results is sources:
                    continue  # untouched unfinished (translation failed)
                if not placeholders_ok(source, translated):
                    print(f"  PLACEHOLDER LOST: {source!r} -> {translated!r} (left unfinished)")
                    failed += 1
                    continue
                tr.text = translated
                # Machine-translated: mark finished so lrelease ships it;
                # native review happens on the .ts files (same convention as
                # the Windows bootstrap).
                del tr.attrib["type"]
                filled += 1
            time.sleep(0.5)  # be polite to the endpoint

        ET.indent(tree)
        tree.write(ts_path, encoding="utf-8", xml_declaration=True)
        print(f"  {tag}: {filled} filled, {failed} placeholder failures")
        total_filled += filled
        total_failed += failed

    print(f"\ntotal: {total_filled} translated, {total_failed} placeholder failures")
    return 0 if total_failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
