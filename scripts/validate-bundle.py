#!/usr/bin/env python3
"""Validates OpenFX bundles: layout, Info.plist agreement, exported symbols, and architectures.

    scripts/validate-bundle.py <bundle-or-directory>...

A directory argument is searched for *.ofx.bundle. Exit status 1 when any bundle fails.
"""
import plistlib
import re
import subprocess
import sys
from pathlib import Path

ARCH_DIRS = {
    "darwin": ["MacOS"],
    "linux": ["Linux-x86-64", "Linux-aarch64"],
    "win32": ["Win64", "Win-arm64", "Win-arm64x", "Win-arm64ec"],
}


def run(cmd):
    try:
        return subprocess.run(cmd, check=False, capture_output=True, text=True).stdout
    except FileNotFoundError:
        return ""


def validate(bundle: Path) -> list:
    problems = []
    if not bundle.is_dir():
        return [f"{bundle}: not a directory"]
    if bundle.is_symlink():
        problems.append("the bundle is a symlink (Q-RESOLVE-025)")
    name = bundle.name
    if not name.endswith(".ofx.bundle"):
        problems.append("name does not end in .ofx.bundle")
    binary_name = name[: -len(".bundle")] if name.endswith(".bundle") else name
    contents = bundle / "Contents"
    if not contents.is_dir():
        return problems + ["missing Contents/"]
    platform_dirs = ARCH_DIRS["darwin"] if sys.platform == "darwin" else ARCH_DIRS["win32"] if sys.platform.startswith("win") else ARCH_DIRS["linux"]
    binaries = [contents / d / binary_name for d in platform_dirs if (contents / d / binary_name).exists()]
    if not binaries:
        problems.append(f"no binary named {binary_name} under Contents/{{{', '.join(platform_dirs)}}}")
    for binary in binaries:
        if sys.platform == "darwin":
            symbols = run(["nm", "-gU", str(binary)])
            for needed in ("OfxGetNumberOfPlugins", "OfxGetPlugin"):
                if not re.search(rf"\b_{needed}\b", symbols):
                    problems.append(f"{binary.name}: does not export {needed}")
            archs = run(["lipo", "-archs", str(binary)]).split()
            if not archs:
                problems.append(f"{binary.name}: lipo reports no architectures")
            codesign = subprocess.run(["codesign", "--verify", "--strict", "--deep", str(bundle)], capture_output=True, text=True)
            if codesign.returncode != 0:
                problems.append(f"codesign --verify failed ({codesign.stderr.strip() or 'unsigned'})")
        elif sys.platform.startswith("linux"):
            symbols = run(["nm", "-D", str(binary)])
            for needed in ("OfxGetNumberOfPlugins", "OfxGetPlugin"):
                if needed not in symbols:
                    problems.append(f"{binary.name}: does not export {needed}")
    if sys.platform == "darwin":
        plist_path = contents / "Info.plist"
        if not plist_path.exists():
            problems.append("missing Contents/Info.plist")
        else:
            with open(plist_path, "rb") as f:
                info = plistlib.load(f)
            executable = info.get("CFBundleExecutable")
            if executable != binary_name:
                problems.append(f"Info.plist CFBundleExecutable is {executable!r}, expected {binary_name!r}")
            for key in ("CFBundleIdentifier", "CFBundleShortVersionString", "CFBundlePackageType"):
                if key not in info:
                    problems.append(f"Info.plist lacks {key}")
            if info.get("CFBundlePackageType") not in (None, "BNDL"):
                problems.append("Info.plist CFBundlePackageType is not BNDL")
    return problems


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    bundles = []
    for arg in argv[1:]:
        path = Path(arg)
        if path.name.endswith(".ofx.bundle"):
            bundles.append(path)
        elif path.is_dir():
            bundles.extend(sorted(path.rglob("*.ofx.bundle")))
    if not bundles:
        print("no bundles found")
        return 1
    failed = 0
    for bundle in bundles:
        problems = validate(bundle)
        status = "ok" if not problems else "FAIL"
        print(f"{status}: {bundle}")
        for problem in problems:
            print(f"  - {problem}")
        failed += bool(problems)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
