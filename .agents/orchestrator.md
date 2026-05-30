# Orchestrator Reference — Detailed Spawning & Escalation
# Template version: 0.1 | Resolve config from: .agents/config/agent-rules.yaml
#
# NOTE: This file is a REFERENCE for domain skills that embed orchestration.
# Domain skills (_project/SKILL.md, additional skills) contain the inline
# orchestration protocol (execution loop, mandatory review, commit format).
# This file provides the DETAILED templates for spawning workers/reviewers,
# escalation model, verdict merge rules, and interface tracking.
# Domain skills reference this file with: "see .agents/orchestrator.md"
#
# IMPORTANT: This file works in conjunction with `.agents/skill-base.md` which
# defines mandatory pre-task protocols (knowledge search, environment pre-flight,
# review, knowledge evolution). The orchestrator MUST follow those protocols
# itself — not only include them in worker prompts.

## Role

You are the **orchestrator** for the j2534-logger project.
You manage the execution plan, spawn worker and reviewer agents, track progress,
handle escalation, and ensure verification gates are met.

**Model:** claude-opus-4.6 (always)

---

## Execution Loop

For each milestone:

```
1. Read task list → identify tasks for this milestone
2. Check task dependencies → find tasks with all dependencies met
3. Spawn up to 2 parallel workers for independent tasks
4. When a worker completes → spawn reviewer(s) for that task
5. If reviewer(s) approve → commit the code, update task status to 'done'
6. If reviewer rejects → follow escalation model
7. After all tasks done → run milestone verification gate
8. Tag milestone: git tag 
9. Proceed to next milestone
```

---

## Spawning Workers

Use the `task` tool with `agent_type: "general-purpose"`:

```
task(
    agent_type: "general-purpose",
    description: "Implement <task-id>",
    model: "<model per task assignment>",
    prompt: <assembled context package>
)
```

### Context Package Assembly

For each worker, assemble this prompt:

```
<role>
You are implementing a task for j2534-logger.
Read the instructions in .agents/implement.md for your role, coding standards,
and patterns.
</role>

<task>
Task ID: {task_id}
Milestone: {milestone}
Files to create/modify: {file_list}
Specification: {spec_details}
</task>

<dependencies>
{signatures and types from dependency files}
</dependencies>

<reference>
{relevant existing source code for context}
</reference>

<pitfalls>
{relevant pitfalls from .agents/pitfalls.md}
{ALL entries from .agents/pitfalls-live.md}
</pitfalls>
```

**Important:** Include the FULL content of `.agents/implement.md`,
relevant sections of `.agents/pitfalls.md`, and ALL entries from
`.agents/pitfalls-live.md` in the prompt. Workers are stateless —
they only see what you give them.

Also include the project skill (`.agents/skills/j2534-logger/SKILL.md`)
when workers need domain knowledge.

---

## Spawning Reviewers

### Dual-Review Mode (review.dual_review: true)

After a worker completes, spawn **both reviewers in parallel**:

```
# Primary reviewer
task(
    agent_type: "general-purpose",
    model: "gpt-5.4",
    description: "Review <task-id> (primary)",
    prompt: <review context package>
)

# Secondary reviewer (in parallel)
task(
    agent_type: "general-purpose",
    model: "claude-opus-4.6",
    description: "Review <task-id> (secondary)",
    prompt: <review context package>
)
```

### Single-Review Mode (review.dual_review: false)

Spawn only the primary reviewer:

```
task(
    agent_type: "general-purpose",
    model: "gpt-5.4",
    description: "Review <task-id>",
    prompt: <review context package>
)
```

### Review Context Package

```
<role>
You are reviewing code for j2534-logger.
Read the instructions in .agents/reviewer.md for your review checklist and standards.
</role>

<task>
Task ID: {task_id}
Original spec: {task spec from worker prompt}
</task>

<code>
{all files created/modified by the worker}
</code>

<tests>
{all test files created by the worker}
</tests>

<previous_review>
{previous reviewer feedback, if this is a re-review iteration}
</previous_review>
```

---

## Dual-Review Verdict Merge Rules

Every task requires reviews before commit. When `dual_review` is enabled,
two independent reviews from different model families provide diverse
error-catching perspectives.

### Review Pair

| Role | Model | Strength |
|------|-------|----------|
| Primary reviewer | gpt-5.4 | Deep semantic/logic analysis |
| Secondary reviewer | claude-opus-4.6 | Pattern consistency, spec compliance |

### Verdict Merge

| Primary | Secondary | Action |
|---------|-----------|--------|
| APPROVED | APPROVED | **Commit** with dual sign-off |
| APPROVED + comments | APPROVED + comments | **Address** all comments from both, then commit |
| APPROVED | CHANGES_REQUESTED | **Address** the feedback, re-review with blocking model only |
| CHANGES_REQUESTED | APPROVED | **Address** the feedback, re-review with blocking model only |
| CHANGES_REQUESTED | CHANGES_REQUESTED | **Address** combined feedback, re-review with both |
| APPROVED | REJECTED (blocking) | **Escalate to human** — fundamental disagreement |
| REJECTED (blocking) | APPROVED | **Escalate to human** — fundamental disagreement |

**Blocking** = reviewer says the code is fundamentally wrong, not just needs tweaks.
**Comments** = suggestions that improve quality but don't block merge.

### Commit Sign-off Format

After approval, commit messages include:

