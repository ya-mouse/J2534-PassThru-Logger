#!/usr/bin/env python3
"""File state machine for caveman compression.

Manages the tri-file convention:
  FILE.md          — active file (what agents/tools read)
  FILE.full.md     — authoritative full-prose version (created on first compress)
  FILE.caveman.md  — compressed caveman variant (generated from full)

State transitions:
  PRISTINE → CAVEMAN_ACTIVE: first /caveman run
  CAVEMAN_ACTIVE → FULL_ACTIVE: /caveman normal
  FULL_ACTIVE → CAVEMAN_ACTIVE: /caveman <level>
  CAVEMAN_ACTIVE → CAVEMAN_ACTIVE: /caveman <new-level> (recompress from full)

Versioning:
  .caveman-state.json in each directory tracks per-file state, hashes, levels,
  and timestamps so we can detect drift (full.md edited since last compress)
  and skip no-op recompressions.
"""

import hashlib
import json
import shutil
from datetime import datetime, timezone
from enum import Enum
from pathlib import Path
from typing import Optional


class FileState(Enum):
    PRISTINE = "pristine"            # Only FILE.md exists, never touched
    CAVEMAN_ACTIVE = "caveman_active"  # FILE.md = copy of FILE.caveman.md
    FULL_ACTIVE = "full_active"      # FILE.md = copy of FILE.full.md
    BROKEN = "broken"                # Inconsistent state


STATE_FILE = ".caveman-state.json"


