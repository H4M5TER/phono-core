"""Validate a vX.Y.Z release tag against the single package version."""
from __future__ import annotations

import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def version(tag: str | None = None) -> str:
    value = (ROOT / "VERSION.txt").read_text().strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", value):
        raise ValueError("VERSION.txt must contain X.Y.Z")
    if tag is not None and tag != f"v{value}":
        raise ValueError(f"Release tag {tag!r} differs from v{value}")
    return value


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag")
    print(version(parser.parse_args().tag))
