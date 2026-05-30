#!/usr/bin/env python3
"""Detect whether a file is natural language (compressible) or code/config (skip).

Ported from github.com/JuliusBrussee/caveman with modifications:
- Backup convention uses .full.md / .caveman.md (not .original.md)
- Added YAML frontmatter detection for SKILL.md files
"""

import json
import re
from pathlib import Path

# Extensions that are natural language and compressible
COMPRESSIBLE_EXTENSIONS = {".md", ".txt", ".markdown", ".rst", ".typ", ".typst", ".tex"}

# Extensions that are code/config and should be skipped
SKIP_EXTENSIONS = {
    ".py", ".js", ".ts", ".tsx", ".jsx", ".json", ".yaml", ".yml",
    ".toml", ".env", ".lock", ".css", ".scss", ".html", ".xml",
    ".sql", ".sh", ".bash", ".zsh", ".go", ".rs", ".java", ".c",
    ".cpp", ".h", ".hpp", ".rb", ".php", ".swift", ".kt", ".lua",
    ".dockerfile", ".makefile", ".csv", ".ini", ".cfg",
}

# Patterns that indicate a line is code
CODE_PATTERNS = [
    re.compile(r"^\s*(import |from .+ import |require\(|const |let |var )"),
    re.compile(r"^\s*(def |class |function |async function |export )"),
    re.compile(r"^\s*(if\s*\(|for\s*\(|while\s*\(|switch\s*\(|try\s*\{)"),
    re.compile(r"^\s*[\}\]\);]+\s*$"),
    re.compile(r"^\s*@\w+"),
    re.compile(r'^\s*"[^"]+"\s*:\s*'),
    re.compile(r"^\s*\w+\s*=\s*[{\[\(\"']"),
]


def _is_code_line(line: str) -> bool:
    return any(p.match(line) for p in CODE_PATTERNS)


def _is_json_content(text: str) -> bool:
    try:
        json.loads(text)
        return True
    except (json.JSONDecodeError, ValueError):
        return False


def _is_yaml_content(lines: list) -> bool:
    yaml_indicators = 0
    for line in lines[:30]:
        stripped = line.strip()
        if stripped.startswith("---"):
            yaml_indicators += 1
        elif re.match(r"^\w[\w\s]*:\s", stripped):
            yaml_indicators += 1
        elif stripped.startswith("- ") and ":" in stripped:
            yaml_indicators += 1
    non_empty = sum(1 for ln in lines[:30] if ln.strip())
    return non_empty > 0 and yaml_indicators / non_empty > 0.6


def detect_file_type(filepath: Path) -> str:
    """Classify a file as 'natural_language', 'code', 'config', or 'unknown'."""
    ext = filepath.suffix.lower()

    if ext in COMPRESSIBLE_EXTENSIONS:
        return "natural_language"
    if ext in SKIP_EXTENSIONS:
        config_exts = {".json", ".yaml", ".yml", ".toml", ".ini", ".cfg", ".env"}
        return "config" if ext in config_exts else "code"

    # Extensionless files — check content
    if not ext:
        try:
            text = filepath.read_text(errors="ignore")
        except (OSError, PermissionError):
            return "unknown"

        lines = text.splitlines()[:50]

        if _is_json_content(text[:10000]):
            return "config"
        if _is_yaml_content(lines):
            return "config"

        code_lines = sum(1 for ln in lines if ln.strip() and _is_code_line(ln))
        non_empty = sum(1 for ln in lines if ln.strip())
        if non_empty > 0 and code_lines / non_empty > 0.4:
            return "code"

        return "natural_language"

    return "unknown"


def should_compress(filepath: Path) -> bool:
    """Return True if the file is natural language and should be compressed."""
    if not filepath.is_file():
        return False
    name = filepath.name
    # Skip variant files — only compress the canonical FILE.md
    if name.endswith(".full.md") or name.endswith(".caveman.md"):
        return False
    # Skip upstream caveman backup convention too
    if name.endswith(".original.md"):
        return False
    return detect_file_type(filepath) == "natural_language"


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        print("Usage: python detect.py <file1> [file2] ...")
        sys.exit(1)
    for path_str in sys.argv[1:]:
        p = Path(path_str).resolve()
        file_type = detect_file_type(p)
        compress = should_compress(p)
        print(f"  {p.name:30s} type={file_type:20s} compress={compress}")
