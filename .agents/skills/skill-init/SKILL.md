---
name: skill-init
description: "Initial project configuration. Guided setup to populate agent-rules.yaml and instantiate templates for a new project."
---

# /skill-init — Initial Project Configuration
# Template version: 0.1
#
# This command is implemented as a Copilot CLI instruction file.
# When the user runs `/skill-init`, Copilot reads this file and follows
# the guided workflow to populate .agents/config/agent-rules.yaml and instantiate
# all template files for the project.

---

## Command Behavior

When `/skill-init` is invoked:

1. **Check for existing config** — If `.agents/config/agent-rules.yaml` exists and has
   `project.name` filled, inform the user and ask whether to reconfigure or abort.

2. **Guided Q&A** — Walk through configuration sections in order, using
   `ask_user` with choices where possible. Collect all answers before writing.

3. **Write config** — Populate `.agents/config/agent-rules.yaml` with collected values.

4. **Instantiate templates** — Copy template files into the project, replacing
   `{{placeholders}}` with config values. Rename `_project` directory to
   the project name.

5. **Bump version** — Set `version.custom: 0` for fresh init.

6. **Suggest next step** — Tell the user to run `/skill-discover` to learn
   the codebase and fill in architecture, debugging, and domain sections.

---

## Q&A Flow

### Phase 1: Project Basics

```
Q1: "What is the project name?"
    → type: freeform
    → maps to: project.name

Q2: "Give a one-line description of the project."
    → type: freeform
    → maps to: project.description

Q3: "What is the primary programming language?"
    → choices: ["Rust", "TypeScript", "Go", "Python", "C++", "Java", "Vela"]
    → maps to: project.language, project.file_extension (auto-derived)

Q4: "What is the project status?"
    → choices: ["Active development", "Design phase", "Maintenance", "Pre-release"]
    → maps to: project.status
```

### Phase 2: Directory Layout

```
Q5: "Where is the source code?"
    → choices: ["src/ (Recommended)", "lib/", "app/"]
    → default: "src"
    → maps to: paths.source_root

Q6: "Where are the tests?"
    → choices: ["tests/ (Recommended)", "test/", "spec/", "Same as source (inline)"]
    → default: "tests"
    → maps to: paths.test_root

Q7: "Where are the docs?"
    → choices: ["docs/ (Recommended)", "doc/", "documentation/"]
    → default: "docs"
    → maps to: paths.docs_dir
```

### Phase 3: Build & Test Commands

```
Q8: "What command builds the project?"
    → type: freeform
    → hint: auto-detect from package.json, Cargo.toml, Makefile, go.mod
    → maps to: commands.build

Q9: "What command runs the tests?"
    → type: freeform
    → hint: auto-detect from project config
    → maps to: commands.test

Q10: "What command runs the linter? (optional, press Enter to skip)"
     → type: freeform, allow_empty
     → maps to: commands.lint

Q11: "What command formats the code? (optional, press Enter to skip)"
     → type: freeform, allow_empty
     → maps to: commands.format
```

### Phase 4: Code Quality

```
Q12: "Target LOC per source file?"
     → choices: ["500 (Recommended)", "300", "400", "600"]
     → default: 500
     → maps to: limits.target_loc

Q13: "Hard maximum LOC per file?"
     → choices: ["700 (Recommended)", "500", "600", "800", "1000"]
     → default: 700
     → maps to: limits.max_loc
```

### Phase 5: AI Models

```
Q14: "Primary worker model?"
     → choices: ["claude-sonnet-4.6 (Recommended)", "claude-opus-4.6", "gpt-5.4", "gpt-5.2"]
     → maps to: models.worker_primary

Q15: "Primary reviewer model?"
     → choices: ["claude-opus-4.6 (Recommended)", "gpt-5.4", "claude-sonnet-4.6"]
     → maps to: models.review_primary
```

### Phase 6: Review Mode

