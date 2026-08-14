#!/usr/bin/env python3
# This file is part of the Diffractor photo and video organizer
# Copyright 2026  Zac Walker
#
# Purpose: Import a vendored library's hand-written .vcxproj into a CMake module. Run once per
# library to migrate, and again whenever a vendored library is re-synced from upstream and its
# project file changes.
#
# The generated module carries an explicit source list rather than parsing the project at configure
# time, because the .vcxproj files are being deleted: after the migration this script has no input
# and the generated module is the source of truth.

from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

MSBUILD_NS = "{http://schemas.microsoft.com/developer/msbuild/2003}"

ROOT = Path(__file__).resolve().parent.parent

# vcxproj stem -> the name the build uses for diffractor::<name>.
TARGET_NAMES = {
    "ZLib": "zlib",
    "libpng": "png",
    "libarchive": "archive",
    "libexif": "exif",
    "libheif": "heif",
    "libraw": "raw",
    "libde265": "de265",
    "liblzma": "lzma",
    "libebml": "ebml",
    "libmatroska": "matroska",
    "libopenmpt": "openmpt",
    "jxl": "jxl",
    # Windows only, and included by FFmpeg.cmake rather than declared as a dependency of its own:
    # elsewhere the fork is built by its own configure.
    "ffmpeg": "ffmpeg_msvc",
}

# Already hand-written, because driving the library's own build system was the only honest option.
SKIP = {"jpeg12", "jpeg16", "LibJpeg", "xmp"}

# Directories a consumer needs that the library's own build did not: its sources reach these headers
# by a relative path, so the project never had to say where they are.
EXTRA_INCLUDES = {
    "expat": ["third-party/expat/lib"],
}

# Supplied by DiffractorCompilerPolicy.cmake or by the configuration, so restating them per library
# would be a second place to change them.
DROP_DEFINES = {
    "NDEBUG", "_DEBUG", "DEBUG", "WIN32", "_WIN32", "_WINDOWS", "_LIB", "_CONSOLE",
    "_CRT_SECURE_NO_WARNINGS", "_CRT_SECURE_NO_DEPRECATE", "_CRT_NONSTDC_NO_WARNINGS",
    "_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES", "_SCL_SECURE_NO_WARNINGS",
    "_WINSOCK_DEPRECATED_NO_WARNINGS",
}

PREFERRED_CONFIGS = ("Release|x64", "Debug|x64", "Release|Win32")


def text_of(element) -> str:
    return (element.text or "").strip() if element is not None else ""


def item_definition_groups(root):
    """Yields (condition, ClCompile) for every such block. A configuration often has more than one,
    the first of them empty, so taking only the first loses the settings entirely."""
    for group in root.findall(f"{MSBUILD_NS}ItemDefinitionGroup"):
        for clcompile in group.findall(f"{MSBUILD_NS}ClCompile"):
            yield group.get("Condition", ""), clcompile


def applies(condition: str, config: str) -> bool:
    """Metadata with no condition applies to every configuration; otherwise it names one."""
    return not condition or config in condition


def pick_config(root) -> str:
    """The Release x64 configuration if the project has one, else the first it declares."""
    conditions = [condition for condition, _ in item_definition_groups(root)]

    for wanted in PREFERRED_CONFIGS:
        if any(wanted in condition for condition in conditions):
            return wanted

    for condition in conditions:
        match = re.search(r"'([^']+\|[^']+)'\s*$", condition)
        if match:
            return match.group(1)

    return ""


def relative(include: str, project_dir: Path) -> str | None:
    path = (project_dir / include.replace("\\", "/")).resolve()

    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return None


def sources(root, project_dir: Path, config: str) -> list[str]:
    result = []

    for tag in ("ClCompile", "MASM", "NASM"):
        for item in root.iter(f"{MSBUILD_NS}{tag}"):
            include = item.get("Include")
            if not include:
                continue

            # Exclusions are per configuration and are usually about the target architecture, so
            # importing them wholesale would compile files the shipped build does not.
            if any(applies(e.get("Condition", ""), config) and text_of(e).lower() == "true"
                   for e in item.findall(f"{MSBUILD_NS}ExcludedFromBuild")):
                continue

            rel = relative(include, project_dir)

            if rel and rel not in result:
                result.append(rel)

    return result


