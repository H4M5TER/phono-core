"""Prepare the exact ExecuTorch source and gitlinks used by phono-core."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent


def run(*args: str, capture: bool = False) -> str:
    result = subprocess.run(args, check=True, text=True, capture_output=capture)
    return result.stdout.strip() if capture else ""


def prepare(target: Path) -> Path:
    lock = json.loads((ROOT / "ci/core-dependencies.json").read_text())["executorch"]
    target = target.resolve()
    if not (target / ".git").exists():
        target.parent.mkdir(parents=True, exist_ok=True)
        run("git", "clone", "--filter=blob:none", "--no-checkout", lock["url"], str(target))
    # A fresh --no-checkout clone has no worktree yet and reports tracked files as deleted.
    elif run("git", "-C", str(target), "status", "--porcelain", capture=True):
        raise ValueError("ExecuTorch checkout has local changes; refusing to replace them")
    if subprocess.run(["git", "-C", str(target), "cat-file", "-e", lock["commit"]],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        run("git", "-C", str(target), "fetch", "--depth", "1", "origin", lock["commit"])
    run("git", "-C", str(target), "checkout", "--detach", lock["commit"])
    run("git", "-C", str(target), "submodule", "sync", "--recursive")
    run("git", "-C", str(target), "submodule", "update", "--init", "--recursive",
        "--", *lock["submodules"])
    actual = run("git", "-C", str(target), "rev-parse", "HEAD", capture=True)
    status = subprocess.check_output(["git", "-C", str(target), "submodule", "status", "--recursive",
                                      "--", *lock["submodules"]], text=True)
    if actual != lock["commit"] or any(line[0] != " " for line in status.splitlines()):
        raise ValueError("ExecuTorch source or submodules differ from the dependency lock")
    return target


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "third_party/executorch")
    print(prepare(parser.parse_args().source))
