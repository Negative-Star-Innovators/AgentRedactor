#!/usr/bin/env python3
"""Build the macOS localization artifacts from the Windows .resw catalogs.

Reads every windows/Strings/<lang>/Resources.resw (MRT .resw XML, the UI
string source of truth) plus any macos/strings/<lang>/Resources.resw overlays
(macOS-only keys such as "Mac*") and emits two files for the macOS GUI:

  macos/gui/AgentRedactor/Localizable.xcstrings
      A single Xcode String Catalog. English is the catalog's source
      language; every key from windows/Strings/en appears with an explicit
      "en" localization (the .resw names are identifiers, not display text).
      Other languages carry only the keys present in their own .resw, so
      Apple's fallback to English applies to anything missing. Keys that
      exist only in non-English .resw files (stale legacy entries) are
      skipped — they have no English source to fall back to.

  macos/gui/AgentRedactor/strings-manifest.json
      Machine-readable {key: {language: value}} map for programmatic lookup
      by dynamic key (e.g. tray menu entries) where SwiftUI's
      LocalizedStringKey cannot be used.

Values are copied verbatim: no translation, no placeholder rewriting. The
.resw format strings use the core's LocFormat style ({0}, {1}, see
core/include/localization.h) and are kept as-is; the Swift side performs
substitution at runtime.

Run from anywhere:
    python3 macos/tools/resw_to_xcstrings.py            # (re)generate
    python3 macos/tools/resw_to_xcstrings.py --check    # verify outputs (CI)

--check regenerates in memory, requires the on-disk files to be byte-identical
to the deterministic output, then re-parses them and asserts: every English
key is present, every localization value matches its .resw source exactly,
and the language count matches the number of Strings/<lang> directories.
Exits non-zero on any failure.
"""

import argparse
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
STRINGS_DIR = REPO / "windows" / "Strings"
OVERLAY_DIR = REPO / "macos" / "strings"
OUT_DIR = REPO / "macos" / "gui" / "AgentRedactor"
XCSTRINGS_PATH = OUT_DIR / "Localizable.xcstrings"
MANIFEST_PATH = OUT_DIR / "strings-manifest.json"

SOURCE_LANGUAGE = "en"


def parse_resw(path):
    """Return ({name: value}, [anomalies]) from a .resw file.

    Values are returned verbatim — no stripping. XML entities (&amp; &lt;
    &quot; ...) are decoded by the XML parser. Duplicate names keep the
    first occurrence and are reported; entries without a <value> are skipped
    and reported.
    """
    data = {}
    anomalies = []
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as e:
        return None, [f"malformed XML: {e}"]
    for d in root.iter("data"):
        name = d.get("name")
        if not name:
            anomalies.append("<data> element without a name attribute")
            continue
        v = d.find("value")
        if v is None or v.text is None:
            anomalies.append(f"key {name!r} has no <value>; skipped")
            continue
        if name in data:
            anomalies.append(f"duplicate key {name!r}; keeping first occurrence")
            continue
        data[name] = v.text
    return data, anomalies


