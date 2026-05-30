# Skill Base — Mandatory Pre-Task & Device-Interaction Protocols
# Template version: 0.1
#
# This file is the **base instruction set** every domain `SKILL.md` inherits.
# Domain skills (e.g. `.agents/skills/<name>/SKILL.md`) MUST link this file
# at the top of their orchestration section and MUST follow these protocols
# before doing any non-trivial work — orchestrator and workers alike.
#
# `skill-init` and `skill-discover` instantiate / preserve this file. Do not
# duplicate its content into individual skills; reference it instead.

---

## ⚠️ MANDATORY: Pre-Task Knowledge Search

**Applies to: orchestrator and workers, before every task — not only when
spawning workers.** Workers receive these files in their prompts; the
orchestrator must consult them itself before making decisions.

### Order of consultation

1. **Module documentation** — If the task touches a module directory,
   READ its `AGENTS.md` first. If it does not exist and the module has
   ≥5 source files OR ≥500 LOC, propose creating it via `MD-Writer`
   before continuing.

2. **Workflow knowledge** — If the task involves a recurring procedure
   (build, deploy, test, deps update, …), SEARCH and READ the matching
   article in `.agents/knowledge/workflows/`. Index lives in
   `.agents/knowledge/README.md`.

3. **Pattern knowledge** — If the task involves a recurring code pattern,
   SEARCH `.agents/knowledge/patterns/`.

4. **Pitfalls** — READ relevant sections of `.agents/pitfalls.md` and **ALL**
   entries in `.agents/pitfalls-live.md` matching the area of work
   (grep by component, toolchain, platform, or symptom).

5. **Design docs** — For architecture-level work, SEARCH `docs/<domain>/`
   for the relevant design document.

### Failure mode this prevents

Reinventing tooling that already exists. **Always grep the knowledge base
first** before writing a custom solution for something that may already be
documented.

---

## ⚠️ MANDATORY: Device/Environment Interaction Pre-Flight

**Before any deploy, flash, RPC call, or environment interaction step —
orchestrator AND workers — perform this checklist:**

### 1. Load environment

Read `.agents/local.env` (gitignored, user-specific). This file contains
device addresses, credentials, and environment-specific values.

If the file or a needed variable is missing, **stop and ask the user**
or refer them to `.agents/local.env.example`. Do NOT improvise addresses
or credentials, do NOT scan the network.

### 2. Consult `.agents/knowledge/workflows/`

The knowledge workflows are the single source of truth for operational
procedures. Read the relevant article before performing any device or
environment interaction.

### 3. Use canonical tools — do not reinvent

If a task seems to require a non-canonical tool, **first** grep
`.agents/knowledge/workflows/` and `.agents/pitfalls-live.md` for the
canonical recipe. If it truly is missing, propose adding it as a knowledge
article — do not silently invent ad-hoc alternatives.

### 4. Failure handling

If a device/service is unreachable: check the documented recovery paths
in the knowledge workflows. Do not loop on probes or invent ad-hoc
recovery procedures.

---

## ⚠️ MANDATORY: Review Before Every Code Commit

When `review.dual_review: true` in `.agents/config/agent-rules.yaml`, every
code commit MUST carry `Reviewed-by:` stamps from BOTH model families
listed in `models.review_primary` and `models.review_secondary`. Workflow
detail and verdict-merge rules live in `.agents/orchestrator.md`.

Exempt from review (still must be committed cleanly):

- Documentation-only changes (`*.md`, `docs/`, `.agents/`, `AGENTS.md`)
- Task tracker updates
- Knowledge-base updates (`.agents/knowledge/`, `.agents/pitfalls-live.md`)
- Generated files explicitly listed in the project rules

---

## Post-Task: Knowledge Evolution

After each successful commit, ASSESS whether the task surfaced:

1. **A new pattern** → article in `.agents/knowledge/patterns/`
2. **A new procedure / workflow** → article in `.agents/knowledge/workflows/`
3. **A new bug class / workaround** → entry in `.agents/pitfalls-live.md`
   (use the format defined in that file's header)
4. **A module pattern/API change** → update the module's `AGENTS.md`
5. **An architecture-level discovery** → update / create
   `docs/<domain>/<topic>.md`
6. **A canonical-tool rediscovery** (you almost wrote a custom tool for
   something the project already provides) → entry in `pitfalls-live.md`
   AND add the recipe to the relevant `workflows/*.md`.

Commit knowledge + docs updates with prefix `knowledge:` (or
`docs:` for `docs/`); they are exempt from dual review.

Skip this step only when the task was purely mechanical with no new
learnings.

---

## Reference Files (loaded into worker prompts)

| File | Contents |
|------|----------|
| `.agents/skill-base.md` | THIS FILE — base instructions every skill inherits |
| `.agents/orchestrator.md` | Detailed spawning, escalation, verdict merge |
| `.agents/implement.md` | Worker coding standards, output format, design analysis |
| `.agents/reviewer.md` | Review checklist, verdict format, severity guide |
| `.agents/knowledge/` | Cross-cutting workflows + patterns; index in `README.md` |
| `.agents/pitfalls.md` | Fixed lessons learned — include relevant sections in worker prompts |
| `.agents/pitfalls-live.md` | **Adaptive** — include ALL entries in worker prompts |
| `.agents/local.env` | Environment-specific values (gitignored) |
| `.agents/config/agent-rules.yaml` | Models, review config, escalation thresholds, gates |

---

## How a Domain Skill Inherits This

A domain `SKILL.md` (e.g. `.agents/skills/<name>/SKILL.md`) MUST contain,
near the top of its orchestration section:

```markdown
## Inherited Base Instructions

This skill inherits the mandatory protocols defined in
[`.agents/skill-base.md`](../../skill-base.md):

- **Pre-Task Knowledge Search** — read module AGENTS.md, knowledge/, pitfalls-live.md before starting
- **Device/Environment Interaction Pre-Flight** — load `.agents/local.env`, consult `knowledge/workflows/`, use canonical tools
- **Mandatory Review** — dual sign-off before every code commit
- **Post-Task Knowledge Evolution** — capture new patterns, workflows, pitfalls

**Read `.agents/skill-base.md` in full before resuming work in this skill.**
```

The skill itself adds only domain-specific content on top of this base.
