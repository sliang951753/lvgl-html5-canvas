#!/usr/bin/env python3
"""Generate web/protocol.js from src/proto/protocol.h.

Parses #define LHC_* lines and emits matching JS const exports.
Run by the CMake `gen_protocol_js` custom target whenever protocol.h
changes, and by CI to verify the committed protocol.js is up-to-date.
"""
from __future__ import annotations
import argparse
import re
import sys
from pathlib import Path

DEFINE_RE = re.compile(
    r"^\s*#define\s+(LHC_[A-Z0-9_]+)\s+(0x[0-9A-Fa-f]+|\d+|\(.+\))\s*(?:/\*\s*(.*?)\s*\*/)?\s*$"
)


def parse_defines(header_text: str) -> list[tuple[str, str, str]]:
    out: list[tuple[str, str, str]] = []
    for line in header_text.splitlines():
        m = DEFINE_RE.match(line)
        if not m:
            continue
        name, value, comment = m.group(1), m.group(2), (m.group(3) or "")
        out.append((name, value, comment))
    return out


def normalize_value(value: str) -> str:
    """Convert C numeric literal to JS. Strip outer parens, keep shifts as-is."""
    v = value.strip()
    if v.startswith("(") and v.endswith(")"):
        v = v[1:-1].strip()
    # `1u << 0` style → `1 << 0`
    v = re.sub(r"(\d+)u\b", r"\1", v)
    return v


def emit_js(defines: list[tuple[str, str, str]]) -> str:
    lines: list[str] = [
        "// AUTO-GENERATED from src/proto/protocol.h. Do not edit by hand.",
        "// Regenerate with: python src/proto/gen_js_protocol.py "
        "--header src/proto/protocol.h --out web/protocol.js",
        "",
        "export const Proto = Object.freeze({",
    ]
    for name, value, comment in defines:
        js_key = name.removeprefix("LHC_")
        js_val = normalize_value(value)
        if comment:
            lines.append(f"  /** {comment} */")
        lines.append(f"  {js_key}: {js_val},")
    lines.append("});")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--header", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument(
        "--check",
        action="store_true",
        help="Exit non-zero if generated output differs from --out (CI mode).",
    )
    args = ap.parse_args()

    header_text = args.header.read_text(encoding="utf-8")
    defines = parse_defines(header_text)
    if not defines:
        print(f"error: no LHC_* defines found in {args.header}", file=sys.stderr)
        return 2

    new_text = emit_js(defines)

    if args.check:
        existing = args.out.read_text(encoding="utf-8") if args.out.exists() else ""
        if existing != new_text:
            print(
                f"error: {args.out} is stale relative to {args.header}",
                file=sys.stderr,
            )
            return 1
        print(f"ok: {args.out} matches {args.header}")
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(new_text, encoding="utf-8")
    print(f"wrote {args.out} ({len(defines)} defines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