def load_catalogs():
    """Return ({tag: {key: value}}, [anomalies]) for every Strings/<lang> dir."""
    if not STRINGS_DIR.is_dir():
        sys.exit(f"error: {STRINGS_DIR} not found")
    tags = sorted(p.name for p in STRINGS_DIR.iterdir()
                  if p.is_dir() and (p / "Resources.resw").is_file())
    if SOURCE_LANGUAGE not in tags:
        sys.exit(f"error: no {SOURCE_LANGUAGE}/Resources.resw under {STRINGS_DIR}")
    catalogs = {}
    anomalies = []
    for tag in tags:
        data, an = parse_resw(STRINGS_DIR / tag / "Resources.resw")
        if data is None:
            sys.exit(f"error: {tag}/Resources.resw: {an[0]}")
        catalogs[tag] = data
        anomalies.extend(f"{tag}/Resources.resw: {a}" for a in an)

    # macOS-only overlay catalogs (e.g. macos/strings/en/Resources.resw with
    # "Mac*" keys that have no Windows counterpart). Merged on top of the
    # windows/Strings/ catalogs: overlay values win on conflict. A non-source
    # overlay for a language unknown to windows/Strings/ is ignored (its keys
    # would have no English source entry anyway).
    if OVERLAY_DIR.is_dir():
        for od in sorted(OVERLAY_DIR.iterdir()):
            resw = od / "Resources.resw"
            if not (od.is_dir() and resw.is_file()):
                continue
            if od.name not in catalogs:
                anomalies.append(f"{od.name}/Resources.resw: language not present "
                                 f"in windows/Strings/; overlay ignored")
                continue
            data, an = parse_resw(resw)
            if data is None:
                sys.exit(f"error: {od.name}/Resources.resw: {an[0]}")
            catalogs[od.name].update(data)
            anomalies.extend(f"{od.name}/Resources.resw (overlay): {a}" for a in an)
    return catalogs, anomalies


def build_outputs(catalogs):
    """Return (xcstrings dict, manifest dict, {tag: [skipped keys]})."""
    en = catalogs[SOURCE_LANGUAGE]
    en_keys = sorted(en)
    en_key_set = set(en_keys)
    languages = sorted(t for t in catalogs if t != SOURCE_LANGUAGE)

    catalog = {"sourceLanguage": SOURCE_LANGUAGE, "strings": {}, "version": "1.0"}
    manifest = {}
    for key in en_keys:
        localizations = {
            SOURCE_LANGUAGE: {
                "stringUnit": {"state": "translated", "value": en[key]}
            }
        }
        entry = {SOURCE_LANGUAGE: en[key]}
        for tag in languages:
            value = catalogs[tag].get(key)
            if value is not None:
                localizations[tag] = {
                    "stringUnit": {"state": "translated", "value": value}
                }
                entry[tag] = value
        catalog["strings"][key] = {"localizations": localizations}
        manifest[key] = entry

    skipped = {}
    for tag in languages:
        extra = sorted(set(catalogs[tag]) - en_key_set)
        if extra:
            skipped[tag] = extra
    return catalog, manifest, skipped


def dump(obj):
    """Deterministic JSON: sorted keys, 2-space indent, UTF-8, trailing newline."""
    return json.dumps(obj, ensure_ascii=False, indent=2, sort_keys=True) + "\n"