def expand(value: str, project_dir: Path) -> str | None:
    """Turns an MSBuild path into one relative to the project root, or None if it does not resolve."""
    value = value.strip().replace("\\", "/")

    if not value or value.startswith("%("):
        return None

    if "$(SolutionDir)" in value:
        candidate = ROOT / value.replace("$(SolutionDir)", "").lstrip("/")
    elif "$(ProjectDir)" in value:
        candidate = project_dir / value.replace("$(ProjectDir)", "").lstrip("/")
    elif "$(" in value:
        return None
    else:
        candidate = project_dir / value

    candidate = Path(str(candidate)).resolve()

    if not candidate.is_dir():
        return None

    try:
        return candidate.relative_to(ROOT).as_posix()
    except ValueError:
        return None


def expand_file(value: str, project_dir: Path) -> str | None:
    """As expand(), for a file rather than a directory."""
    value = value.strip().replace("\\", "/")

    if not value or value.startswith("%("):
        return None

    if "$(SolutionDir)" in value:
        candidate = ROOT / value.replace("$(SolutionDir)", "").lstrip("/")
    elif "$(ProjectDir)" in value:
        candidate = project_dir / value.replace("$(ProjectDir)", "").lstrip("/")
    elif "$(" in value:
        return None
    else:
        candidate = project_dir / value

    candidate = Path(str(candidate)).resolve()

    if not candidate.is_file():
        return None

    try:
        return candidate.relative_to(ROOT).as_posix()
    except ValueError:
        return None


def include_dirs(root, project_dir: Path) -> list[str]:
    """The union across configurations. A project often lists these on one configuration only, and a
    header found through them is needed by every build of it."""
    result = []

    # The project's own directory. MSBuild puts it on the line implicitly, and a header included
    # as "zbuild.h" from a source in a subdirectory is only found through it.
    own = expand(".", project_dir)
    if own:
        result.append(own)

    for _, clcompile in item_definition_groups(root):
        raw = text_of(clcompile.find(f"{MSBUILD_NS}AdditionalIncludeDirectories"))

        for part in raw.split(";"):
            rel = expand(part, project_dir)
            if rel and rel not in result:
                result.append(rel)

    return result

def asm_settings(root, project_dir: Path, config: str) -> tuple[list[str], list[str], list[str]]:
    """Assembler include paths, definitions and pre-included files. These live in their own item
    definition and are not compiler settings: FFmpeg gets its generated configuration into nasm by
    pre-including config-x64.asm, which differs per configuration."""
    dirs, defs, pre = [], [], []

    for group in root.findall(f"{MSBUILD_NS}ItemDefinitionGroup"):
        if not applies(group.get("Condition", ""), config):
            continue

        for tag in ("nasm", "NASM", "MASM"):
            # findall, not find: a group carries several of these and the pre-include sits in a
            # later one, exactly as the compiler settings do.
            for element in group.findall(f"{MSBUILD_NS}{tag}"):
                for part in text_of(element.find(f"{MSBUILD_NS}IncludePaths")).split(";"):
                    rel = expand(part, project_dir)
                    if rel and rel not in dirs:
                        dirs.append(rel)

                for part in text_of(element.find(f"{MSBUILD_NS}PreprocessorDefinitions")).split(";"):
                    part = part.strip()
                    if part and not part.startswith("%(") and part not in defs:
                        defs.append(part)

                for part in text_of(element.find(f"{MSBUILD_NS}PreIncludeFiles")).split(";"):
                    rel = expand_file(part, project_dir)
                    if rel and rel not in pre:
                        pre.append(rel)

    return dirs, defs, pre


def filter_defines(raw: str) -> list[str]:
    result = []

    for part in raw.split(";"):
        part = part.strip()
        name = part.split("=", 1)[0]

        if not part or part.startswith("%(") or name in DROP_DEFINES or name == "CMAKE_INTDIR":
            continue
        if part not in result:
            result.append(part)

    return result


