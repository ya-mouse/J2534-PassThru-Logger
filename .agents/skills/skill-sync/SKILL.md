---
name: skill-sync
description: "Subproject and external rule set synchronization. Discovers subprojects and imports their instructions as scoped commands."
---

# /skill-sync — Subproject & External Rule Set Synchronization
# Template version: 0.1
#
# This command discovers subprojects (git repos, submodules) and external
# directories that use the same agent-rules template framework. It imports
# their instructions as scoped commands available via /scope-{name}-{command}.

---

## Command Syntax

```
/skill-sync                                 # Auto-discover subprojects + previously synced externals
/skill-sync add <path> [--name <alias>]     # Add an external directory as a synced source
/skill-sync remove <name>                   # Remove a previously synced source
/skill-sync list                            # List all synced sources and their scoped commands
/skill-sync refresh [<name>]                # Re-scan a specific source (or all)
```

---

## Prerequisites

- `.agents/config/agent-rules.yaml` must exist in the top-level project (run `/skill-init` first)
- Synced sources must have their own `.agents/config/agent-rules.yaml` (i.e., also initialized
  with this template framework)

---

## What It Does

### Problem

A top-level project may depend on subprojects, each with their own agent rules:
- **Git submodules** checked out under the repo
- **Nested repos** (monorepo components, vendored dependencies)
- **External directories** — source trees overridden via environment variables
  (e.g., a component normally built via Docker but locally checked out for
  integration work, code refactoring, or debugging)

Each subproject has its own skills, debugging workflows, pitfalls, and conventions.
When working on the top-level project, agents need access to both:
1. The top-level project's instructions (always included)
2. Relevant subproject instructions (on demand, via scoped commands)

### Solution

`skill-sync` discovers these sources and registers them as discoverable
proxy skills under `.agents/skills/scope-{name}/SKILL.md`. When a scoped
skill is invoked, the agent loads **both** the top-level project context
**and** the subproject's specific instructions.

### Sync Modes

`/skill-sync` supports two sync modes for SKILL.md content:

| Mode | Config Value | Description |
|------|-------------|-------------|
| **Full copy** (default) | `sync_mode: full_copy` | Embeds the entire external SKILL.md content into the local `.agents/skills/{name}/SKILL.md`. Agents get the content directly without needing to read external files. |
| **Proxy** | `sync_mode: proxy` | Local SKILL.md contains only a pointer to the external file path. Agents must read the external file at invocation time. |

**Full copy is the recommended default** because:
1. Sub-agents (explore, task, general-purpose) do **not reliably follow** "go read this external file" instructions — they are stateless and may skip or partially read the file
2. Copilot discovers skills by scanning `.agents/skills/*/SKILL.md` — the content must be present locally for automatic discovery to work
3. Session context compaction may lose the instruction to read external files
4. External paths may break if the source directory is moved or unmounted

**When to use proxy mode:**
- The external SKILL.md is extremely large (>50KB) and would bloat the local repo
- The external content changes very frequently and manual refresh is impractical
- You only need the skill occasionally and accept that agents may not load it reliably

### Deep-Dive Documents

External projects may have supplementary documentation (e.g., `agentic-docs/` directories)
that are too large to embed locally but valuable for on-demand reference. These are registered
as `deep_dive_docs` in the synced source config — agents can read them when working on
specific subsystems but they are not automatically loaded.

The SKILL.md (whether embedded or proxied) should include a **reference table** pointing to
these deep-dive docs with descriptions of when to read each one.

---

## Discovery Process

### Auto-Discovery (`/skill-sync` with no arguments)

1. **Scan for git submodules:**
   ```bash
   git submodule --quiet foreach 'echo $sm_path'
   ```
   For each submodule path, check if `{path}/.agents/config/agent-rules.yaml` exists.

2. **Scan for nested git repos:**
   ```bash
   find . -maxdepth 3 -name '.git' -type d | grep -v '^\./\.git$'
   ```
   For each nested repo, check if `{path}/.agents/config/agent-rules.yaml` exists.

3. **Re-scan previously registered external sources:**
   Read `synced_sources` from `.agents/config/agent-rules.yaml` and verify each
   external path still exists and has a valid config.

4. **Register discovered sources:**
   For each valid source, read its `.agents/config/agent-rules.yaml` to get the
   project name, then:
   - Create `.agents/skills/scope-{name}/SKILL.md` proxy file
   - Add entry to `synced_sources` in top-level config

### Adding External Sources (`/skill-sync add`)

```
/skill-sync add /path/to/external/component
/skill-sync add /path/to/external/component --name my-alias
/skill-sync add $MY_COMPONENT_SRC                          # env var expansion
```

**Flow:**

