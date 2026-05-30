---
name: skill-discover
description: "Guided repository discovery. Explores the codebase to populate architecture, debugging workflows, and domain knowledge in agent rules."
---

# /skill-discover — Guided Repository Discovery
# Template version: 0.1
#
# This command is implemented as a Copilot CLI instruction file.
# When the user runs `/skill-discover [focus-area]`, Copilot reads this file
# and conducts a guided exploration of the repository to populate the
# <!-- FILL: ... --> sections in templates and discover project-specific
# patterns, pitfalls, and architecture.

---

## Command Syntax

```
/skill-discover                    # Full discovery — scans everything
/skill-discover architecture       # Focus on architecture & pipeline
/skill-discover debugging          # Focus on debugging workflows
/skill-discover testing            # Focus on test patterns & coverage
/skill-discover domain             # Focus on domain knowledge & design decisions
/skill-discover pitfalls           # Focus on known issues & anti-patterns
```

---

## Prerequisites

- `.agents/config/agent-rules.yaml` must exist (run `/skill-init` first)
- Agent template files must be instantiated in the project

If prerequisites not met, tell the user:
```
⚠ Run /skill-init first to set up the agent rules framework.
```

---

## Discovery Process

### Phase 1: Repository Scan (automatic, no user input needed)

Gather factual data about the repository:

```
1. Read .agents/config/agent-rules.yaml for current state
2. Scan project root:
   - List all top-level directories and files
   - Count files by extension
   - Identify build/config files (Cargo.toml, package.json, etc.)
   - Find README, CONTRIBUTING, docs/
3. Scan source directory (paths.source_root):
   - Map module/directory structure
   - Identify entry points (main.*, index.*, mod.*, etc.)
   - Count LOC per file (flag any exceeding limits.max_loc)
   - Identify public API surfaces
4. Scan test directory (paths.test_root):
   - Map test structure
   - Identify test framework
   - Check coverage patterns (which source files have matching tests)
5. Scan for CI/CD:
   - .github/workflows/, .gitlab-ci.yml, Jenkinsfile, etc.
6. Scan for design docs:
   - docs/, ADR/, DEC/, decisions/, specs/
7. Check git history (if available):
   - Recent commit patterns
   - Most-changed files (likely hot spots)
   - Common commit message patterns
```

Store results in `discovered.*` section of agent-rules.yaml.

### Phase 2: Architecture Discovery (interactive)

Present findings and ask clarifying questions:

```
"I found the following project structure:
  {summarized directory tree}
  {N source files, M test files, K doc files}

Let me ask some questions to understand the architecture."
```

**Questions to ask (adapt based on what was found):**

```
Q1: "Is this the correct high-level description of the project architecture?"
    → Present inferred architecture based on directory names and file patterns
    → choices: ["Yes, that's right", "Partially — let me clarify"]

Q2: "Does the project have a processing pipeline or data flow?"
    → choices: ["Yes — let me describe it", "No — it's request/response",
                "No — it's event-driven", "No — it's a library"]

Q3: (if pipeline) "Describe the pipeline stages:"
    → freeform
    → Use response to populate architecture.pipeline

Q4: "Which files should agents read first to understand the project?"
    → Present top candidates (README, main entry point, key config)
    → Allow user to confirm or add more
    → Maps to: architecture.key_files

Q5: "Are there design decisions or ADR documents I should know about?"
    → Present any docs/ or decisions/ found
    → Maps to: domain.design_docs
```

### Phase 3: Debugging Discovery (interactive)

```
Q6: "How do you typically debug issues in this project?"
    → freeform
    → Maps to: debugging.overview

Q7: "Are there specific debugging commands or recipes agents should know?"
    → freeform (multi-line)
    → Maps to: debugging.recipes[]

Q8: "Are there common error patterns that trip people up?"
    → freeform (multi-line)
    → Maps to: debugging.crash_patterns[]

Q9: "Are there multi-step verification workflows? (e.g., build → test → integration)"
    → freeform
    → Maps to: verification steps
```