def additional_options(root, config: str) -> list[str]:
    """Raw switches the project passes. dav1d needs /experimental:c11atomics to see stdatomic.h."""
    result = []

    for condition, clcompile in item_definition_groups(root):
        if not applies(condition, config):
            continue

        for part in text_of(clcompile.find(f"{MSBUILD_NS}AdditionalOptions")).split():
            if part.startswith("%(") or part in result:
                continue
            result.append(part)

    return result


def character_set(root, config: str) -> list[str]:
    """CharacterSet Unicode is what selects the wide Win32 entry points, and code that calls
    FindNextFile with a WIN32_FIND_DATAW does not compile without it."""
    for group in root.findall(f"{MSBUILD_NS}PropertyGroup"):
        if not applies(group.get("Condition", ""), config):
            continue

        if text_of(group.find(f"{MSBUILD_NS}CharacterSet")) == "Unicode":
            return ["UNICODE", "_UNICODE"]

    return []


def defines(root, config: str) -> list[str]:
    """The union across every ClCompile block of the chosen configuration."""
    result = []

    for condition, clcompile in item_definition_groups(root):
        if not applies(condition, config):
            continue

        for value in filter_defines(text_of(clcompile.find(f"{MSBUILD_NS}PreprocessorDefinitions"))):
            if value not in result:
                result.append(value)

    return result


def per_file_defines(root, project_dir: Path, config: str) -> dict[str, list[str]]:
    """Definitions set on a file rather than the project. MSBuild lets these replace the project's,
    so sqlite3.c does not get the cache size the project-wide setting names."""
    result = {}

    for item in root.iter(f"{MSBUILD_NS}ClCompile"):
        include = item.get("Include")
        if not include:
            continue

        for child in item.findall(f"{MSBUILD_NS}PreprocessorDefinitions"):
            if not applies(child.get("Condition", ""), config):
                continue

            values = filter_defines(text_of(child))
            rel = relative(include, project_dir)

            if values and rel:
                result[rel] = values

    return result