def _sha256(path: Path) -> str:
    """Compute SHA-256 hex digest of a file."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def _load_state(directory: Path) -> dict:
    """Load .caveman-state.json from a directory."""
    state_path = directory / STATE_FILE
    if state_path.exists():
        try:
            return json.loads(state_path.read_text())
        except (json.JSONDecodeError, OSError):
            return {"version": 1, "files": {}}
    return {"version": 1, "files": {}}


def _save_state(directory: Path, state: dict):
    """Write .caveman-state.json to a directory."""
    state_path = directory / STATE_FILE
    state_path.write_text(json.dumps(state, indent=2) + "\n")


def detect_file_state(filepath: Path) -> FileState:
    """Detect the caveman state of a markdown file.

    Args:
        filepath: Path to the canonical FILE.md

    Returns:
        FileState enum value
    """
    filepath = filepath.resolve()
    stem = filepath.stem
    parent = filepath.parent
    suffix = filepath.suffix

    # Build sibling paths: stem.full.md, stem.caveman.md
    full_path = parent / f"{stem}.full{suffix}"
    cave_path = parent / f"{stem}.caveman{suffix}"

    has_canonical = filepath.exists()
    has_full = full_path.exists()
    has_cave = cave_path.exists()

    if not has_canonical and not has_full and not has_cave:
        return FileState.BROKEN  # nothing exists

    if has_canonical and not has_full and not has_cave:
        return FileState.PRISTINE

    if not has_canonical and has_full and not has_cave:
        return FileState.BROKEN  # half-state from crash

    if has_full and has_cave and has_canonical:
        # Determine which variant is active by comparing hashes
        canon_hash = _sha256(filepath)
        cave_hash = _sha256(cave_path)
        full_hash = _sha256(full_path)

        if canon_hash == cave_hash:
            return FileState.CAVEMAN_ACTIVE
        elif canon_hash == full_hash:
            return FileState.FULL_ACTIVE
        else:
            # FILE.md was edited directly — check state.json for hint
            state = _load_state(parent)
            entry = state.get("files", {}).get(filepath.name, {})
            mode = entry.get("mode", "")
            if mode == "caveman_active":
                return FileState.CAVEMAN_ACTIVE
            elif mode == "full_active":
                return FileState.FULL_ACTIVE
            # Unknown — treat as caveman active if both variants exist
            return FileState.CAVEMAN_ACTIVE

    if has_full and not has_cave:
        # Full backup exists but no caveman variant — was probably restored
        return FileState.FULL_ACTIVE

    return FileState.BROKEN


def sibling_paths(filepath: Path):
    """Return (full_path, caveman_path) for a canonical FILE.md."""
    filepath = filepath.resolve()
    stem = filepath.stem
    parent = filepath.parent
    suffix = filepath.suffix
    full_path = parent / f"{stem}.full{suffix}"
    cave_path = parent / f"{stem}.caveman{suffix}"
    return full_path, cave_path


def prepare_for_compress(filepath: Path) -> Path:
    """Ensure FILE.full.md exists and return its path.

    If this is a pristine file, copies FILE.md → FILE.full.md.
    If full.md already exists, returns it as-is (compression reads from full).

    Returns:
        Path to FILE.full.md (the source for compression)
    """
    filepath = filepath.resolve()
    full_path, _ = sibling_paths(filepath)
    state = detect_file_state(filepath)

    if state == FileState.PRISTINE:
        shutil.copy2(filepath, full_path)
    elif state == FileState.BROKEN and not full_path.exists() and filepath.exists():
        # Recover: treat canonical as the full version
        shutil.copy2(filepath, full_path)

    if not full_path.exists():
        raise FileNotFoundError(f"Cannot find full version for {filepath}")

    return full_path


def activate_caveman(filepath: Path, compressed_text: str, level: str):
    """Write compressed text as FILE.caveman.md and copy to FILE.md.

    Args:
        filepath: Canonical FILE.md path
        compressed_text: The caveman-compressed content
        level: Compression level used (lite/full/ultra/etc.)
    """
    filepath = filepath.resolve()
    full_path, cave_path = sibling_paths(filepath)

    # Write caveman variant
    cave_path.write_text(compressed_text)

    # Copy to canonical
    shutil.copy2(cave_path, filepath)

    # Update state
    state = _load_state(filepath.parent)
    state.setdefault("files", {})[filepath.name] = {
        "mode": "caveman_active",
        "level": level,
        "full_hash": _sha256(full_path),
        "caveman_hash": _sha256(cave_path),
        "compressed_at": _now_iso(),
    }
    _save_state(filepath.parent, state)


def activate_full(filepath: Path):
    """Restore FILE.full.md as the canonical FILE.md.

    Args:
        filepath: Canonical FILE.md path
    """
    filepath = filepath.resolve()
    full_path, cave_path = sibling_paths(filepath)

    if not full_path.exists():
        raise FileNotFoundError(f"No full version found: {full_path}")

    shutil.copy2(full_path, filepath)

    # Update state
    state = _load_state(filepath.parent)
    entry = state.get("files", {}).get(filepath.name, {})
    entry["mode"] = "full_active"
    entry["restored_at"] = _now_iso()
    state.setdefault("files", {})[filepath.name] = entry
    _save_state(filepath.parent, state)


def needs_recompress(filepath: Path) -> bool:
    """Check if FILE.full.md has been edited since last compression.

    Returns True if:
    - full.md hash differs from recorded hash in state.json
    - No state.json entry exists (first time or lost)
    """
    filepath = filepath.resolve()
    full_path, _ = sibling_paths(filepath)

    if not full_path.exists():
        return False

    state = _load_state(filepath.parent)
    entry = state.get("files", {}).get(filepath.name, {})
    recorded_hash = entry.get("full_hash", "")

    if not recorded_hash:
        return True

    return _sha256(full_path) != recorded_hash


def is_same_compression(filepath: Path) -> bool:
    """Check if the current canonical matches the caveman variant (no-op check)."""
    filepath = filepath.resolve()
    _, cave_path = sibling_paths(filepath)

    if not filepath.exists() or not cave_path.exists():
        return False

    return _sha256(filepath) == _sha256(cave_path)


def list_managed_files(directory: Path, recursive: bool = True) -> list:
    """List all managed files in a directory with their states.

    Returns list of dicts with keys: path, state, level, full_hash_match
    """
    results = []
    pattern = "**/*.md" if recursive else "*.md"

    for md_file in sorted(directory.glob(pattern)):
        # Skip variant files
        if md_file.name.endswith(".full.md") or md_file.name.endswith(".caveman.md"):
            continue
        if md_file.name == STATE_FILE:
            continue

        state = detect_file_state(md_file)
        full_path, cave_path = sibling_paths(md_file)

        entry = {
            "path": str(md_file),
            "state": state.value,
            "has_full": full_path.exists(),
            "has_caveman": cave_path.exists(),
        }

        # Add level and drift info from state.json
        dir_state = _load_state(md_file.parent)
        file_entry = dir_state.get("files", {}).get(md_file.name, {})
        entry["level"] = file_entry.get("level", "")
        entry["needs_recompress"] = needs_recompress(md_file) if state != FileState.PRISTINE else False

        results.append(entry)

    return results


def cleanup_state(filepath: Path):
    """Remove variant files and state entry, restoring to pristine.

    Copies FILE.full.md → FILE.md if it exists, then removes .full.md and .caveman.md.
    """
    filepath = filepath.resolve()
    full_path, cave_path = sibling_paths(filepath)

    # Restore from full if available
    if full_path.exists():
        shutil.copy2(full_path, filepath)
        full_path.unlink()

    if cave_path.exists():
        cave_path.unlink()

    # Remove from state.json
    state = _load_state(filepath.parent)
    state.get("files", {}).pop(filepath.name, None)
    if state.get("files"):
        _save_state(filepath.parent, state)
    else:
        state_path = filepath.parent / STATE_FILE
        if state_path.exists():
            state_path.unlink()


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("Usage: python state.py <status|list> [path]")
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == "status" and len(sys.argv) >= 3:
        p = Path(sys.argv[2]).resolve()
        st = detect_file_state(p)
        full, cave = sibling_paths(p)
        print(f"File:     {p}")
        print(f"State:    {st.value}")
        print(f"Full:     {full} ({'exists' if full.exists() else 'missing'})")
        print(f"Caveman:  {cave} ({'exists' if cave.exists() else 'missing'})")
        if st != FileState.PRISTINE:
            print(f"Drift:    {'yes' if needs_recompress(p) else 'no'}")

    elif cmd == "list":
        directory = Path(sys.argv[2]).resolve() if len(sys.argv) >= 3 else Path.cwd()
        files = list_managed_files(directory)
        for f in files:
            drift_marker = " [DRIFT]" if f.get("needs_recompress") else ""
            level = f" ({f['level']})" if f.get("level") else ""
            print(f"  {f['state']:18s} {f['path']}{level}{drift_marker}")

    else:
        print("Usage: python state.py <status|list> [path]")
        sys.exit(1)