### Phase 4: Domain Knowledge Discovery (interactive)

```
Q10: "What are the key domain concepts an agent needs to understand?"
     → freeform
     → Maps to: domain.key_concepts[]

Q11: "Are there external references (docs, specs, RFCs) agents should consult?"
     → freeform
     → Maps to: domain.external_references[]

Q12: "What coding patterns are specific to this project that aren't obvious?"
     → freeform
     → Populates SKILL.md sections

Q13: "What are the biggest mistakes someone new to this codebase makes?"
     → freeform
     → Seeds pitfalls.md with initial entries
```

### Phase 5: Testing Discovery (interactive)

```
Q14: "What's the test strategy?"
     → choices: ["Unit tests per module", "Integration tests",
                 "End-to-end tests", "Mix of all"]

Q15: "What test framework is used?"
     → Auto-detect from dependencies, confirm with user

Q16: "Any special test conventions?"
     → freeform
     → Maps to: conventions.testing
```

---

## Focus-Area Behavior

When a focus area is specified, skip unrelated phases:

| Focus | Phases Run |
|-------|-----------|
| `architecture` | 1 (scan) + 2 (architecture) |
| `debugging` | 1 (scan) + 3 (debugging) |
| `testing` | 1 (scan) + 5 (testing) |
| `domain` | 1 (scan) + 4 (domain) |
| `pitfalls` | 1 (scan) + 4.Q13 (mistakes) + review git history for fix patterns |
| (none) | All phases |

---

## Output Actions

After discovery:

1. **Update .agents/config/agent-rules.yaml** — Fill `discovered.*`, `architecture.*`,
   `debugging.*`, `domain.*` sections

2. **Populate SKILL.md** — Fill the `<!-- FILL: ... -->` sections in
   `.agents/skills/{project.name}/SKILL.md` with discovered information

3. **Populate copilot-instructions.md** — Fill `<!-- FILL: ... -->` sections
   in `.github/copilot-instructions.md`

4. **Seed pitfalls.md** — If the user described common mistakes, create
   initial entries in the appropriate categories

5. **Bump version** — Increment `version.custom` in agent-rules.yaml

6. **Report summary:**

```
✓ Discovery complete for {{project.name}}

  Scanned: {N} source files, {M} test files, {K} doc files
  Updated:
    - .agents/config/agent-rules.yaml (architecture, debugging, domain)
    - .agents/skills/{{project.name}}/SKILL.md ({sections filled})
    - .github/copilot-instructions.md ({sections filled})
    - .agents/pitfalls.md ({N} initial entries)

  Version: 0.1.{new_custom}

  Remaining <!-- FILL: --> sections: {count}
  Run /skill-discover {focus} to fill specific sections.
```

---

## Asking Good Questions

### When to ask vs. infer

- **Infer** facts from files (language, framework, directory structure, build commands)
- **Ask** about intent (architecture decisions, debugging workflows, domain knowledge)
- **Confirm** inferences ("I see you use pytest — is that correct?")

### When something is unclear

If the agent can't determine something from the codebase:

```
"I'm not sure about X. I see [evidence], which could mean [A] or [B].
Which is closer to what you intended?"
```

Always provide choices based on evidence, plus a freeform option.

### Iterative deepening

After the initial pass, suggest specific areas to explore deeper:

```
"I have a good overview now. These areas could use more detail:
  1. Debugging workflows for [specific module]
  2. Error handling patterns in [specific area]
  3. Cross-module interaction between [A] and [B]

Would you like to deep-dive into any of these?"
```

---

## Re-running Discovery

`/skill-discover` can be run multiple times. Each run:

- Preserves existing content (doesn't overwrite filled sections)
- Only fills `<!-- FILL: ... -->` sections that are still present
- Updates `discovered.*` with fresh scan data
- Bumps `version.custom`

To force a re-scan of already-filled sections, the user can:
1. Re-add `<!-- FILL: ... -->` markers to sections they want refreshed
2. Or delete the section content and re-run