1. Resolve the path (expand env vars, resolve relative paths)
2. Check that `{path}/.agents/config/agent-rules.yaml` exists
3. Read the external project's name from its config
4. If `--name` is provided, use that as the alias; otherwise use the project name
5. Ask user to confirm:
   ```
   "Found agent-rules project '{external_name}' at {path}.
    Register as: scope-{name}-*
    Proceed?"
   → choices: ["Yes", "Yes, with different name", "Cancel"]
   ```
6. Ask user for sync mode:
   ```
   "How should the SKILL.md content be synced?"
   → choices: ["Full copy (Recommended)", "Proxy (reference only)"]
   ```
7. Add to `synced_sources` in `.agents/config/agent-rules.yaml`
8. Scan the source's commands and skills
9. **If full_copy mode:** Execute the content embedding procedure (see below)
10. **If proxy mode:** Create a minimal pointer file (see below)
11. Report created skills:
   ```
   ✓ Synced '{name}' from {path}
     Created: .agents/skills/scope-{name}/SKILL.md
     Available skills: scope-{name} (visible in /skills)
   ```

### Full Copy Procedure

When `sync_mode: full_copy` is selected (default), the agent:

1. **Read** the external source's primary SKILL.md:
   `{source_path}/.agents/skills/{project_name}/SKILL.md`

2. **Read** the external source's pitfalls (if they exist):
   - `{source_path}/.agents/pitfalls.md`
   - `{source_path}/.agents/pitfalls-live.md`

3. **Scan** for supplementary documentation directories:
   - Look for `{source_path}/agentic-docs/` or similar doc directories
   - Catalog any `.md` files found (these become `deep_dive_docs`)

4. **Create** the local embedded SKILL.md at `.agents/skills/scope-{name}/SKILL.md`:
   ```markdown
   ---
   name: scope-{name}
   description: "Scoped skill for {project_name}. Loads domain knowledge from external synced source."
   ---

   <!-- ═══════════════════════════════════════════════════════════════════
        SYNCED SKILL — Full copy from external project
        Source: {source_path}/.agents/skills/{project_name}/SKILL.md
        Last synced: {YYYY-MM-DD}
        To refresh: /skill-sync refresh {name}
        ═══════════════════════════════════════════════════════════════════ -->

   ## Precedence Rules

   When working with this scoped context:
   1. **Top-level ({{project.name}}) conventions always win** — naming, LOC limits, git patterns
   2. **{project_name} domain knowledge supplements** top-level context (doesn't replace)
   3. **{project_name} pitfalls are additive** — merged with top-level pitfalls
   4. **{project_name} debugging recipes are available** alongside top-level ones

   ---

   {full content of external SKILL.md, excluding its YAML frontmatter}

   ---

   ## Deep-Dive Reference Documents   (if agentic-docs found)

   {table of deep-dive docs with absolute paths and "When to Read" guidance}

   ---

   ## Pitfalls   (if non-empty pitfalls found)

   {content from pitfalls.md / pitfalls-live.md}
   ```

5. **Update** the synced source config in `agent-rules.yaml`:
   - Set `sync_mode: full_copy`
   - Set `last_synced: {today}`
   - Point `proxy_skill` to the local embedded path
   - Add `deep_dive_docs` list with absolute paths to supplementary docs

### Proxy Procedure

When `sync_mode: proxy` is selected, the agent creates a minimal SKILL.md
that instructs agents to read the external file (original behavior):

```markdown
---
name: scope-{name}
description: "Scoped skill for {project_name}. Proxy to external synced source."
---

# scope-{name} — {project_name}

This is a **proxy skill**. Read the external skill file for full domain knowledge:
`{source_path}/.agents/skills/{project_name}/SKILL.md`

⚠ **Note:** This proxy mode may not work reliably with sub-agents.
Consider running `/skill-sync refresh {name}` with full_copy mode instead.
```

---

## Source Scanning

For each synced source, scan for available instructions:

```
{source_path}/
├── .agents/
│   ├── config/agent-rules.yaml    → read project.name, version
│   ├── orchestrator.md            → register
│   ├── implement.md               → register
│   ├── reviewer.md                → register
│   ├── pitfalls.md                → register
│   ├── pitfalls-live.md           → register
│   └── skills/
│       ├── {name}/
│       │   └── SKILL.md           → register as primary skill
│       ├── skill-init/SKILL.md    → SKIP (control command)
│       ├── skill-discover/SKILL.md → SKIP (control command)
│       └── skill-sync/SKILL.md    → SKIP (control command)
└── .github/
    └── copilot-instructions.md    → register as project-instructions
```

### Control Command Filtering

The following commands from subprojects are **never** registered as scoped
commands — they are template management commands, not project instructions:

