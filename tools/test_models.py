"""Run mandatory real-inference tests with explicit model package directories."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess

from fetch_models import validate


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, required=True)
    parser.add_argument("--model-dir", type=Path, action="append", required=True)
    args = parser.parse_args()
    for directory in args.model_dir:
        directory = directory.resolve(strict=True)
        validate(directory)
        environment = {**os.environ, "PHONO_TEST_MODEL_DIR": str(directory), "PHONO_REQUIRE_TEST_MODEL": "1"}
        if os.name == "nt":
            environment["PATH"] = str(args.runtime_dir.resolve()) + os.pathsep + environment.get("PATH", "")
        else:
            environment["LD_LIBRARY_PATH"] = str(args.runtime_dir.resolve())
        subprocess.run(["ctest", "--test-dir", str(args.build_dir), "-C", "Release", "-L", "model",
                        "--output-on-failure", "--no-tests=error"], env=environment, check=True)
