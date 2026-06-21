"""Load every Diffractor translation file and compare them side by side.

This reads all of ``exe/languages/*.po`` and builds a per-string view across
every language so the translations can be reviewed for consistency. It is meant
to give a human (or an agent) an easy way to spot:

  * untranslated / absent strings - a source string that exists in the union of
    all ``.po`` files but is empty or missing in one or more languages;
  * placeholder/token mismatches - the English source uses a token such as
    ``{count}`` or ``{first-name}`` but a translation drops, adds or misspells
    it. Diffractor substitutes these tokens at runtime, so a mismatch is a real
    bug;
  * quality mismatches in existing translations - objective, language-agnostic
    structural differences between a source string and its translation: leading
    or trailing whitespace (matters because several strings are concatenated in
    the UI), embedded newline count, a dropped trailing colon, or a dropped
    sentence-ending ``.``/``?``/``!`` (the last is skipped for CJK text, where
    omitting the period on labels is the normal convention);
  * ``msgstr[2]`` usage - Diffractor uses a binary ``msgstr[0]`` / ``msgstr[1]``
    (one / plural) model, so any third plural form (used by e.g. Czech) is
    ignored by ``load_po`` and never shown. The report notes which strings carry
    one so the unused form is not mistaken for a translation that takes effect.

The English source text *is* the ``msgid`` in each ``.po`` file (Diffractor's
``gen_po`` test generates the msgids from the in-code strings), so the union of
all msgids is used as the reference set of source strings.

Usage (from the repo root, using the project virtual environment)::

    tools\\.venv\\Scripts\\python.exe tools\\check_translations.py

A full Markdown report grouped by source string is written to
``tmp/translation_report.md`` and a summary plus the list of problems is printed
to stdout. Pass ``--full`` to also dump every string to stdout.

The parser is intentionally self-contained (no third-party dependency) and
tolerant in the same way Diffractor's own ``load_po`` is - e.g. it accepts the
stray trailing semicolons present in some ``.po`` files.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import OrderedDict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LANG_DIR = REPO_ROOT / "exe" / "languages"
DEFAULT_REPORT = REPO_ROOT / "tmp" / "translation_report.md"

# Runtime placeholders look like {count}, {first-name}, {total}, {} ...
TOKEN_RE = re.compile(r"\{[^{}]*\}")

# Mirrors language_name() in src/app_text.cpp.
LANGUAGE_NAMES = {
    "br": "Breton",
    "cs": "Czech",
    "de": "German",
    "en": "English",
    "es": "Spanish",
    "fr": "French",
    "it": "Italian",
    "ja": "Japanese",
    "lv": "Latvian",
    "nl": "Dutch",
    "pl": "Polish",
    "pt": "Portuguese",
    "ru": "Russian",
    "sr": "Serbian",
    "zh": "Chinese",
}


def language_name(code: str) -> str:
    return LANGUAGE_NAMES.get(code, code)


def tokens(text: str) -> list[str]:
    """Return the sorted, de-duplicated set of {placeholder} tokens in text."""
    return sorted(set(TOKEN_RE.findall(text)))


# Phrase-terminating punctuation, ASCII plus the full-width / CJK forms used by
# Japanese so the check does not flag legitimate Japanese punctuation.
_TERMINAL_MAP = {
    "。": ".", "．": ".", "：": ":", "？": "?", "！": "!", "…": "…", "‥": "…",
    ".": ".", ":": ":", "?": "?", "!": "!",
}


def _terminal(text: str) -> str:
    """Return the normalised trailing terminal punctuation of text, or ''."""
    text = text.rstrip()
    if not text:
        return ""
    return _TERMINAL_MAP.get(text[-1], "")


def _has_cjk(text: str) -> bool:
    """True if text contains Japanese/Chinese script.

    Japanese UI text conventionally omits the sentence-ending period on short
    labels and tooltips, so the dropped-period check is skipped for CJK to avoid
    flagging that deliberate, consistent style.
    """
    return any(
        "\u3040" <= ch <= "\u30ff"  # hiragana + katakana
        or "\u3400" <= ch <= "\u9fff"  # CJK ideographs
        or "\uff66" <= ch <= "\uff9f"  # half-width katakana
        for ch in text
    )


def quality_issues(src: str, got: str) -> list[str]:
    """Objective source-vs-translation mismatches in an existing translation.

    These are structural (not fluency) checks that are safe to flag without
    knowing the language: leading/trailing whitespace, embedded newline count,
    and presence of trailing terminal punctuation (``.``/``:``/``?``/``!``,
    CJK-aware). They catch dropped colons, broken concatenation spacing and
    truncated sentences.
    """
    issues: list[str] = []
    if not got.strip():
        return issues  # empty is reported separately as "untranslated"

    if src.startswith((" ", "\t")) != got.startswith((" ", "\t")):
        issues.append("leading-space mismatch")
    if src.endswith((" ", "\t")) != got.endswith((" ", "\t")):
        issues.append("trailing-space mismatch")
    if src.count("\n") != got.count("\n"):
        issues.append(f"newline mismatch (src {src.count(chr(10))} / got {got.count(chr(10))})")

    s_term, g_term = _terminal(src), _terminal(got)
    # A trailing colon is a structural label marker - flag if either side has it
    # but the other does not. Sentence enders (. ? !) are only flagged when the
    # source has one but a non-CJK translation dropped all terminal punctuation.
    if (s_term == ":") != (g_term == ":"):
        issues.append(f"colon mismatch (src {s_term!r} / got {g_term!r})")
    elif s_term in (".", "?", "!") and g_term == "" and not _has_cjk(got):
        issues.append(f"end-punctuation dropped (src {s_term!r})")

    return issues


def display(text: str) -> str:
    """Collapse a translation onto a single readable line for the report."""
    return text.replace("\r\n", "\n").replace("\n", "\\n").strip()


def un_escape(line: str) -> str:
    """Extract and un-escape the quoted value from a .po line.

    Mirrors ``un_escape`` in src/app_text.cpp: take the text between the first
    and last double quote, then resolve ``\\n``, ``\\"`` and ``\\\\``. This is
    deliberately lenient about anything outside the quotes (e.g. a stray
    trailing ``;``).
    """
    start = line.find('"')
    end = line.rfind('"')
    if start == -1 or end == -1 or start >= end:
        return ""
    result = line[start + 1:end]
    result = result.replace("\\n", "\n").replace('\\"', '"').replace("\\\\", "\\")
    return result


def _new_entry() -> dict:
    return {"id": "", "id_plural": "", "str": "", "str_plural": "", "str_extra": ""}


def _is_empty(entry: dict) -> bool:
    return not any(entry.values())


def parse_po(path: Path) -> list[dict]:
    """Parse a .po file into raw entries, mirroring Diffractor's load_po state
    machine but keeping ``msgstr[2]`` in its own ``str_extra`` field so the
    report can show both the correct value and the value Diffractor would load.
    """
    entries: list[dict] = []
    entry = _new_entry()
    state = None

    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.rstrip(" \t\r\n")
            if not line or line[0] == "#":
                continue

            if line.startswith("msgstr[1]"):
                state = "str_plural"
            elif line.startswith("msgstr[0]"):
                state = "str"
            elif line.startswith("msgstr[2]"):
                state = "str_extra"
            elif line.startswith("msgid_plural"):
                state = "id_plural"
            elif line.startswith("msgstr"):
                state = "str"
            elif line.startswith("msgid"):
                state = "id"
            # otherwise this is a continuation line ("...") and keeps the state

            if state == "id" and not _is_empty(entry) and line[0] != '"':
                entries.append(entry)
                entry = _new_entry()

            value = un_escape(line)
            if state is not None:
                entry[state] += value

    if not _is_empty(entry):
        entries.append(entry)

    return entries


def load_po_file(path: Path) -> "OrderedDict[str, dict]":
    """Parse one .po file into an ordered map: msgid -> entry data.

    ``msgstr`` is the singular (``msgstr`` or ``msgstr[0]``) and ``msgstr_plural``
    is ``msgstr[1]``. ``msgstr_extra`` holds ``msgstr[2]`` (only Czech uses it);
    Diffractor's load_po would fold this into the singular form.
    """
    out: "OrderedDict[str, dict]" = OrderedDict()
    for entry in parse_po(path):
        if entry["id"] == "":
            continue  # skip the header entry
        out[entry["id"]] = {
            "singular": entry["id"],
            "plural": entry["id_plural"],
            "msgstr": entry["str"],
            "msgstr_plural": entry["str_plural"],
            "msgstr_extra": entry["str_extra"],
        }
    return out


def analyse(languages: "OrderedDict[str, OrderedDict]"):
    """Build the reference source set and find per-string problems.

    Returns a tuple ``(source, problems)`` where ``source`` maps each msgid to
    its English singular/plural text and ``problems`` is a list of
    ``(msgid, {lang_code: [issue, ...]})`` for every string with at least one
    issue in at least one language.
    """
    source: "OrderedDict[str, dict]" = OrderedDict()
    for entries in languages.values():
        for msgid, data in entries.items():
            if msgid not in source:
                source[msgid] = {"singular": data["singular"], "plural": data["plural"]}

    problems = []

    for msgid, src in source.items():
        src_singular_tokens = tokens(src["singular"])
        src_plural_tokens = tokens(src["plural"]) if src["plural"] else []
        row_issues: "OrderedDict[str, list[str]]" = OrderedDict()

        for code, entries in languages.items():
            data = entries.get(msgid)
            issues: list[str] = []

            if data is None:
                issues.append("absent")
            else:
                if not data["msgstr"].strip():
                    issues.append("untranslated")
                else:
                    got = tokens(data["msgstr"])
                    if got != src_singular_tokens:
                        issues.append(f"token mismatch: src {src_singular_tokens} != {got}")
                    issues.extend(quality_issues(src["singular"], data["msgstr"]))

                if src["plural"]:
                    if not data["msgstr_plural"].strip():
                        issues.append("plural untranslated")
                    else:
                        got_plural = tokens(data["msgstr_plural"])
                        if got_plural != src_plural_tokens:
                            issues.append(
                                f"plural token mismatch: src {src_plural_tokens} != {got_plural}"
                            )
                        issues.extend(
                            f"plural {i}" for i in quality_issues(src["plural"], data["msgstr_plural"])
                        )

                if data["msgstr_extra"].strip():
                    issues.append("has msgstr[2] (extra plural form; ignored by load_po)")

            if issues:
                row_issues[code] = issues

        if row_issues:
            problems.append((msgid, row_issues))

    return source, problems


def compute_stats(languages, source, problems):
    """Per-language coverage and issue counts keyed by language code."""
    problem_map = {msgid: ri for msgid, ri in problems}
    stats: "OrderedDict[str, dict]" = OrderedDict()

    for code, entries in languages.items():
        total = len(source)
        translated = sum(1 for m in source if m in entries and entries[m]["msgstr"].strip())
        token_issues = sum(
            1
            for ri in problem_map.values()
            if code in ri and any("token" in issue for issue in ri[code])
        )
        extra_issues = sum(
            1
            for ri in problem_map.values()
            if code in ri and any("msgstr[2]" in issue for issue in ri[code])
        )
        quality = sum(
            1
            for ri in problem_map.values()
            if code in ri and any(
                ("space" in issue or "newline" in issue or "punctuation" in issue
                 or "colon" in issue)
                for issue in ri[code]
            )
        )
        stats[code] = {
            "total": total,
            "translated": translated,
            "missing": total - translated,
            "token_issues": token_issues,
            "quality": quality,
            "msgstr2": extra_issues,
        }

    return stats


def format_string_block(msgid, source, languages, row_issues=None):
    """Render one source string with every language's translation."""
    src = source[msgid]
    lines = [f"- **en**: `{display(src['singular'])}`"]
    if src["plural"]:
        lines.append(f"    - en *(plural)*: `{display(src['plural'])}`")

    for code, entries in languages.items():
        data = entries.get(msgid)
        if data is None:
            lines.append(f"    - **{code}**: _(absent)_")
            continue

        value = display(data["msgstr"]) if data["msgstr"].strip() else "_(untranslated)_"
        lines.append(f"    - **{code}**: {value}")
        if src["plural"]:
            plural_value = (
                display(data["msgstr_plural"]) if data["msgstr_plural"].strip() else "_(untranslated)_"
            )
            lines.append(f"        - plural: {plural_value}")

        if row_issues and code in row_issues:
            lines.append(f"        - :warning: {'; '.join(row_issues[code])}")

    return "\n".join(lines)


