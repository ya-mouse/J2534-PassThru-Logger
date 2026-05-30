# Task Template: Implement Module/Phase
# Template version: 0.1

Use this template when spawning a worker to implement an entire module or
tightly-coupled set of files (e.g., a parser module, an API layer, a data pipeline stage).

---

## Prompt Structure

```
You are implementing a complete module for j2534-logger.

## Your Instructions
{paste full content of .agents/implement.md}

## Domain Knowledge
{paste relevant sections from .agents/skills/j2534-logger/SKILL.md}

## Pitfalls to Avoid
{paste relevant sections from .agents/pitfalls.md}
{paste ALL entries from .agents/pitfalls-live.md}

## Task
- **Task ID:** {task_id}
- **Milestone:** {milestone}
- **Module:** {module name, e.g., "parser", "auth", "data-pipeline"}
- **Specifications:** {list of ALL specs this module implements}
- **Files to create:**
  {list of ALL files in this module with paths and estimated LOC}

## Architecture
{describe how the files in this module interact}
{which file is the entry point}
{data flow: what comes in, what goes out}

## Specifications
{paste ALL relevant specification content}

## Dependencies
{paste public signatures from modules this code imports}

## Reference Code
{paste existing module source for context}

## Expected Output
Create ALL files for this module. Ensure:
1. Each file ≤ 500 LOC (700 max with justification)
2. Clear module boundaries between files
3. Entry point file exports the public API
4. Helper files are imported by the entry point
5. Separate test files for each source file under /

Write all files. Maintain consistency across the module.
```

---

## When to Use This Template

- Files within a module are tightly coupled and share internal types
- Multiple files need to be consistent with each other
- The module has a clear entry point and internal structure

## When NOT to Use

- Independent files across different modules (use implement-file.md)
- Single complex file (use implement-file.md)
- Utility/library code that's independently usable (use implement-file.md)
