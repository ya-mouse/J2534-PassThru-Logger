# Task Template: Implement File(s)
# Template version: 0.1

Use this template when spawning a worker to implement one or more source files.

---

## Prompt Structure

```
You are implementing part of j2534-logger.

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
- **Specification:** {spec reference or description}
- **Source files to create:**
  {list of files with expected paths}
- **Test files to create:**
  {list of test file paths}

## Specifications
{paste the relevant specification content}

## Dependencies (interfaces you can import)
{paste public signatures from dependency modules}

## Reference Code
{paste existing source file(s) for context}
This code is reference only. Improve upon it — don't copy.
Key improvements to make:
- {specific improvements for this task}

## Expected Output
Create each file listed above. For each source file:
1. Implementation following the specification
2. Clear error handling with diagnostics
3. All public functions documented with their purpose

For each test file:
1. Happy-path tests
2. Edge case tests (empty input, max values, deeply nested)
3. Error path tests (invalid input, type errors)

Write all files. Do not ask questions — implement based on the spec.
If something is genuinely ambiguous, document your choice with a comment.
```

---

## Variable Substitution Guide

| Variable | Source |
|----------|--------|
| `{task_id}` | From task list / EXECUTION_PLAN |
| `{milestone}` | Current milestone |
| `{spec reference}` | From task description or design docs |
| `{file paths}` | From source tree plan |
| `{test file paths}` | Mirror source paths under / |
| `{specification content}` | From design/spec documents |
| `{dependency signatures}` | From INTERFACES.md or source files |
| `{existing source}` | From ./ |
