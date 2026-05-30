# Live Pitfalls (Auto-Updated)
# Template version: 0.1

> **Scope awareness:** Each pitfall entry must clearly state its scope —
> which component, phase, or toolchain version it applies to.
> Live pitfalls are temporary workarounds, not permanent design decisions.
>
> **Do not carry these restrictions into:**
> - Project design documents or specifications
> - Public APIs or user-facing interfaces
> - Documentation aimed at end users
> - Code that will run under a fixed/upgraded toolchain

---

## How This Works

1. **Post-struggle**: When a build/fix cycle requires ≥2 iterations on the
   same root cause, the orchestrator appends an entry here.
2. **Post-review**: When a reviewer flags a bug class that workers should
   avoid, it gets recorded here too.
3. **Worker prompts**: Include both `pitfalls.md` (static) and
   `pitfalls-live.md` (this file) — workers see the latest known issues.
4. **Consolidation**: At each milestone gate, merge confirmed entries into
   `pitfalls.md` and reset this file.

### Quality Gate for New Entries

Before adding or committing a new pitfall entry, verify:

- [ ] **Scoped correctly** — States explicitly which component/phase/tool
  version this applies to.
- [ ] **Fix doesn't harm the project** — The workaround is a process
  constraint, not a design compromise. If the workaround would change
  public APIs or architecture, flag it for human review instead.
- [ ] **Temporary** — The entry notes what future work will make this
  pitfall obsolete.

### Entry Format

```markdown
### P-LIVE-NNN: <short title>
- **Scope**: <component/phase/tool this applies to>
- **Discovered**: <task-id>, <date>
- **Symptom**: What went wrong (error message, crash, etc.)
- **Root cause**: Why it happened
- **Workaround**: What to do instead
- **Obsoleted by**: Which future work removes this limitation
- **Source**: struggle | review
```

---

## Entries

_No entries yet. Pitfalls will be appended here as they're discovered during development._

---

## Archive

<!-- At each milestone gate, resolved entries are moved here for history. -->
