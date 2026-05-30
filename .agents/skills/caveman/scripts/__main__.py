#!/usr/bin/env python3
"""Caveman scripts CLI.

Usage:
    python -m scripts status <file>       — Show state of a file
    python -m scripts list [directory]     — List all managed files
    python -m scripts validate <orig> <compressed> — Validate compression
    python -m scripts refs <directory>     — Check cross-file references
    python -m scripts detect <file>...     — Detect file type
"""

import sys
from pathlib import Path

# Force UTF-8 output
for _stream in (sys.stdout, sys.stderr):
    reconfigure = getattr(_stream, "reconfigure", None)
    if callable(reconfigure):
        try:
            reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass


def print_usage():
    print(__doc__)


def cmd_status(args):
    from .state import detect_file_state, sibling_paths, needs_recompress, FileState
    if not args:
        print("Usage: python -m scripts status <file>")
        return 1
    p = Path(args[0]).resolve()
    st = detect_file_state(p)
    full, cave = sibling_paths(p)
    print(f"File:     {p}")
    print(f"State:    {st.value}")
    print(f"Full:     {full} ({'exists' if full.exists() else 'missing'})")
    print(f"Caveman:  {cave} ({'exists' if cave.exists() else 'missing'})")
    if st != FileState.PRISTINE:
        print(f"Drift:    {'yes' if needs_recompress(p) else 'no'}")
    return 0


def cmd_list(args):
    from .state import list_managed_files
    directory = Path(args[0]).resolve() if args else Path.cwd()
    files = list_managed_files(directory)
    if not files:
        print("No markdown files found.")
        return 0
    for f in files:
        drift = " [DRIFT]" if f.get("needs_recompress") else ""
        level = f" ({f['level']})" if f.get("level") else ""
        print(f"  {f['state']:18s} {f['path']}{level}{drift}")
    return 0


def cmd_validate(args):
    from .validate import validate
    if len(args) != 2:
        print("Usage: python -m scripts validate <original> <compressed>")
        return 1
    orig = Path(args[0]).resolve()
    comp = Path(args[1]).resolve()
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
    return 0 if res.is_valid else 1


def cmd_refs(args):
    from .reprocess_refs import check_references
    directory = Path(args[0]).resolve() if args else Path.cwd()
    report = check_references(directory, recursive=True)
    print(report.summary())
    return 1 if report.has_issues else 0


def cmd_detect(args):
    from .detect import detect_file_type, should_compress
    if not args:
        print("Usage: python -m scripts detect <file>...")
        return 1
    for path_str in args:
        p = Path(path_str).resolve()
        file_type = detect_file_type(p)
        compress = should_compress(p)
        print(f"  {p.name:30s} type={file_type:20s} compress={compress}")
    return 0


def main():
    if len(sys.argv) < 2:
        print_usage()
        sys.exit(1)

    cmd = sys.argv[1]
    args = sys.argv[2:]

    commands = {
        "status": cmd_status,
        "list": cmd_list,
        "validate": cmd_validate,
        "refs": cmd_refs,
        "detect": cmd_detect,
    }

    if cmd in commands:
        sys.exit(commands[cmd](args))
    else:
        print(f"Unknown command: {cmd}")
        print_usage()
        sys.exit(1)


if __name__ == "__main__":
    main()