def write_report(report_path, languages, source, stats, problems):
    lines: list[str] = []
    lines.append("# Diffractor translation consistency report")
    lines.append("")
    lines.append(f"Languages: {', '.join(f'{c} ({language_name(c)})' for c in languages)}")
    lines.append(f"Source strings (union of all msgids): **{len(source)}**")
    lines.append("")

    # Summary table
    lines.append("## Coverage summary")
    lines.append("")
    lines.append("| Code | Language | Source | Translated | Missing | Token issues | Quality | msgstr[2] |")
    lines.append("|------|----------|-------:|-----------:|--------:|-------------:|--------:|----------:|")
    for code, s in stats.items():
        lines.append(
            f"| {code} | {language_name(code)} | {s['total']} | {s['translated']} "
            f"| {s['missing']} | {s['token_issues']} | {s['quality']} | {s['msgstr2']} |"
        )
    lines.append("")

    # Problems
    lines.append(f"## Problems ({len(problems)} strings)")
    lines.append("")
    if not problems:
        lines.append("No missing translations, token mismatches or msgstr[2] entries found.")
    else:
        lines.append(
            "Each entry below has a missing translation, a placeholder/token "
            "mismatch, a structural quality mismatch (whitespace / newline / "
            "punctuation) or a msgstr[2] form in at least one language."
        )
        lines.append("")
        for msgid, row_issues in problems:
            lines.append(format_string_block(msgid, source, languages, row_issues))
            lines.append("")
    lines.append("")

    # Full dump
    lines.append("## All strings")
    lines.append("")
    for msgid in source:
        lines.append(format_string_block(msgid, source, languages))
        lines.append("")

    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text("\n".join(lines), encoding="utf-8")


