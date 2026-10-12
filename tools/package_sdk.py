"""Package the tested SDK and demos, then write their release checksums."""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

from release_version import version

ROOT = Path(__file__).resolve().parents[1]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
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
    archives = []
    with tempfile.TemporaryDirectory(prefix="phono-package-", dir=output) as temporary:
        stage = Path(temporary)
        sdk = stage / "sdk"
        shutil.copytree(prefix, sdk)
        archives.append(Path(shutil.make_archive(str(output / f"phono-core-sdk-{value}-{args.platform}"), archive_format, sdk)))
        demos = stage / "demos"
        subprocess.run(["cmake", "--install", str(args.build_dir), "--config", "Release", "--prefix", str(demos), "--component", "demos"], check=True)
        for directory in ("share", "lib", "bin"):
            if (sdk / directory).is_dir():
                shutil.copytree(sdk / directory, demos / directory, dirs_exist_ok=True)
        if args.platform == "linux-x86_64":
            launcher = demos / "run-demo.sh"
            launcher.write_text('#!/bin/sh\nset -eu\nroot=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexport LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"\nprogram=$1\nmodel=$2\nshift 2\nexec "$root/bin/$program" "$model" "$root/share/phono-core/core_configs/engine_config.json" "$root/share/phono-core/core_configs/context_manager.json" "$@"\n')
            launcher.chmod(0o755)
        (demos / "README.txt").write_text(
            "模型单独下载，向演示程序显式传入模型目录及附带的两份配置。\n"
            "Download models separately and pass the model directory and both included config files explicitly.\n"
            "Windows: bin\\cli_demo_capi.exe MODEL_DIR share\\phono-core\\core_configs\\engine_config.json share\\phono-core\\core_configs\\context_manager.json\n"
            "Linux: ./run-demo.sh cli_demo_capi MODEL_DIR\n", encoding="utf-8")
        archives.append(Path(shutil.make_archive(str(output / f"phono-core-demos-{value}-{args.platform}"), archive_format, demos)))
    manifest = output / f"SHA256SUMS-{args.platform}.txt"
    manifest.write_text("".join(f"{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}\n" for archive in archives))
    print(manifest)
