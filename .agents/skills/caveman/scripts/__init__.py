"""Caveman compression scripts for agent-rules framework.

Provides tools to compress natural language markdown files into caveman format,
validate compression fidelity, manage FILE.md / FILE.full.md / FILE.caveman.md
state, and maintain cross-file references.
"""

__all__ = ["state", "detect", "validate", "reprocess_refs"]
__version__ = "0.1.0"
