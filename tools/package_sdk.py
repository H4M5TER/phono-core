"""Package the tested SDK for release."""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil

from release_version import version

ROOT = Path(__file__).resolve().parents[1]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--platform", choices=("linux-x86_64", "windows-x64"), required=True)
    parser.add_argument("--output", type=Path, default=ROOT / "dist")
    parser.add_argument("--tag")
    args = parser.parse_args()
    value = version(args.tag)
    prefix = args.prefix.resolve(strict=True)
    if (prefix / "share/phono-core/VERSION").read_text().strip() != value:
        raise ValueError("Installed SDK version differs from VERSION.txt")
    required = ["include/phono-core/phono_api.h", "lib/cmake/phono-core/phono-coreConfig.cmake",
                "share/licenses/phono-core/LICENSE", "share/phono-core/build-info.json"]
    required += ["bin/phono_core.dll", "lib/phono_core.lib"] if args.platform == "windows-x64" else ["lib/libphono_core.so"]
    for filename in required:
        if not (prefix / filename).is_file():
            raise ValueError(f"SDK is missing {filename}")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    archive_format = "zip" if args.platform == "windows-x64" else "gztar"
    archive = shutil.make_archive(str(output / f"phono-core-sdk-{value}-{args.platform}"), archive_format, prefix)
    print(archive)
