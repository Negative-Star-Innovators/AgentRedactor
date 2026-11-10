#!/usr/bin/env python3
import argparse
import os
import subprocess


def classify(files: list[str]) -> set[str]:
    selected: set[str] = set()
    for path in files:
        if not path or path.endswith(".md") or path.startswith(("docs/", "website/")):
            continue
        if path in ("windows/version.txt",) or path.startswith((
            "windows/Strings/", "windows/models/", "core/", "scripts/",
            "third_party_tests/", "cloudflare/", ".github/",
        )):
            selected.update(("windows", "linux", "macos"))
        elif path.startswith("windows/"):
            selected.add("windows")
        elif path.startswith("linux/"):
            selected.add("linux")
        elif path.startswith("macos/"):
            selected.add("macos")
        elif path.startswith(("tests/macos/", "tests/gui/macos/")):
            selected.add("macos")
        elif path.startswith("tests/linux/"):
            selected.add("linux")
        elif path.startswith("tests/gui/windows/"):
            selected.add("windows")
        else:
            selected.update(("windows", "linux", "macos"))
    return selected


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base")
    parser.add_argument("--head", default="HEAD")
    args = parser.parse_args()
    if args.base and args.base != "0" * 40:
        files = subprocess.check_output(
            ["git", "diff", "--name-only", args.base, args.head], text=True,
        ).splitlines()
        selected = classify(files)
    else:
        selected = {"windows", "linux", "macos"}
    output = os.environ.get("GITHUB_OUTPUT")
    lines = [f"{platform}={str(platform in selected).lower()}"
             for platform in ("windows", "linux", "macos")]
    lines.append(f"code={str(bool(selected)).lower()}")
    print("Affected platforms:", ", ".join(sorted(selected)) or "none")
    if output:
        with open(output, "a", encoding="utf-8") as handle:
            handle.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
