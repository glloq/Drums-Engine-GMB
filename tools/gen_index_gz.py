#!/usr/bin/env python3
"""Regenere engine/src/web/index_html_gz.h a partir de engine/data/index.html.

L'interface web est servie depuis la flash du programme (PROGMEM) et non depuis
LittleFS : apres toute modification de engine/data/index.html il faut relancer

    python3 tools/gen_index_gz.py

sinon le firmware continue de servir l'ancienne page.

Le mtime de l'entete gzip est force a 0 pour que deux generations successives du
meme fichier produisent un header identique (diffs propres).
"""

import gzip
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "engine" / "data" / "index.html"
DST = ROOT / "engine" / "src" / "web" / "index_html_gz.h"

BYTES_PER_LINE = 16


def main() -> int:
    if not SRC.is_file():
        print(f"source introuvable: {SRC}", file=sys.stderr)
        return 1

    raw = SRC.read_bytes()
    blob = gzip.compress(raw, compresslevel=9, mtime=0)

    lines = [
        "#ifndef INDEX_HTML_GZ_H",
        "#define INDEX_HTML_GZ_H",
        "",
        "#include <Arduino.h>",
        "",
        "// Auto-generated from engine/data/index.html",
        "// Regenerate with: python3 tools/gen_index_gz.py",
        f"// Size: {len(blob)} bytes (gzipped)",
        "",
        f"const size_t INDEX_HTML_GZ_LEN = {len(blob)};",
        "const uint8_t INDEX_HTML_GZ[] PROGMEM = {",
    ]
    for i in range(0, len(blob), BYTES_PER_LINE):
        chunk = blob[i:i + BYTES_PER_LINE]
        last = i + BYTES_PER_LINE >= len(blob)
        body = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append("  " + body + ("" if last else ","))
    lines += ["};", "", "#endif // INDEX_HTML_GZ_H", ""]

    DST.write_text("\n".join(lines), encoding="utf-8")
    ratio = 100.0 * len(blob) / len(raw)
    print(f"{SRC.name}: {len(raw)} o -> {len(blob)} o gzip ({ratio:.1f} %) -> {DST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
