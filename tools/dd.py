#!/usr/bin/env python3
# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Cross-platform build backend behind the dd gateway. dd.ps1 owns the Windows release
# machinery (MSBuild, signing, MSIX, GitHub); this owns the CMake/Ninja build, and is the only
# place that knows how to bring a Linux machine up to being able to build Diffractor.
#
# Invoke through the gateway rather than directly: ./dd <command> or .\dd.ps1 <command>.

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "tmp" / "linux-build"

# Build tooling. Everything Diffractor cannot be compiled without.
APT_TOOLCHAIN = [
    "build-essential",
    "cmake",
    "ninja-build",
    "pkg-config",
    "ccache",
    "nasm",  # FFmpeg and dav1d assembly
    "python3",
    "git",
    "git-lfs",  # exe/test fixtures are LFS objects, see .gitattributes
]

# System copies of vendored libraries, used during bring-up. These are a convenience: the shipped
# build vendors them (see docs/third-party.md). FFmpeg and the XMP toolkit are deliberately absent
# because they are Diffractor forks and must never come from a distribution.
APT_DEPENDENCIES = [
    "zlib1g-dev",
    "libpng-dev",
    "libjpeg-dev",  # virtual: resolves to libjpeg-turbo on Debian and Ubuntu
    "libsqlite3-dev",
    "libexpat1-dev",
    "libwebp-dev",
    "libarchive-dev",
    "libexif-dev",
    "libhunspell-dev",  # util_spell.cpp
    "libminizip-dev",  # util_zip.cpp
]


def is_windows() -> bool:
    return platform.system() == "Windows"


def say(message: str) -> None:
    print(message, flush=True)


def run(command: list[str], cwd: Path | None = None, check: bool = True) -> int:
    say("  $ " + " ".join(command))
    result = subprocess.run(command, cwd=str(cwd or REPO_ROOT))
    if check and result.returncode != 0:
        sys.exit(result.returncode)
    return result.returncode


def missing_apt_packages(packages: list[str]) -> list[str]:
    missing = []
    for package in packages:
        installed = subprocess.run(
            ["dpkg-query", "-W", "-f=${Status}", package],
            capture_output=True,
            text=True,
        )
        if "install ok installed" not in installed.stdout:
            missing.append(package)
    return missing


def require_linux(command: str) -> None:
    if is_windows():
        sys.exit(f"'{command}' is a Linux command; on Windows use .\\dd.ps1 build.")


def configure_git_for_mirrored_tree() -> None:
    """Stop git misreporting a working tree copied from Windows.

    Two things make the whole tree look modified, and neither is a real change: the Windows
    checkout has core.autocrlf=true so every text file holds CRLF, and exe/test is Git LFS, so
    without the filter git compares real bytes against a pointer blob. Left alone, a commit from
    here would rewrite every text file and replace every LFS pointer with a raw binary.
    """
    if not (REPO_ROOT / ".git").exists():
        return

    run(["git", "config", "core.autocrlf", "true"], check=False)

    if shutil.which("git-lfs"):
        run(["git", "lfs", "install", "--local"], check=False)
    else:
        say("  git-lfs not found: exe/test fixtures will look modified and must not be committed.")


# -------------------------------------------------------------------------------------------
# Commands
# -------------------------------------------------------------------------------------------