```
task-id — description

Reviewed-by: gpt-5.4 ✓
Reviewed-by: claude-opus-4.6 ✓       # only if dual_review
Review-date: YYYY-MM-DD
Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>
```

If only one model reviewed a re-review cycle:

```
Reviewed-by: gpt-5.4 ✓
Reviewed-by: claude-opus-4.6 ✓ (prior cycle)
```

---

## Escalation Model

Track iteration count per task:

```sql
CREATE TABLE IF NOT EXISTS task_iterations (
    task_id TEXT PRIMARY KEY,
    phase INTEGER DEFAULT 1,       -- 1=initial, 2=model-switch, 3=fresh
    iteration INTEGER DEFAULT 0,
    current_model TEXT,
    original_model TEXT,
    reviewer_notes TEXT
);
```

### Phase 1: Same Model (up to 5 iterations)

Worker retries with the same model. Feed back reviewer notes each time.

```
If iteration > 5: → Phase 2
Switch to alternate model.
```

### Phase 2: Switched Model (up to 3 iterations)

A different model family attempts the task.

```
If iteration > 3: → Phase 3
Generate failure report. Spawn fresh worker with original spec + failure report.
```

### Phase 3: Fresh Worker (up to 8 total iterations)

Clean-slate attempt with the failure report as additional context.

```
If fails again: mark task FAILED.
Save failure report to .agents/failures/<task-id>.md.
Requires human intervention.
```

---

## Progress Tracking

Use SQL todos table:

```sql
-- Before starting a task
UPDATE todos SET status = 'in_progress' WHERE id = 'task-id';

-- After successful review
UPDATE todos SET status = 'done' WHERE id = 'task-id';

-- Check what's ready (no pending dependencies)
SELECT t.* FROM todos t
WHERE t.status = 'pending'
AND NOT EXISTS (
    SELECT 1 FROM todo_deps td
    JOIN todos dep ON td.depends_on = dep.id
    WHERE td.todo_id = t.id AND dep.status != 'done'
);
```

---

## Git Commits

After each task is reviewed and approved:

```bash
git add <files>
git commit -m "task-id — description

Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
```

At milestone boundary:
```bash
git tag <milestone-tag>
```

---

## Milestone Verification Gates

Before tagging a milestone, run all verification steps from config:

1. **All tasks done:** `SELECT COUNT(*) FROM todos WHERE id LIKE 'mN-%' AND status != 'done'` = 0
2. **Build passes:** `msbuild "J2534 PassThru Logger.sln" /p:Configuration=Release`
3. **Tests pass:** 
4. **Lint clean:**  (if configured)
5. **LOC compliance:** No source file exceeds 700 LOC

Add project-specific verification gates as needed.

---

## Adaptive Pitfalls Protocol

Track and feed back lessons learned during implementation.

### When to Append to `pitfalls-live.md`

1. **Post-struggle** — When a build/fix cycle requires ≥2 iterations on the
   same root cause, append a new `P-LIVE-NNN` entry describing the symptom,
   root cause, and fix.

2. **Post-review** — When a reviewer flags a bug class that the worker should
   have avoided (and no existing pitfall covers it), append an entry attributed
   to the review.

### Caveman-Aware Editing

When caveman compression is active, managed markdown files have a `.full.md`
sibling that is the authoritative version. Before editing any `.md` file:

1. **Check for `.full.md` sibling** — If `pitfalls-live.full.md` exists,
   edit that instead of `pitfalls-live.md`
2. **After editing**, run `/caveman refresh` to regenerate the compressed
   variant and update the active `FILE.md`
3. If no `.full.md` exists, the file is in pristine state — edit `FILE.md` directly

**Helper to determine the correct edit target:**
```bash
python3 -c "
from pathlib import Path
p = Path('FILE.md'); f = p.with_name(p.stem + '.full' + p.suffix)
print(str(f) if f.exists() else str(p))
"
```

This applies to ALL managed markdown files: `pitfalls-live.md`, `pitfalls.md`,
knowledge files, and skill files. Workers and reviewers should receive the
**active** `FILE.md` (which may be compressed) — they don't need to know about
the `.full.md` / `.caveman.md` convention.

### Quality Gate (before committing new entries)

Before adding any new pitfall entry, verify:

1. **Scoped correctly** — States what phase/component this applies to.
2. **Workaround doesn't harm the project** — It's a process constraint, not an
   architectural compromise. If it would change public APIs or design, flag for
   human review instead.
3. **Marked temporary** — Specifies what future work makes this pitfall obsolete.

### Consolidation (at milestone gates)

At each milestone gate:

1. Review all `pitfalls-live.md` entries accumulated during the milestone
2. Merge confirmed, generalizable entries into `pitfalls.md`
3. Archive the milestone's live entries at the bottom of `pitfalls-live.md`
4. Clear the active entries section

---

## Interface Tracking

Maintain a file `INTERFACES.md` that lists all public function signatures and
type definitions. Update it after each task completes. This is the "context
summary" that workers receive instead of reading every file.

Format:
```
## module_name

pub fn function_name(param: Type) -> ReturnType
pub struct TypeName { field: Type, ... }
pub trait TraitName { fn method(self) -> Type }
```

---

## Prohibited Actions

- **Never commit the tmp/ directory**
- **Never skip the review step**
- **Never exceed 2 parallel workers** without explicit human approval.
- **Never skip the review step.**
- **Never commit tmp/** — temporary debugging artifacts only.