def print_summary(languages, source, stats, problems, report_path):
    print(f"Parsed {len(languages)} languages from {DEFAULT_LANG_DIR}")
    print(f"Source strings (union of all msgids): {len(source)}")
    print()
    header = (
        f"{'Code':<5} {'Language':<12} {'Source':>7} {'Translated':>11} "
        f"{'Missing':>8} {'Tokens':>7} {'Quality':>8} {'msgstr[2]':>10}"
    )
    print(header)
    print("-" * len(header))
    for code, s in stats.items():
        print(
            f"{code:<5} {language_name(code):<12} {s['total']:>7} {s['translated']:>11} "
            f"{s['missing']:>8} {s['token_issues']:>7} {s['quality']:>8} {s['msgstr2']:>10}"
        )
    print()
    print(f"Strings with at least one problem: {len(problems)}")
    print(f"Full report written to: {report_path}")


def print_full(languages, source):
    for msgid in source:
        print(format_string_block(msgid, source, languages))
        print()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--lang-dir", type=Path, default=DEFAULT_LANG_DIR,
        help="folder containing the .po files (default: exe/languages)",
    )
    parser.add_argument(
        "--report", type=Path, default=DEFAULT_REPORT,
        help="path for the Markdown report (default: tmp/translation_report.md)",
    )
    parser.add_argument(
        "--full", action="store_true",
        help="also print the full per-string comparison to stdout",
    )
    args = parser.parse_args(argv)

    po_paths = sorted(args.lang_dir.glob("*.po"))
    if not po_paths:
        print(f"No .po files found in {args.lang_dir}", file=sys.stderr)
        return 1

    languages = OrderedDict((p.stem, load_po_file(p)) for p in po_paths)
    source, problems = analyse(languages)
    stats = compute_stats(languages, source, problems)

    write_report(args.report, languages, source, stats, problems)
    print_summary(languages, source, stats, problems, args.report)
    if args.full:
        print()
        print_full(languages, source)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
