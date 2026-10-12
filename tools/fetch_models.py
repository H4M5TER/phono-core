"""Download the reviewed model snapshots into this repository's cache."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MODELS = {
    "w4a8": ("afirelily/phonop2c_v2_2_base_w4a8_model", "c11bc50c13a5a6ff627ab60a3f5571b8fdbacc73"),
    "w8a8": ("afirelily/phonop2c_v2_2_base_w8a8_model", "cf11a953a62fbba10f0eb585bc65802e01b70f7d"),
}


def validate(directory: Path) -> None:
    config_path = directory / "config.json"
    if not config_path.is_file():
        raise ValueError(f"Model package is missing {config_path}")
    config = json.loads(config_path.read_text(encoding="utf-8"))
    paths = [config["runtime"]["pre_model_path"], config["runtime"]["post_model_path"],
             *(config["vocabs"][key] for key in ("chinese_vocab", "context_vocab", "pinyin_vocab"))]
    if "segmenter" in config:
        paths += [config["segmenter"]["model_path"], config["segmenter"]["char_vocab"]]
    for relative in paths:
        path = directory / relative
        if not path.is_file() or not path.stat().st_size:
            raise ValueError(f"Model package is missing {path}")


def fetch(precision: str, cache: Path) -> Path:
    repository, revision = MODELS[precision]
    directory = cache.resolve() / precision / revision
    with urllib.request.urlopen(f"https://huggingface.co/api/models/{repository}/revision/{revision}", timeout=120) as response:
        metadata = json.load(response)
    for item in metadata["siblings"]:
        relative = item["rfilename"]
        if not (relative == "config.json" or relative.startswith(("bins/", "vocabs/"))):
            continue
        path = directory / relative
        if path.is_file() and path.stat().st_size:
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".download")
        with urllib.request.urlopen(f"https://huggingface.co/{repository}/resolve/{revision}/{relative}", timeout=120) as response:
            with temporary.open("wb") as output:
                while chunk := response.read(1024 * 1024):
                    output.write(chunk)
        temporary.replace(path)
    validate(directory)
    return directory


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--precision", choices=MODELS, required=True)
    parser.add_argument("--cache", type=Path, default=ROOT / "cache/models")
    parser.add_argument("--path-file", type=Path)
    args = parser.parse_args()
    directory = fetch(args.precision, args.cache)
    if args.path_file:
        args.path_file.parent.mkdir(parents=True, exist_ok=True)
        args.path_file.write_text(str(directory), encoding="utf-8")
    print(directory)
