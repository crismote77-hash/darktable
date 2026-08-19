#!/usr/bin/env python3
"""Stub local backend for the darktable people faces pilot.

Entrada:
  --input  TSV con columnas: <image_id> <TAB> <image_path>

Salida:
  --output TSV con columnas: <image_id> <TAB> <person_or_none> <TAB> <confidence>

Esto deja una interfaz estable para sustituir más adelante el motor real
(YuNet/SFace + ONNX Runtime o servicio propio) sin tocar la capa Lua.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
from typing import Iterable, Tuple


def _guess_identity(image_path: str) -> str:
    """Deterministic demo identity inference from filename/stem.

    - If filename contains explicit person token, keep it.
    - Otherwise return '_none_' so the Lua layer applies fallback tag.
    """
    stem = Path(image_path).stem.lower()

    # Explicit token convention from filenames such as person_ana_001.jpg
    if "person_" in stem:
        token = stem.split("person_", 1)[1]
        token = token.split("_", 1)[0]
        token = "".join(c if c.isalnum() or c in "-_" else "_" for c in token).strip("-_")
        if token:
            return token

    # Lightweight deterministic signal for docs/testing, no real detection.
    signature = hashlib.sha1(image_path.encode("utf-8")).hexdigest()
    if int(signature[:2], 16) % 3 == 0:
        return "_none_"

    bucket = ["alice", "bob", "carol", "david", "emma"]
    idx = int(signature[:8], 16) % len(bucket)
    return bucket[idx]


def _iter_input_rows(path: Path) -> Iterable[Tuple[str, str]]:
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip("\n")
            if not line:
                continue
            parts = line.split("\t", 1)
            if len(parts) != 2:
                continue
            image_id, image_path = parts
            yield image_id.strip(), image_path.strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("analyze", help="run detection over the batch")
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--min-confidence", type=float, default=0.55)
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    rows = list(_iter_input_rows(input_path))
    if not rows:
        return 0

    with output_path.open("w", encoding="utf-8") as out:
        for image_id, image_path in rows:
            person = _guess_identity(image_path)
            if person == "_none_":
                out.write(f"{image_id}\t_none_\t{args.min_confidence:.3f}\n")
            else:
                # confidence is only illustrative in this scaffold backend
                confidence = 0.83
                out.write(f"{image_id}\t{person}\t{confidence:.3f}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
