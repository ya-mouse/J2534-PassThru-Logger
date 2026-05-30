# Task Template: Review Code
# Template version: 0.1

Use this template when spawning a reviewer after a worker completes.

---

## Prompt Structure

```
You are reviewing code for j2534-logger.

## Your Instructions
{paste full content of .agents/reviewer.md}

## Task Being Reviewed
- **Task ID:** {task_id}
- **Milestone:** {milestone}
- **Original spec:** {brief summary of what was requested}
- **Specifications implemented:** {list of specs}

## Source Files to Review
{paste ALL source files created by the worker, with file paths}

## Test Files to Review
{paste ALL test files created by the worker, with file paths}

## Dependency Context
{signatures of modules the code imports from}

## Previous Review Notes (if any)
{paste previous reviewer feedback if this is a re-review iteration}
{indicate which iteration this is: "Review iteration 2 of 5, Phase 1"}

## Your Task
Review all files against the checklist in your instructions.
Produce a structured verdict: APPROVED or CHANGES_REQUESTED.

If CHANGES_REQUESTED:
- List all issues with severity (CRITICAL / MAJOR / MINOR)
- Include file path and line range for each issue
- Provide specific fix suggestions
- List any missing test cases

If this is a re-review (iteration > 1):
- Verify previous issues are resolved
- Check that fixes didn't introduce new problems
- Note any regression from previous version
```

---

## Iteration Tracking

Include in the prompt:

```
## Escalation Status
- Phase: {1|2|3} (1=initial model, 2=switched model, 3=fresh worker)
- Iteration: {N} of {max}
- Current worker model: {model}
- Previous models tried: {list}
- Total review cycles so far: {count}
```

This helps the reviewer understand context and adjust thoroughness:
- Early iterations: be thorough, flag everything
- Later iterations: focus on the specific issues that keep failing
