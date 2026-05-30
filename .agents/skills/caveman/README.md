# Caveman — Token-Efficient Agent Instructions

Compress agent instruction files (`.agents/` markdown) into terse, technically
accurate caveman-speak. Cuts ~60-75% of input tokens while preserving code,
URLs, structure, headings, and all technical substance.

Inspired by [caveman](https://github.com/JuliusBrussee/caveman) but redesigned
for the agent-rules framework:
- **Agentic compression** — uses sub-agents (not direct API calls)
- **Tri-file convention** — `FILE.md` / `FILE.full.md` / `FILE.caveman.md`
- **Lossless switching** — instant toggle between full and compressed modes
- **Drift detection** — knows when full version was updated and needs recompress
- **Reference integrity** — checks cross-file links after compression

## Quick Start

```
/caveman              # Compress default targets at 'full' level
/caveman ultra        # Maximum compression
/caveman normal       # Restore full-prose versions
/caveman status       # Show what's compressed
/caveman refresh      # Recompress files whose full version changed
```

## Levels

| Level | Style | Savings |
|-------|-------|---------|
| **lite** | Drop filler/hedging, keep articles + grammar | ~30-40% |
| **full** | Drop articles, fragments OK, short synonyms | ~60-70% |
| **ultra** | Abbreviate prose words, arrows for causality | ~75-85% |

## File Layout

```
FILE.md              ← active (what agents read)
FILE.full.md         ← original full prose (source of truth)
FILE.caveman.md      ← compressed variant (auto-generated)
.caveman-state.json  ← per-directory tracking (gitignored)
```

## Configuration

In `.agents/config/agent-rules.yaml`:

```yaml
caveman:
  default_level: full
  model: claude-sonnet-4.6
  default_targets:
    - .agents/knowledge/
    - .agents/pitfalls.md
    - .agents/pitfalls-live.md
  explicit_targets:
    - .agents/skills/
  exclude_globs:
    - "**/CHANGELOG.md"
    - "**/LICENSE*"
```

## Python Scripts

The `scripts/` directory provides validation and state management tools:

```bash
# Check file state
python3 -m scripts status .agents/pitfalls.md

# List all managed files
python3 -m scripts list .agents/

# Validate compression
python3 -m scripts validate FILE.full.md FILE.caveman.md

# Check cross-file references
python3 -m scripts refs .agents/

# Detect file type
python3 -m scripts detect some-file.md
```

## How It Works

1. `/caveman` reads target files and their state
2. For each file: backs up as `FILE.full.md`, spawns a compression sub-agent
3. Sub-agent returns compressed text, validated against the original
4. On validation failure: targeted fix sub-agent (up to 2 retries)
5. Compressed text written to `FILE.caveman.md` and copied to `FILE.md`
6. Cross-file references checked for broken anchors

No direct API calls — compression runs through the host agent's sub-agent
infrastructure (configurable model, default `claude-sonnet-4.6`).

## Updating Content While Caveman Is Active

Always edit `FILE.full.md` (the authoritative version), then run `/caveman refresh`
to regenerate the compressed variant. Never edit `FILE.caveman.md` directly.

## Credits

Based on [caveman](https://github.com/JuliusBrussee/caveman) by Julius Brussee.
Adapted for the agent-rules framework with tri-file management, agentic
compression, and reference integrity checking.
