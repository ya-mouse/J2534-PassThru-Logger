#!/usr/bin/env python3
"""Validate caveman-compressed files preserve structural elements.

Ported from github.com/JuliusBrussee/caveman with additions:
- Frontmatter preservation check (YAML --- blocks)
- Line-number reference warning (FILE.py:NN patterns)
"""

import re
from collections import Counter
from pathlib import Path
from typing import List

URL_REGEX = re.compile(r"https?://[^\s)]+")
FENCE_OPEN_REGEX = re.compile(r"^(\s{0,3})(`{3,}|~{3,})(.*)$")
HEADING_REGEX = re.compile(r"^(#{1,6})\s+(.*)", re.MULTILINE)
BULLET_REGEX = re.compile(r"^\s*[-*+]\s+", re.MULTILINE)
PATH_REGEX = re.compile(
    r"(?:\./|\.\./|/|[A-Za-z]:\\)[\w\-/\\\.]+|[\w\-\.]+[/\\][\w\-/\\\.]+",
)
FRONTMATTER_REGEX = re.compile(r"\A---\s*\n(.*?)\n---", re.DOTALL)
LINE_REF_REGEX = re.compile(r"\b[\w\-]+\.\w+:\d+\b")


class ValidationResult:
    def __init__(self):
        self.is_valid = True
        self.errors: List[str] = []
        self.warnings: List[str] = []

    def add_error(self, msg: str):
        self.is_valid = False
        self.errors.append(msg)

    def add_warning(self, msg: str):
        self.warnings.append(msg)


def read_file(path: Path) -> str:
    return path.read_text(errors="ignore")


# ---------- Extractors ----------

def extract_headings(text: str):
    return [(level, title.strip()) for level, title in HEADING_REGEX.findall(text)]


def extract_code_blocks(text: str):
    """Line-based fenced code block extractor with nested fence support."""
    blocks = []
    lines = text.split("\n")
    i = 0
    n = len(lines)
    while i < n:
        m = FENCE_OPEN_REGEX.match(lines[i])
        if not m:
            i += 1
            continue
        fence_char = m.group(2)[0]
        fence_len = len(m.group(2))
        block_lines = [lines[i]]
        i += 1
        closed = False
        while i < n:
            close_m = FENCE_OPEN_REGEX.match(lines[i])
            if (
                close_m
                and close_m.group(2)[0] == fence_char
                and len(close_m.group(2)) >= fence_len
                and close_m.group(3).strip() == ""
            ):
                block_lines.append(lines[i])
                closed = True
                i += 1
                break
            block_lines.append(lines[i])
            i += 1
        if closed:
            blocks.append("\n".join(block_lines))
    return blocks


def extract_urls(text: str):
    return set(URL_REGEX.findall(text))


def extract_paths(text: str):
    return set(PATH_REGEX.findall(text))


def extract_inline_codes(text: str):
    text_without_fences = re.sub(r"^```[\s\S]*?^```", "", text, flags=re.MULTILINE)
    text_without_fences = re.sub(r"^~~~[\s\S]*?^~~~", "", text_without_fences, flags=re.MULTILINE)
    return re.findall(r"`([^`]+)`", text_without_fences)


def extract_frontmatter(text: str):
    m = FRONTMATTER_REGEX.match(text)
    return m.group(0) if m else None


def count_bullets(text: str):
    return len(BULLET_REGEX.findall(text))


# ---------- Validators ----------

def validate_headings(orig: str, comp: str, result: ValidationResult):
    h1 = extract_headings(orig)
    h2 = extract_headings(comp)
    if len(h1) != len(h2):
        result.add_error(f"Heading count mismatch: {len(h1)} vs {len(h2)}")
    if h1 != h2:
        result.add_warning("Heading text/order changed")


def validate_code_blocks(orig: str, comp: str, result: ValidationResult):
    c1 = extract_code_blocks(orig)
    c2 = extract_code_blocks(comp)
    if c1 != c2:
        result.add_error("Code blocks not preserved exactly")


def validate_urls(orig: str, comp: str, result: ValidationResult):
    u1 = extract_urls(orig)
    u2 = extract_urls(comp)
    if u1 != u2:
        result.add_error(f"URL mismatch: lost={u1 - u2}, added={u2 - u1}")


def validate_paths(orig: str, comp: str, result: ValidationResult):
    p1 = extract_paths(orig)
    p2 = extract_paths(comp)
    if p1 != p2:
        result.add_warning(f"Path mismatch: lost={p1 - p2}, added={p2 - p1}")


def validate_bullets(orig: str, comp: str, result: ValidationResult):
    b1 = count_bullets(orig)
    b2 = count_bullets(comp)
    if b1 == 0:
        return
    diff = abs(b1 - b2) / b1
    if diff > 0.15:
        result.add_warning(f"Bullet count changed too much: {b1} -> {b2}")


def validate_inline_codes(orig: str, comp: str, result: ValidationResult):
    c1 = Counter(extract_inline_codes(orig))
    c2 = Counter(extract_inline_codes(comp))
    if c1 != c2:
        lost = set(c1.keys()) - set(c2.keys())
        added = set(c2.keys()) - set(c1.keys())
        for code, count in c1.items():
            if code in c2 and c2[code] < count:
                lost.add(f"{code} (lost {count - c2[code]} of {count} occurrences)")
        if lost:
            result.add_error(f"Inline code lost: {lost}")
        if added:
            result.add_warning(f"Inline code added: {added}")


def validate_frontmatter(orig: str, comp: str, result: ValidationResult):
    """Frontmatter (YAML --- block) must be preserved exactly."""
    fm_orig = extract_frontmatter(orig)
    fm_comp = extract_frontmatter(comp)
    if fm_orig and not fm_comp:
        result.add_error("Frontmatter lost during compression")
    elif fm_orig and fm_comp and fm_orig != fm_comp:
        result.add_error("Frontmatter modified during compression (must be preserved exactly)")


def validate_line_refs(orig: str, comp: str, result: ValidationResult):
    """Warn if file:line references changed (compression shifts line numbers)."""
    refs_orig = set(LINE_REF_REGEX.findall(orig))
    refs_comp = set(LINE_REF_REGEX.findall(comp))
    lost = refs_orig - refs_comp
    if lost:
        result.add_warning(f"Line-number references changed/lost: {lost}")


# ---------- Main ----------

def validate(original_path: Path, compressed_path: Path) -> ValidationResult:
    result = ValidationResult()
    orig = read_file(original_path)
    comp = read_file(compressed_path)

    validate_frontmatter(orig, comp, result)
    validate_headings(orig, comp, result)
    validate_code_blocks(orig, comp, result)
    validate_urls(orig, comp, result)
    validate_paths(orig, comp, result)
    validate_bullets(orig, comp, result)
    validate_inline_codes(orig, comp, result)
    validate_line_refs(orig, comp, result)

    return result


if __name__ == "__main__":
    import sys
    if len(sys.argv) != 3:
        print("Usage: python validate.py <original> <compressed>")
        sys.exit(1)
    orig = Path(sys.argv[1]).resolve()
    comp = Path(sys.argv[2]).resolve()
    res = validate(orig, comp)
    print(f"\nValid: {res.is_valid}")
    if res.errors:
        print("\nErrors:")
        for e in res.errors:
            print(f"  - {e}")
    if res.warnings:
        print("\nWarnings:")
        for w in res.warnings:
            print(f"  - {w}")