```
CONTROL_COMMANDS = [
    "skill-init",
    "skill-discover",
    "skill-sync",
]
```

Any command file whose name (without extension) matches a control command
is silently skipped during sync.

### Registered Scoped Commands

For a source named `backend` with the files above, the following scoped
skills become available:

```
scope-backend-skill               → loads SKILL.md
scope-backend-instructions        → loads copilot-instructions.md
scope-backend-implement           → loads implement.md
scope-backend-reviewer            → loads reviewer.md
scope-backend-orchestrator        → loads orchestrator.md
scope-backend-pitfalls            → loads pitfalls.md + pitfalls-live.md
```

### Creating Discoverable Skill Files

**CRITICAL**: Copilot CLI discovers skills by scanning `.agents/skills/*/SKILL.md`.
Simply writing metadata to `agent-rules.yaml` is NOT enough — the agent must
create actual skill files for each scoped command.

For each registered scoped command, create a **proxy skill file**:

```
.agents/skills/scope-{name}/SKILL.md
```

**Example:** For a source named `backend` at `./services/backend`:

Create `.agents/skills/scope-backend/SKILL.md`:

```markdown
---
name: scope-backend
description: "Scoped skills for the backend subproject. Provides domain knowledge, conventions, and debugging context from services/backend."
---

# Scoped Context: backend

This skill provides access to the `backend` subproject's agent rules.
Source path: `./services/backend`

When this skill is invoked, load the following context:

## Top-level context (always loaded first)

Include the top-level project's instructions:
- Read: `.github/copilot-instructions.md`
- Read: `.agents/skills/{{project.name}}/SKILL.md`

## Subproject context (layered on top)

Include the backend subproject's domain knowledge and instructions:
- Read: `./services/backend/.agents/skills/backend-api/SKILL.md`
- Read: `./services/backend/.github/copilot-instructions.md`
- Read: `./services/backend/.agents/pitfalls.md`
- Read: `./services/backend/.agents/pitfalls-live.md`

## Precedence

The top-level project's conventions and rules take precedence.
When conventions conflict, follow the top-level project's rules.
Subproject domain knowledge supplements (does not replace) top-level context.
```

**What to include in proxy skill files:**

| Source file | Include in proxy? | How |
|-------------|-------------------|-----|
| `skills/{name}/SKILL.md` | Yes — primary content | Read and embed, or reference path for agent to load |
| `copilot-instructions.md` | Yes — conventions | Reference path |
| `pitfalls.md` + `pitfalls-live.md` | Yes — additive | Reference paths |
| `implement.md` | Optional | Only if workers need subproject-specific constraints |
| `reviewer.md` | Optional | Only if review criteria differ |
| `orchestrator.md` | No — use top-level | Subproject orchestrator is not imported |

**On sync, the agent MUST:**

1. Create the directory: `.agents/skills/scope-{name}/`
2. Generate `SKILL.md` with valid YAML frontmatter (`name`, `description`)
3. List all source files to load as part of this scoped context
4. Update `synced_sources` in `.agents/config/agent-rules.yaml`

**On remove, the agent MUST:**

1. Delete `.agents/skills/scope-{name}/` directory
2. Remove the entry from `synced_sources`

**On refresh, the agent MUST:**

1. Regenerate the proxy SKILL.md if source files changed
2. Update `synced_sources` metadata

---

## Scoped Command Invocation

When a user invokes a scoped command, the agent:

1. **Always loads top-level context first:**
   - Top-level `.github/copilot-instructions.md`
   - Top-level `.agents/skills/{project}/SKILL.md`
   - Top-level `.agents/config/agent-rules.yaml` (for conventions, limits, etc.)

2. **Then loads the scoped instruction:**
   - The specific file from the synced source
   - The source's `.agents/config/agent-rules.yaml` (for source-specific conventions)

3. **Presents combined context to the agent:**
   ```
   <top_level_context>
   {top-level copilot-instructions.md}
   {top-level SKILL.md}
   </top_level_context>

   <scoped_context source="{name}" path="{source_path}">
   {scoped instruction file content}
   </scoped_context>

   <note>
   You are working in the context of the top-level project "{{project.name}}"
   with additional context from the "{name}" subproject.
   The top-level project's conventions and rules take precedence.
   When conventions conflict, follow the top-level project's rules.
   </note>
   ```

### Precedence Rules

