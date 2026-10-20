"""Build, test and install the standalone Windows x64 C API SDK."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from setup_executorch import prepare
from release_version import version


def run(args, environment):
    return subprocess.run([str(arg) for arg in args], env=environment, check=True)


def notices(prefix: Path, executorch: Path, build: Path) -> None:
    dependencies = json.loads((ROOT / "ci/core-dependencies.json").read_text())
    for component in ["", *dependencies["executorch"]["submodules"]]:
        directory = executorch / component
        for filename in ("LICENSE", "LICENSE.MIT", "LICENSE.txt", "COPYING", "COPYING.txt"):
            source = directory / filename
            if source.is_file():
                target = prefix / "share/licenses/phono-core/third_party/executorch" / component / filename
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
    for dependency in ("uni-algo", "nlohmann-json"):
        candidates = [directory / "x64-windows-static/share" / dependency / "copyright"
                      for directory in (build / "vcpkg_installed", ROOT / "vcpkg_installed")]
        notice = next((path for path in candidates if path.is_file()), None)
        if notice is None:
            raise ValueError(f"The {dependency} license is missing from the dependency installation")
        target = prefix / "share/licenses/phono-core/third_party" / dependency / "copyright"
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(notice, target)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/windows-sdk")
    parser.add_argument("--prefix", type=Path, default=ROOT / "build/sdk-windows")
    parser.add_argument("--executorch-source", type=Path, default=ROOT / "third_party/executorch")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--tag")
    args = parser.parse_args()
    value = version(args.tag)
    if sys.platform != "win32":
        raise SystemExit("Use ci_build_core.py inside ci/pixi.toml for Linux x86_64")
    build, prefix = args.build_dir.resolve(), args.prefix.resolve()
    executorch = prepare(args.executorch_source)
    environment = {**os.environ, "PYTHONPATH": str(executorch.parent)}
    if "VCPKG_ROOT" not in environment and "VCPKG_INSTALLATION_ROOT" in environment:
        environment["VCPKG_ROOT"] = environment["VCPKG_INSTALLATION_ROOT"]
    run(["cmake", "-S", ROOT, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         "-DCMAKE_INSTALL_LIBDIR=lib", "-DPHONO_DTYPE_SELECTIVE_BUILD=OFF", "-DBUILD_TESTING=ON",
         f"-DPython3_EXECUTABLE={sys.executable}", f"-DEXECUTORCH_SOURCE_DIR={executorch}",
         f"-DPHONO_RUNTIME_OUTPUT_DIR={build / 'bin'}"], environment)
    run(["cmake", "--build", build, "--parallel", args.jobs, "--target", "phono_core_shared",
         "phono_core_tests", "phono_capi_tests", "phono_engine_tests", "phono_ime_editor_tests"], environment)
    environment["PATH"] = str(build / "bin") + os.pathsep + environment["PATH"]
    run(["ctest", "--test-dir", build, "-L", "unit", "--output-on-failure", "--no-tests=error"], environment)
    run(["cmake", "--install", build, "--prefix", prefix, "--component", "sdk"], environment)
    notices(prefix, executorch, build)
    metadata = {"version": value, "c_abi": 1, "dependencies": json.loads((ROOT / "ci/core-dependencies.json").read_text()),
                "source_commit": subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip(),
                "dtype_selective_build": False}
    (prefix / "share/phono-core/build-info.json").write_text(json.dumps(metadata, indent=2) + "\n")
    consumer = build / "sdk-consumer"
    run(["cmake", "-S", ROOT / "tests/sdk_consumer", "-B", consumer, "-G", "Ninja",
         "-DCMAKE_BUILD_TYPE=Release", f"-DCMAKE_PREFIX_PATH={prefix}"], environment)
    run(["cmake", "--build", consumer, "--parallel", args.jobs], environment)
    environment["PATH"] = str(prefix / "bin") + os.pathsep + environment["PATH"]
    run(["ctest", "--test-dir", consumer, "--output-on-failure"], environment)
    print(prefix)
