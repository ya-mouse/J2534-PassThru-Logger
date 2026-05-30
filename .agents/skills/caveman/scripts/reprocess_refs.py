#!/usr/bin/env python3
"""Reprocess cross-file markdown references after caveman compression.

Handles:
1. Heading anchor links — detects broken #slug references when headings change
2. Cross-file links — ensures [text](other.md) still resolves
3. Reports broken references for manual/agent review

This is a POST-COMPRESSION step. Run after all files are compressed.
Does not modify files — only reports issues (the agent decides what to fix).
"""

import re
from pathlib import Path
from typing import Dict, List, Set, Tuple

HEADING_REGEX = re.compile(r"^(#{1,6})\s+(.*)", re.MULTILINE)
# Markdown link with optional anchor: [text](path#anchor) or [text](path)
MD_LINK_REGEX = re.compile(r"\[([^\]]*)\]\(([^)]+)\)")
# HTML anchor-style ID
HTML_ID_REGEX = re.compile(r'id=["\']([^"\']+)["\']')


def heading_to_slug(heading: str) -> str:
    """Convert a markdown heading to a GitHub-style anchor slug.

    Rules: lowercase, replace spaces with hyphens, strip non-alphanumeric
    except hyphens and underscores.
    """
    slug = heading.strip().lower()
    slug = re.sub(r"[^\w\s\-]", "", slug)
    slug = re.sub(r"\s+", "-", slug)
    slug = re.sub(r"-+", "-", slug)
    return slug.strip("-")


def extract_heading_slugs(text: str) -> Set[str]:
    """Extract all heading anchor slugs from markdown text."""
    slugs = set()
    for _, title in HEADING_REGEX.findall(text):
        slugs.add(heading_to_slug(title))
    # Also extract HTML IDs
    for html_id in HTML_ID_REGEX.findall(text):
        slugs.add(html_id)
    return slugs


def extract_links(text: str) -> List[Tuple[str, str, str]]:
    """Extract markdown links as (display_text, path, anchor_or_empty).

    Returns list of (text, file_path, anchor) tuples.
    """
    links = []
    for display, target in MD_LINK_REGEX.findall(text):
        # Skip external URLs and data URIs
        if target.startswith(("http://", "https://", "mailto:", "data:", "#")):
            if target.startswith("#"):
                links.append((display, "", target[1:]))
            continue
        if "#" in target:
            path, anchor = target.split("#", 1)
            links.append((display, path, anchor))
        else:
            links.append((display, target, ""))
    return links


class RefReport:
    """Report of reference issues found after compression."""

    def __init__(self):
        self.broken_anchors: List[dict] = []    # anchor links that no longer resolve
        self.broken_files: List[dict] = []       # file links that don't exist
        self.heading_changes: List[dict] = []    # headings that changed slug

    @property
    def has_issues(self) -> bool:
        return bool(self.broken_anchors or self.broken_files or self.heading_changes)

    def summary(self) -> str:
        lines = []
        if self.broken_anchors:
            lines.append(f"Broken anchors: {len(self.broken_anchors)}")
            for ba in self.broken_anchors:
                lines.append(f"  {ba['source']} → #{ba['anchor']} (target: {ba.get('target_file', 'self')})")
        if self.broken_files:
            lines.append(f"Missing file targets: {len(self.broken_files)}")
            for bf in self.broken_files:
                lines.append(f"  {bf['source']} → {bf['path']}")
        if self.heading_changes:
            lines.append(f"Changed heading slugs: {len(self.heading_changes)}")
            for hc in self.heading_changes:
                lines.append(f"  {hc['file']}: {hc['old_slug']} → {hc['new_slug']}")
        return "\n".join(lines) if lines else "No reference issues found."


def check_heading_drift(full_path: Path, caveman_path: Path) -> List[dict]:
    """Compare heading slugs between full and caveman versions.

    Returns list of changed headings with old/new slugs.
    """
    if not full_path.exists() or not caveman_path.exists():
        return []

    full_text = full_path.read_text(errors="ignore")
    cave_text = caveman_path.read_text(errors="ignore")

    full_headings = HEADING_REGEX.findall(full_text)
    cave_headings = HEADING_REGEX.findall(cave_text)

    changes = []
    # Match by position (headings should be in same order)
    for i, (level, title) in enumerate(full_headings):
        old_slug = heading_to_slug(title)
        if i < len(cave_headings):
            new_slug = heading_to_slug(cave_headings[i][1])
            if old_slug != new_slug:
                changes.append({
                    "file": str(caveman_path),
                    "heading_index": i,
                    "old_title": title.strip(),
                    "new_title": cave_headings[i][1].strip(),
                    "old_slug": old_slug,
                    "new_slug": new_slug,
                })
    return changes


def check_references(directory: Path, recursive: bool = True) -> RefReport:
    """Scan all active .md files in a directory for broken references.

    Checks both internal (#anchor) and cross-file (other.md#anchor) links
    against the current active files.

    Args:
        directory: Root directory to scan
        recursive: Whether to recurse into subdirectories
    """
    report = RefReport()
    pattern = "**/*.md" if recursive else "*.md"

    # Build slug index: filename → set of slugs
    slug_index: Dict[str, Set[str]] = {}
    md_files = []

    for md_file in sorted(directory.glob(pattern)):
        if md_file.name.endswith((".full.md", ".caveman.md")):
            continue
        if md_file.name == ".caveman-state.json":
            continue
        md_files.append(md_file)
        text = md_file.read_text(errors="ignore")
        # Index by both absolute and relative-to-directory path
        rel = str(md_file.relative_to(directory))
        slug_index[rel] = extract_heading_slugs(text)
        slug_index[md_file.name] = extract_heading_slugs(text)

    # Check all links
    for md_file in md_files:
        text = md_file.read_text(errors="ignore")
        links = extract_links(text)
        rel_dir = md_file.parent

        for display, link_path, anchor in links:
            if not link_path and anchor:
                # Self-referencing anchor
                self_slugs = slug_index.get(md_file.name, set())
                if anchor not in self_slugs:
                    report.broken_anchors.append({
                        "source": str(md_file),
                        "anchor": anchor,
                        "target_file": "self",
                        "display_text": display,
                    })
            elif link_path:
                # Cross-file link
                target = (rel_dir / link_path).resolve()
                if not target.exists():
                    # Try without the anchor component
                    if not link_path.endswith(".md"):
                        continue  # Skip non-md targets
                    report.broken_files.append({
                        "source": str(md_file),
                        "path": link_path,
                        "display_text": display,
                    })
                elif anchor:
                    # File exists, check anchor
                    target_key = target.name
                    target_slugs = slug_index.get(target_key, set())
                    if anchor not in target_slugs:
                        report.broken_anchors.append({
                            "source": str(md_file),
                            "anchor": anchor,
                            "target_file": link_path,
                            "display_text": display,
                        })

    # Check heading drift for caveman-managed files
    from .state import sibling_paths, detect_file_state, FileState

    for md_file in md_files:
        state = detect_file_state(md_file)
        if state in (FileState.CAVEMAN_ACTIVE, FileState.FULL_ACTIVE):
            full_path, cave_path = sibling_paths(md_file)
            changes = check_heading_drift(full_path, cave_path)
            report.heading_changes.extend(changes)

    return report


if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("Usage: python reprocess_refs.py <directory> [--recursive]")
        sys.exit(1)

    directory = Path(sys.argv[1]).resolve()
    recursive = "--recursive" in sys.argv or "-r" in sys.argv

    report = check_references(directory, recursive=recursive)
    print(report.summary())
    sys.exit(1 if report.has_issues else 0)
