#!/usr/bin/env python3
"""Bootstrap translations for new languages using Google Translate.

Run from the repository root with the project venv active:
    .transvenv/Scripts/python AgentRedactor/generate_new_languages.py <tag> <gt-code> ...

Example (adds Arabic and Malay blocks to new_language_blocks.py):
    .transvenv/Scripts/python AgentRedactor/generate_new_languages.py ar-SA ar ms-MY ms

Output is appended to new_language_blocks.py in the repo root. Review the
blocks before copying them into AgentRedactor/generate_localizations.py.

Requires: python -m venv .transvenv && .transvenv/Scripts/pip install deep-translator
"""

import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Dict, List

try:
    from deep_translator import GoogleTranslator
except ImportError as e:  # pragma: no cover
    raise ImportError(
        "deep-translator is required. Run:\n"
        "  python -m venv .transvenv\n"
        "  .transvenv/Scripts/pip install deep-translator"
    ) from e

COPY_AS_IS = {
    "Agent Redactor", "CPU", "CUDA", "DirectML", "ONNX Runtime Provider",
    "API", "URL", "HTTP", "HTTPS", "PII", "Regex", "PII: {0}", "Regex: {0}",
    "ONNX", "OpenAI", "Anthropic", "OpenClaw", "Claude Code", "LLM",
    "Windows", "OK", "debug.log",
}


def parse_resw(path: Path) -> Dict[str, str]:
    tree = ET.parse(path)
    root = tree.getroot()
    data: Dict[str, str] = {}
    for elem in root.findall("data"):
        name = elem.get("name")
        value_elem = elem.find("value")
        if name is not None and value_elem is not None and value_elem.text is not None:
            data[name] = value_elem.text
    return data


def quote_string(s: str) -> str:
    r = repr(s)
    if r.startswith("'") and r.endswith("'"):
        r = r[1:-1]
        r = r.replace('"', '\\"').replace("\\'", "'")
        return f'"{r}"'
    return r


def translate_batch(texts: List[str], target: str, chunk_size: int = 25) -> List[str]:
    translator = GoogleTranslator(source='en', target=target)
    results: List[str] = []
    for i in range(0, len(texts), chunk_size):
        chunk = texts[i:i + chunk_size]
        print(f"  chunk {i // chunk_size + 1}/{(len(texts) + chunk_size - 1) // chunk_size} ({len(chunk)} strings)", flush=True)
        retries = 3
        while retries > 0:
            try:
                translated = translator.translate_batch(chunk)
                if len(translated) != len(chunk):
                    raise RuntimeError(f"Batch length mismatch: {len(translated)} vs {len(chunk)}")
                results.extend(translated)
                break
            except Exception as e:
                print(f"  retry {4 - retries}/{3}: {e}", flush=True)
                retries -= 1
                time.sleep(5)
                if retries == 0:
                    print("  FAILED chunk, using English fallback", flush=True)
                    results.extend(chunk)
        time.sleep(2)
    return results


def translate_language(tag: str, gt: str, unique_english: List[str], output_path: Path) -> None:
    print(f"[{tag}] Translating {len(unique_english)} unique strings...", flush=True)
    translated = translate_batch(unique_english, gt)
    translation_map = dict(zip(unique_english, translated))

    with open(output_path, 'a', encoding='utf-8') as out:
        out.write(f'# {tag}\n')
        out.write(f'LANG_TRANSLATIONS["{tag}"] = {{\n')
        for english in unique_english:
            out.write(f'    {quote_string(english)}: {quote_string(translation_map[english])},\n')
        out.write('}\n\n')

        downgrade_en = "A newer version of [ProductName] is already installed."
        downgrade_translated = translate_batch([downgrade_en], gt)[0]
        out.write(f'WXL_TRANSLATIONS["{tag}"] = {{\n')
        out.write(f'    {quote_string(downgrade_en)}: {quote_string(downgrade_translated)},\n')
        out.write('}\n\n')
        out.flush()

    print(f"[{tag}] Done. Appended to {output_path}", flush=True)


def main() -> None:
    args = sys.argv[1:]
    if len(args) < 2 or len(args) % 2 != 0:
        print("Usage: generate_new_languages.py <tag1> <gt-code1> [<tag2> <gt-code2> ...]")
        print("Example: generate_new_languages.py ar-SA ar ms-MY ms")
        sys.exit(1)

    repo_root = Path(__file__).resolve().parent.parent
    en_resw_path = repo_root / "AgentRedactor" / "Strings" / "en-US" / "Resources.resw"
    en_data = parse_resw(en_resw_path)
    unique_english = sorted(set(en_data.values()) - COPY_AS_IS)
    print(f"Found {len(en_data)} resw entries, {len(unique_english)} unique translatable strings", flush=True)

    output_path = repo_root / "new_language_blocks.py"
    # Create the file with a header if it does not exist.
    if not output_path.exists():
        output_path.write_text(
            "# Auto-generated translation blocks for AgentRedactor/generate_localizations.py\n"
            "# Review before inserting.\n\n",
            encoding='utf-8'
        )

    for tag, gt in zip(args[0::2], args[1::2]):
        translate_language(tag, gt, unique_english, output_path)


if __name__ == "__main__":
    main()