```
Q16: "Enable dual-model review? (two independent reviews from different AI model families)"
     → choices: ["Yes — dual review (Recommended)", "No — single reviewer"]
     → maps to: review.dual_review

     If yes:
     Q16a: "Secondary reviewer model?"
           → choices: ["gpt-5.4 (Recommended)", "claude-opus-4.6", "claude-sonnet-4.6"]
           → maps to: models.review_secondary
```

### Phase 7: Escalation

```
Q17: "Adjust escalation thresholds? (defaults: 5 same-model / 3 switched / 8 fresh)"
     → choices: ["Keep defaults (Recommended)", "Customize"]

     If customize:
     Q17a: "Max same-model retries before switching?" → freeform (default: 5)
     Q17b: "Max switched-model retries before fresh worker?" → freeform (default: 3)
     Q17c: "Max fresh-worker retries before failure?" → freeform (default: 8)

Q18: "Max parallel workers?"
     → choices: ["2 (Recommended)", "1", "3", "4"]
     → maps to: escalation.max_parallel_workers
```

### Phase 8: Git Conventions

```
Q19: "Commit message prefix? (e.g., 'feat:', 'stage2/mN:', or empty for none)"
     → type: freeform, allow_empty
     → maps to: git.commit_prefix

Q20: "Milestone tag pattern? (e.g., 'v{N}', 'milestone-{N}', or empty for none)"
     → type: freeform, allow_empty
     → maps to: git.milestone_tag_pattern
```

### Phase 9: Naming Conventions

```
Q21: "Function naming convention?"
     → choices: ["snake_case (Recommended for Rust/Python/Go)", "camelCase (Recommended for JS/TS/Java)"]
     → maps to: conventions.naming.functions

Q22: "Type naming convention?"
     → choices: ["PascalCase (Recommended)", "camelCase"]
     → maps to: conventions.naming.types
```

---

## Auto-Detection

Before asking Q8-Q11 (build commands), scan the project root for:

| File | Detected Stack | Build | Test | Lint | Format |
|------|---------------|-------|------|------|--------|
| `Cargo.toml` | Rust | `cargo build` | `cargo test` | `cargo clippy` | `cargo fmt` |
| `package.json` | Node.js | `npm run build` | `npm test` | `npm run lint` | `npm run format` |
| `go.mod` | Go | `go build ./...` | `go test ./...` | `golangci-lint run` | `gofmt -w .` |
| `pyproject.toml` | Python | `python -m build` | `pytest` | `ruff check` | `ruff format` |
| `Makefile` | Make | `make build` | `make test` | `make lint` | `make fmt` |
| `build.sh` | Shell | `bash build.sh` | | | |

Present detected values as defaults, let user confirm or override.

---

## Template Instantiation

After collecting all values:

1. Copy `templates/.agents/` → `.agents/` in project root
   - This MUST include `.agents/skill-base.md` (the base instructions
     every domain skill inherits — pre-task knowledge search, environment
     interaction pre-flight, mandatory review, knowledge evolution).
     If the template tree lacks it, copy from a sibling project or
     re-fetch the latest skill-init asset bundle.
2. Copy `templates/.github/` → `.github/`
3. Rename `.agents/skills/_project/` → `.agents/skills/{project.name}/`
4. In all copied files, replace `{{placeholder}}` with config values
5. Verify the new domain `SKILL.md` opens with the
   "Inherited Base Instructions" block referencing
   `.agents/skill-base.md`. If missing, prepend it (template content is
   in `.agents/skill-base.md` § "How a Domain Skill Inherits This").
6. Leave `<!-- FILL: ... -->` comments in place — those are for `/skill-discover`
7. Create `{{paths.tmp_dir}}/` directory and add to `.gitignore`
8. Create `.agents/local.env.example` with placeholder environment blocks
   (project-specific variables: device addresses, ports, credentials, etc.).
   Add `.agents/local.env` itself to `.gitignore`.

---

## Completion Message

```
✓ Agent rules initialized for {{project.name}}

  Config:    .agents/config/agent-rules.yaml
  Agents:    .agents/ (orchestrator, implement, reviewer, pitfalls, skill)
  Version:   0.1.0

  Next step: Run /skill-discover to learn the codebase and fill in
  architecture, debugging workflows, and domain knowledge.
```
