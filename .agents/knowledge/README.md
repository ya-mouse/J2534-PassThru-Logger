# Agent Knowledge Base

Cross-cutting procedural knowledge shared across all domain skills.
Module-specific knowledge lives in per-module `AGENTS.md` files.

> **Base protocols** (pre-task knowledge search, environment pre-flight,
> review, post-task evolution) are defined in `.agents/skill-base.md`.
> All domain skills inherit these protocols. This knowledge base is one of
> the sources consulted in the pre-task search.

## Structure

```
knowledge/
├── patterns/           # Code patterns and how-tos
└── workflows/          # Build, test, and operational procedures
```

## Knowledge Evolution Protocol

Agents update this knowledge base as part of the post-commit workflow.
See the domain skill's "Knowledge Evolution Protocol" section for the
full pre-task / post-commit flow.

### What goes where

| Knowledge type | Location | Example |
|---------------|----------|---------|
| Module architecture, APIs, internal patterns | Per-module `AGENTS.md` | Component FSM states |
| Cross-cutting code patterns | `.agents/knowledge/patterns/` | RPC handler boilerplate |
| Build/test/operational procedures | `.agents/knowledge/workflows/` | How to flash firmware |
| Bug classes, gotchas, workarounds | `.agents/pitfalls-live.md` | Cache invalidation gotcha |

### Article format

Each knowledge article follows this structure:

```markdown
# <Title>

## When to Use
Brief description of when this pattern/workflow applies.

## Pattern / Procedure
The actual content (code examples, step-by-step instructions).

## Pitfalls
Known gotchas specific to this pattern.

## References
Links to source files, related articles, external docs.
```

### Adding new articles

1. Check if an existing article covers the topic — update instead of creating
2. Place in the correct subfolder (patterns/ or workflows/)
3. Use kebab-case filenames: `my-new-pattern.md`
4. Update this README's structure section
5. Commit separately: `knowledge: add <topic>`