def cmd_setup(args: argparse.Namespace) -> None:
    """Install the toolchain and the system packages the build resolves against."""
    require_linux("setup")

    if not shutil.which("apt-get"):
        sys.exit("setup currently understands apt only. Install the equivalents by hand:\n"
                 f"  toolchain:    {' '.join(APT_TOOLCHAIN)}\n"
                 f"  dependencies: {' '.join(APT_DEPENDENCIES)}")

    wanted = APT_TOOLCHAIN + APT_DEPENDENCIES
    missing = missing_apt_packages(wanted)

    say("")
    say(f"Diffractor setup  ({len(wanted) - len(missing)}/{len(wanted)} already present)")
    say("")

    for package in wanted:
        say(f"  {'MISSING' if package in missing else 'ok     '}  {package}")

    say("")

    if not missing:
        say("Nothing to install.")
        return

    if args.check:
        say(f"{len(missing)} package(s) missing. Run './dd setup' to install them.")
        sys.exit(1)

    say("Installing. This needs sudo and will prompt for your password.")
    say("")
    run(["sudo", "apt-get", "update"])
    run(["sudo", "apt-get", "install", "-y", *missing])
    say("")
    say("Configuring git for a working tree mirrored from Windows.")
    configure_git_for_mirrored_tree()
    say("")
    say("Setup complete. Next: ./dd build")


def cmd_configure(args: argparse.Namespace) -> None:
    require_linux("configure")

    generator = "Ninja" if shutil.which("ninja") else "Unix Makefiles"
    if generator != "Ninja":
        say("ninja not found, falling back to make. Run './dd setup' for the faster build.")

    command = [
        "cmake",
        "-S", str(REPO_ROOT),
        "-B", str(BUILD_DIR),
        "-G", generator,
        f"-DCMAKE_BUILD_TYPE={args.config}",
    ]
    command += [f"-D{d}" for d in (args.define or [])]
    run(command)


def cmd_build(args: argparse.Namespace) -> None:
    require_linux("build")

    if not (BUILD_DIR / "CMakeCache.txt").exists():
        cmd_configure(args)

    run(["cmake", "--build", str(BUILD_DIR), "-j", str(args.jobs)])
    say("")
    say(f"Built {BUILD_DIR / 'diffractor'}")


def cmd_run(args: argparse.Namespace) -> None:
    require_linux("run")

    binary = BUILD_DIR / "diffractor"
    if not binary.exists():
        sys.exit(f"{binary} does not exist. Run './dd build' first.")
    run([str(binary), *args.rest], check=False)


def cmd_test(args: argparse.Namespace) -> None:
    require_linux("test")

    binary = BUILD_DIR / "diffractor"
    if not binary.exists():
        sys.exit(f"{binary} does not exist. Run './dd build' first.")
    # The headless test runner is not in the Linux build yet: run_console_tests needs
    # view_state and index_state, which need translation units that do not compile. See
    # docs/linux.md for what remains.
    sys.exit(run([str(binary), "/test", *args.rest], check=False))


def cmd_clean(args: argparse.Namespace) -> None:
    if BUILD_DIR.exists():
        say(f"Removing {BUILD_DIR}")
        shutil.rmtree(BUILD_DIR)
    else:
        say("Nothing to clean.")


def cmd_info(args: argparse.Namespace) -> None:
    say("")
    say(f"  repo         {REPO_ROOT}")
    say(f"  build dir    {BUILD_DIR}")
    say(f"  host         {platform.system()} {platform.release()}")
    say(f"  python       {platform.python_version()}")
    say("")
    for tool in ("cmake", "ninja", "g++", "clang++", "pkg-config", "ccache", "nasm"):
        found = shutil.which(tool)
        say(f"  {tool:<12} {found or 'MISSING'}")
    say("")


COMMANDS = {
    "setup": cmd_setup,
    "configure": cmd_configure,
    "build": cmd_build,
    "run": cmd_run,
    "test": cmd_test,
    "clean": cmd_clean,
    "info": cmd_info,
}


def main() -> None:
    parser = argparse.ArgumentParser(prog="dd", description="Diffractor build backend")
    parser.add_argument("command", choices=sorted(COMMANDS))
    parser.add_argument("--config", default="Debug", help="CMake build type (default: Debug)")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--define", "-D", action="append", help="extra -D passed to CMake")
    parser.add_argument("--check", action="store_true", help="report, do not change anything")
    parser.add_argument("rest", nargs=argparse.REMAINDER)

    args = parser.parse_args()
    COMMANDS[args.command](args)


if __name__ == "__main__":
    main()