def render(name: str, project: Path, config: str, srcs: list[str], incs: list[str], defs: list[str],
           file_defs: dict[str, list[str]], asm_incs: list[str], asm_defs: list[str],
           asm_pre: list[str], options: list[str]) -> str:
    target = f"diffractor_{name}"
    lines = [
        "# This file is part of the Diffractor photo and video organizer",
        "# Copyright 2026  Zac Walker",
        "#",
        f"# Purpose: Build the vendored {project.parent.name} as diffractor::{name}.",
        "#",
        f"# Imported from {project.relative_to(ROOT).as_posix()} ({config}) by tools/import_vcxproj.py.",
        "# Compiler and linker flags are not restated here: DiffractorCompilerPolicy.cmake owns those",
        "# for every target.",
        "",
        "include_guard(GLOBAL)",
        "",
        f"add_library({target} STATIC",
    ]

    for s in srcs:
        lines.append(f"        \"${{CMAKE_SOURCE_DIR}}/{s}\"")

    lines.append(")")
    lines.append("")

    if incs:
        lines.append(f"target_include_directories({target} PUBLIC")
        for i in incs:
            lines.append(f"        \"${{CMAKE_SOURCE_DIR}}/{i}\"")
        lines.append(")")
        lines.append("")

    if defs:
        # Quoted individually: CMake reads an unquoted ( or ) as list syntax, which turned
        # SQLITE_DEFAULT_PAGE_SIZE=(1024*16) into four separate definitions. Guarded by language
        # because a ClCompile definition never reached the assembler under MSBuild either.
        quoted = " ".join(f'"$<$<COMPILE_LANGUAGE:C,CXX>:{d}>"' for d in defs)
        lines.append(f"target_compile_definitions({target} PRIVATE {quoted})")
        lines.append("")

    asm = [s for s in srcs if s.endswith(".asm")]

    if asm:
        # Every %include is resolved against these. The project's own directory is stated, and so is
        # the assembling file's, which MSBuild supplied per item as %(RootDir)%(Directory) and which
        # therefore differs from one group of files to the next.
        by_dir: dict[str, list[str]] = {}
        for a in asm:
            by_dir.setdefault(str(Path(a).parent.as_posix()), []).append(a)

        lines.append("# Assembler include paths and definitions. These are not compiler settings and reach")
        lines.append("# nasm no other way. The object name keeps the source's relative directory because")
        lines.append("# FFmpeg has several same-named .asm files - hevc/sao.asm and vvc/sao.asm among them -")
        lines.append("# and a flat object directory silently lets one overwrite the other.")

        for directory, files in sorted(by_dir.items()):
            dirs = [directory] + [d for d in asm_incs if d != directory]
            flags = " ".join(f"-I${{CMAKE_SOURCE_DIR}}/{d}/" for d in dirs)
            flags += "".join(f" -D{d}" for d in asm_defs)
            flags += "".join(f" -P${{CMAKE_SOURCE_DIR}}/{p}" for p in asm_pre)

            lines.append("set_source_files_properties(")
            for f in files:
                lines.append(f"        \"${{CMAKE_SOURCE_DIR}}/{f}\"")
            lines.append(f"        PROPERTIES COMPILE_FLAGS \"{flags}\"")
            lines.append("        VS_SETTINGS \"ObjectFileName=$(IntDir)%(RelativeDir)%(FileName).obj\")")

        lines.append("")

    for path, values in file_defs.items():
        lines.append("# Set on the file rather than the project, and replacing rather than adding to")
        lines.append("# whatever the project sets.")
        lines.append(f"set_source_files_properties(\"${{CMAKE_SOURCE_DIR}}/{path}\"")
        lines.append(f"        PROPERTIES COMPILE_DEFINITIONS \"{';'.join(values)}\")")
        lines.append("")

    if options:
        guarded = " ".join(f"$<$<COMPILE_LANGUAGE:C,CXX>:{o}>" for o in options)
        lines.append(f"target_compile_options({target} PRIVATE {guarded})")
        lines.append("")

    lines.append(f"diffractor_apply_vendored_policy({target})")
    lines.append("")
    lines.append(f"add_library(diffractor::{name} ALIAS {target})")
    lines.append("")

    return "\n".join(lines)


def main() -> int:
    projects = sorted(ROOT.glob("third-party/*/*.vcxproj")) + sorted(ROOT.glob("third-party/*/*/*.vcxproj"))
    out_dir = ROOT / "cmake" / "vendored"
    out_dir.mkdir(parents=True, exist_ok=True)

    written, skipped = [], []

    for project in projects:
        stem = project.stem

        if stem in SKIP:
            skipped.append(f"{stem} (hand-written)")
            continue

        name = TARGET_NAMES.get(stem, stem.lower())

        try:
            root = ET.parse(project).getroot()
        except ET.ParseError as e:
            skipped.append(f"{stem} (unparsable: {e})")
            continue

        config = pick_config(root)
        srcs = sources(root, project.parent, config)

        if not srcs:
            skipped.append(f"{stem} (no sources)")
            continue

        module = out_dir / f"{name}.cmake"
        incs = include_dirs(root, project.parent)

        for extra in EXTRA_INCLUDES.get(name, []):
            if extra not in incs:
                incs.append(extra)

        asm_incs, asm_defs, asm_pre = asm_settings(root, project.parent, config)

        module.write_text(
            render(name, project, config, srcs,
                   incs,
                   defines(root, config) + character_set(root, config),
                   per_file_defines(root, project.parent, config),
                   asm_incs,
                   asm_defs,
                   asm_pre,
                   additional_options(root, config)),
            encoding="utf-8",
            newline="\n",
        )
        written.append(f"{name:<12} {len(srcs):>4} sources  <- {project.name}")

    print("=== written ===")
    for w in written:
        print(" ", w)

    print("\n=== skipped ===")
    for s in skipped:
        print(" ", s)

    return 0


if __name__ == "__main__":
    sys.exit(main())