When top-level and subproject instructions conflict:
1. **Top-level conventions win** (naming, LOC limits, git patterns)
2. **Subproject domain knowledge supplements** (doesn't replace) top-level
3. **Subproject pitfalls are additive** — merged with top-level pitfalls
4. **Subproject debugging recipes are available** alongside top-level ones

---

## Config Schema: synced_sources

Added to `.agents/config/agent-rules.yaml`:

```yaml
# ─── Synced Sources (managed by skill-sync) ─────────────────────────────
synced_sources: []
  # - name: "backend"                    # Scoped skill prefix
  #   path: "./services/backend"         # Relative or absolute path
  #   type: "submodule"                  # submodule | nested_repo | external
  #   project_name: "backend-api"        # From source's agent-rules.yaml
  #   version: "0.1.2"                   # Source's template version
  #   last_synced: "2026-03-08"
  #   proxy_skill: ".agents/skills/scope-backend/SKILL.md"  # Created proxy
  #   source_files:                      # Files loaded by the proxy skill
  #     - ".agents/skills/backend-api/SKILL.md"
  #     - ".github/copilot-instructions.md"
  #     - ".agents/pitfalls.md"
  #     - ".agents/pitfalls-live.md"
```

---

## Listing Synced Sources

`/skill-sync list` outputs:

```
Synced sources for {{project.name}}:

  backend (submodule @ ./services/backend)
    Version: 0.1.2 | Last synced: 2026-03-08
    Proxy skill: .agents/skills/scope-backend/SKILL.md
    Visible in /skills as: scope-backend

  ml-engine (external @ /Users/dev/ml-engine)
    Version: 0.1.0 | Last synced: 2026-03-07
    Proxy skill: .agents/skills/scope-ml-engine/SKILL.md
    Visible in /skills as: scope-ml-engine

  Total: 2 sources, 2 scoped skills
```

---

## Removing Sources

`/skill-sync remove <name>` removes a synced source:

1. Confirm with user: `"Remove '{name}' from synced sources?"`
2. Delete `.agents/skills/scope-{name}/` directory (the proxy skill file)
3. Remove the entry from `synced_sources` in `.agents/config/agent-rules.yaml`
4. Report: `"✓ Removed '{name}'. Proxy skill and scoped commands removed."`

Note: This only removes the sync registration and proxy. It does not delete the
subproject's files or agent rules.

---

## Refreshing Sources

`/skill-sync refresh` re-scans all sources (or a specific one):

1. For each source, verify the path still exists
2. Re-read its `.agents/config/agent-rules.yaml` for version changes
3. Re-scan for available commands (files may have been added/removed)
4. Regenerate `.agents/skills/scope-{name}/SKILL.md` proxy files if source files changed
5. Update the registry in top-level `.agents/config/agent-rules.yaml`
6. Report changes:
   ```
   ✓ Refreshed 2 sources:
     backend: no changes
     ml-engine: +1 command (reviewer added), version 0.1.0 → 0.1.1, proxy updated
   ```

Sources whose paths no longer exist are flagged:
```
⚠ Source 'ml-engine' path not found: /Users/dev/ml-engine
  → choices: ["Remove it", "Update path", "Skip for now"]
```

---

## Use Cases

### 1. Monorepo with submodules

```
my-project/
├── .agents/config/agent-rules.yaml          # top-level
├── .agents/...
├── services/
│   ├── api/                         # git submodule, has its own agent-rules
│   │   ├── .agents/config/agent-rules.yaml
│   │   └── .agents/...
│   └── worker/                      # git submodule
│       ├── .agents/config/agent-rules.yaml
│       └── .agents/...
```

After `skill-sync`:
- `scope-api` skill created → API service domain knowledge
- `scope-worker` skill created → Worker service domain knowledge
- Both visible in `/skills`, top-level context always included

### 2. External source override (Docker component)

Normally `ml-engine` runs in Docker. For integration work, the developer
checks it out locally:

```
> Use skill-sync to add $ML_ENGINE_SRC as ml-engine
```

Creates `.agents/skills/scope-ml-engine/SKILL.md` proxy.
Now agents understand both the top-level project AND the ML engine's
internals when doing integration work or refactoring across boundaries.

### 3. Cross-project refactoring

Working on a shared library used by multiple projects:

```
> Use skill-sync to add ../shared-lib as shared
> Use scope-shared to understand the library's conventions
# Now refactor with both project's and library's context
```

---

## Version Compatibility

When syncing, check that the source's `version.template` is compatible
with the top-level project's template version:

| Top-level | Source | Action |
|-----------|--------|--------|
| 0.1 | 0.1 | ✓ Compatible |
| 0.1 | 0.2 | ⚠ Warn: source uses newer template (may have extra features) |
| 0.2 | 0.1 | ⚠ Warn: source uses older template (may be missing features) |
| 0.x | 1.x | ✗ Error: major version mismatch — template structure may differ |

Warnings are informational only — sync proceeds. Errors require `--force` to override.