def check_outputs(catalog_text, manifest_text, catalogs):
    """Assert the emitted artifacts match the .resw sources. Return [failures]."""
    failures = []
    en = catalogs[SOURCE_LANGUAGE]
    en_keys = set(en)
    tags = set(catalogs)

    # Byte-level: the on-disk files must equal the deterministic regeneration.
    catalog, manifest, skipped = build_outputs(catalogs)
    if catalog_text != dump(catalog):
        failures.append("Localizable.xcstrings differs from a fresh regeneration "
                        "(stale output or non-deterministic serialization)")
    if manifest_text != dump(manifest):
        failures.append("strings-manifest.json differs from a fresh regeneration "
                        "(stale output or non-deterministic serialization)")

    # Structural: schema and language coverage.
    try:
        cat = json.loads(catalog_text)
    except json.JSONDecodeError as e:
        return failures + [f"Localizable.xcstrings is not valid JSON: {e}"]
    if cat.get("sourceLanguage") != SOURCE_LANGUAGE:
        failures.append(f'sourceLanguage is {cat.get("sourceLanguage")!r}')
    if cat.get("version") != "1.0":
        failures.append(f'version is {cat.get("version")!r}')
    strings = cat.get("strings", {})
    if set(strings) != en_keys:
        missing = sorted(en_keys - set(strings))[:5]
        unexpected = sorted(set(strings) - en_keys)[:5]
        failures.append(f"catalog key set != en key set "
                        f"(missing: {missing or 'none'}, unexpected: {unexpected or 'none'})")
    catalog_langs = set()
    for key, kobj in strings.items():
        catalog_langs.update(kobj.get("localizations", {}))
    if catalog_langs != tags:
        failures.append(f"catalog languages {sorted(catalog_langs)} != "
                        f"Strings dirs {sorted(tags)}")

    # Value-level: every catalog value must equal its .resw source verbatim.
    # Covers all languages and keys (a superset of the de/ar/zh-CN sample).
    for tag, resw in catalogs.items():
        for key, value in resw.items():
            if tag != SOURCE_LANGUAGE and key not in en_keys:
                continue  # stale non-English-only keys are intentionally skipped
            loc = strings.get(key, {}).get("localizations", {}).get(tag, {})
            unit = loc.get("stringUnit", {})
            if unit.get("state") != "translated":
                failures.append(f"{key}/{tag}: state is {unit.get('state')!r}")
            if unit.get("value") != value:
                failures.append(
                    f"{key}/{tag}: value mismatch (catalog "
                    f"{unit.get('value')!r} != resw {value!r})")

    # Manifest: same universe, same verbatim values, full language matrix.
    try:
        man = json.loads(manifest_text)
    except json.JSONDecodeError as e:
        return failures + [f"strings-manifest.json is not valid JSON: {e}"]
    if man != manifest:
        failures.append("manifest content differs from .resw sources")
    return failures


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="verify the on-disk artifacts instead of writing them")
    args = ap.parse_args()

    catalogs, anomalies = load_catalogs()
    catalog, manifest, skipped = build_outputs(catalogs)
    catalog_text = dump(catalog)
    manifest_text = dump(manifest)

    tags = sorted(catalogs)
    en_count = len(catalogs[SOURCE_LANGUAGE])
    print(f"{len(tags)} language catalogs (incl. {SOURCE_LANGUAGE}); "
          f"{en_count} source keys")

    if anomalies:
        print(f"\nWARNING: {len(anomalies)} source anomaly(ies):", file=sys.stderr)
        for a in anomalies:
            print(f"  {a}", file=sys.stderr)
    if skipped:
        n = len(next(iter(skipped.values())))
        same = all(v == next(iter(skipped.values())) for v in skipped.values())
        detail = f"the same {n} keys" if same else \
            ", ".join(f"{t}: {len(k)} keys" for t, k in sorted(skipped.items()))
        print(f"\nnote: skipped non-{SOURCE_LANGUAGE}-only keys present in "
              f"{len(skipped)} language(s) — {detail} (no English source)")

    if args.check:
        try:
            catalog_text_disk = XCSTRINGS_PATH.read_text(encoding="utf-8")
            manifest_text_disk = MANIFEST_PATH.read_text(encoding="utf-8")
        except OSError as e:
            sys.exit(f"CHECK FAILED: cannot read outputs: {e}")
        failures = check_outputs(catalog_text_disk, manifest_text_disk, catalogs)
        for a in anomalies:
            failures.append(f"source anomaly: {a}")
        if failures:
            for f in failures[:20]:
                print(f"CHECK FAILED: {f}", file=sys.stderr)
            if len(failures) > 20:
                print(f"CHECK FAILED: ... and {len(failures) - 20} more",
                      file=sys.stderr)
            sys.exit(1)
        locs = sum(len(k["localizations"]) for k in catalog["strings"].values())
        print(f"OK: {len(catalog['strings'])} keys, {locs} localized units, "
              f"{len(tags)} languages — all values match .resw sources verbatim")
        return 0

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    XCSTRINGS_PATH.write_text(catalog_text, encoding="utf-8")
    MANIFEST_PATH.write_text(manifest_text, encoding="utf-8")
    for key in catalog["strings"]:
        n = len(catalog["strings"][key]["localizations"])
        print(f"  {key}: {n} languages")
    print(f"\nwrote {XCSTRINGS_PATH.relative_to(REPO)}")
    print(f"wrote {MANIFEST_PATH.relative_to(REPO)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
