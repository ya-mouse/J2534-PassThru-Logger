---
name: caveman
description: >
  Token-efficient compression for agent instruction files. Compresses natural language
  in .agents/ markdown files while preserving code, URLs, structure, and technical terms.
  Supports levels: lite, full (default), ultra. Manages FILE.full.md / FILE.caveman.md
  variants with FILE.md as the active copy.
---

# /caveman — Token-Efficient Agent Instructions

Compress agent instruction files to reduce input token usage while preserving
full technical accuracy. Based on the caveman approach: "why use many token when
few do trick."

## Command Syntax

```
/caveman [level] [path...]     — Compress files to caveman style
/caveman normal [path...]      — Restore full-prose versions
/caveman status [path...]      — Show compression state of files
/caveman refresh [path...]     — Recompress files whose full version changed
/caveman help [topic/path...]  — Interactive guide with contextual examples
```

**Levels:** `lite` | `full` (default) | `ultra`

**Paths:** Files or directories. If omitted, uses configured `default_targets`.

## File Convention

Every managed file has up to three variants:

| File | Role |
|------|------|
| `FILE.md` | **Active** — what agents and tools read |
| `FILE.full.md` | **Full prose** — authoritative source of truth, never auto-modified |
| `FILE.caveman.md` | **Compressed** — generated from FILE.full.md, never hand-edited |

**State transitions:**

```
PRISTINE (only FILE.md exists)
    │
    ▼  /caveman [level]
CAVEMAN_ACTIVE (FILE.md = copy of FILE.caveman.md)
    │
    ▼  /caveman normal
FULL_ACTIVE (FILE.md = copy of FILE.full.md)
    │
    ▼  /caveman [level]
CAVEMAN_ACTIVE (recompress from FILE.full.md)
```

**Rules:**
- `FILE.full.md` is NEVER modified by compression — only by humans or agents updating content
- `FILE.caveman.md` is ALWAYS regenerated from `FILE.full.md` (never re-compress compressed text)
- `FILE.md` is ALWAYS a copy of one of the two variants
- A file with no `.full.md` sibling has never been processed by caveman

## Configuration

Read from `.agents/config/agent-rules.yaml` under the `caveman:` key:

```yaml
caveman:
  default_level: full
  model: claude-sonnet-4.6
  default_targets:
    - .agents/knowledge/
    - .agents/pitfalls.md
    - .agents/pitfalls-live.md
  explicit_targets:
    - .agents/skills/
  exclude_globs:
    - "**/CHANGELOG.md"
    - "**/LICENSE*"
    - "**/.caveman-state.json"
```

- `default_targets`: Recursed automatically when `/caveman` is invoked without paths
- `explicit_targets`: Only processed when explicitly named (e.g., `/caveman full .agents/skills/foo/SKILL.md`)
- `exclude_globs`: Always skipped

---

## Execution Protocol

### `/caveman [level] [path...]`

1. **Resolve targets**
   - If paths given: use them directly
   - If no paths: use `default_targets` from config (recurse into directories)
   - For each directory: find all `*.md` files recursively, filtered by `should_compress()`
   - Skip files matching `exclude_globs`
   - Skip files under `explicit_targets` directories unless explicitly named

2. **For each target file:**

   a. **Check state** — Run Python state detection:
      ```bash
      python3 -m scripts status <filepath>
      ```
      Read the output to determine current state.

   b. **If PRISTINE or FULL_ACTIVE or needs recompress:**
      - Call `prepare_for_compress()` → ensures `FILE.full.md` exists
      - Read the content of `FILE.full.md`
      - **Extract and lock frontmatter** — YAML `---` block at file start must be preserved exactly
      - Spawn compression sub-agent (see §Compression Sub-Agent below)
      - Write result to `FILE.caveman.md`
      - Validate: `python3 -m scripts validate FILE.full.md FILE.caveman.md`
      - On validation failure → spawn fix sub-agent (up to 2 retries)
      - On success → copy `FILE.caveman.md` → `FILE.md`, update `.caveman-state.json`

   c. **If CAVEMAN_ACTIVE and same level:**
      - Check `needs_recompress()` — if full.md hash unchanged, **skip** (no-op)
      - If full.md changed, recompress from full.md

3. **Post-compression reference check**
   ```bash
   python3 -m scripts refs <root_directory>
   ```
   Report any broken anchors or cross-file links. If issues found, fix them
   in the caveman variants (never modify `.full.md` for reference fixups).

4. **Report summary** to user:
   ```
   Caveman compression complete (level: full)
     Compressed:  12 files
     Skipped:     3 files (no change)
     Errors:      0
     Ref issues:  1 (see above)
   ```

### `/caveman normal [path...]`

1. Resolve targets (same logic as above, but also include `explicit_targets` directories)
2. For each file in CAVEMAN_ACTIVE state:
   - Copy `FILE.full.md` → `FILE.md`
   - Update `.caveman-state.json` mode to `full_active`
   - Keep `.full.md` and `.caveman.md` for quick re-activation later
3. Report how many files restored.

### `/caveman status [path...]`

1. Resolve targets
2. For each file, run: `python3 -m scripts list <directory>`
3. Display table:
   ```
   State              File                           Level   Drift
   ─────────────────  ─────────────────────────────  ──────  ─────
   caveman_active     .agents/pitfalls.md            full    no
   caveman_active     .agents/knowledge/patterns/…   full    YES
   pristine           .agents/skills/foo/SKILL.md    —       —
   full_active        .agents/pitfalls-live.md       full    no
   ```

### `/caveman refresh [path...]`

1. Resolve targets
2. For each file where `needs_recompress()` is True:
   - Recompress from `FILE.full.md` at the level recorded in `.caveman-state.json`
   - Validate and update `FILE.md`
3. This is the command to run after editing `FILE.full.md` content

---

## Compression Sub-Agent

Spawn a `general-purpose` sub-agent via the `task` tool:

```
task(
    agent_type: "general-purpose",
    model: "<caveman.model from config, default claude-sonnet-4.6>",
    name: "caveman-compress",
    description: "Compress markdown file",
    prompt: <see below>
)
```

### Compression Prompt

```markdown
You are compressing a markdown file into caveman format.
Return ONLY the compressed markdown. No explanation. No outer fence.

## Compression Level: {level}

### Level Rules

**lite**: Drop filler words (just/really/basically/actually/simply), hedging
(it might be worth, you could consider), pleasantries (sure/certainly/of course).
Keep articles and full sentences. Professional but tight.

**full** (default): Drop articles (a/an/the). Fragments OK. Short synonyms
(big not extensive, fix not "implement a solution for"). Technical terms exact.
Pattern: [thing] [action] [reason]. [next step].

**ultra**: Abbreviate common prose words (DB/auth/config/req/res/fn/impl).
Strip conjunctions. Arrows for causality (X → Y). One word when one word enough.
Code symbols, function names, API names, error strings: NEVER abbreviate.

### STRICT RULES — MUST FOLLOW

1. **Frontmatter** — The YAML `---` block at the start of the file must be
   returned EXACTLY as-is. Do not compress the `name:` or `description:` fields.
   Copy them byte-for-byte.

2. **Code blocks** — Everything inside ``` fences must be returned EXACTLY.
   Do not remove comments, spacing, reorder lines, shorten commands.

3. **Inline code** — Everything inside `backticks` must be preserved EXACTLY.

4. **URLs and links** — All URLs, markdown links [text](url), and file paths
   must be preserved EXACTLY.

5. **Headings** — All markdown headings (# through ######) must keep their
   exact text. Compress only the body text below headings.

6. **Tables** — Keep table structure. Compress cell text only.

7. **Bullet hierarchy** — Keep nesting levels. Compress bullet text.

8. **Technical terms** — Library names, API names, protocols, algorithms,
   function names, type names, config keys: preserve exactly.

9. **Proper nouns** — Project names, people, companies: preserve exactly.

10. **Numbers** — Dates, versions, numeric values, thresholds: preserve exactly.

## FILE TO COMPRESS:

{file_content}
```

### Fix Prompt (on validation failure)

```markdown
You are fixing a caveman-compressed markdown file. Specific validation errors were found.

CRITICAL: Do NOT recompress or rephrase. ONLY fix the listed errors.
The ORIGINAL is reference only (to restore missing content).
Preserve caveman style in all untouched sections.

ERRORS:
{error_list}

HOW TO FIX:
- Missing URL: find in ORIGINAL, restore exactly
- Code block mismatch: restore exact code block from ORIGINAL
- Heading mismatch: restore exact heading text from ORIGINAL
- Inline code lost: restore exact backtick content from ORIGINAL
- Frontmatter modified: restore exact frontmatter from ORIGINAL
- Do not touch sections not mentioned in errors

ORIGINAL (reference):
{full_content}

COMPRESSED (fix this):
{caveman_content}

Return ONLY the fixed file. No explanation.
```

---

## Content Update Protocol

When agent instructions need to be updated (pitfalls, knowledge, etc.) while
caveman is active:

### Updating Content

1. **Always edit `FILE.full.md`** — never edit `FILE.md` or `FILE.caveman.md` directly
2. After editing, run `/caveman refresh` to regenerate the caveman variant
3. The refresh reads from `FILE.full.md`, recompresses, and updates both
   `FILE.caveman.md` and `FILE.md`

### Orchestrator Integration

When the orchestrator appends to `pitfalls-live.md` or updates knowledge files:

1. Check if `pitfalls-live.full.md` exists → if yes, edit that instead
2. After editing, call `/caveman refresh .agents/pitfalls-live.md`
3. If no `.full.md` exists → file is in pristine state, edit `FILE.md` directly

**Rule:** Any code that writes to a managed markdown file should check for
the `.full.md` sibling first. The helper:

```bash
python3 -c "
from scripts.state import detect_file_state, sibling_paths, FileState
from pathlib import Path
p = Path('FILE.md')
st = detect_file_state(p)
full, _ = sibling_paths(p)
target = str(full) if st != FileState.PRISTINE and full.exists() else str(p)
print(target)
"
```

prints the path that should be edited.

---

## Directory Behavior

| Directory | Auto-recurse | When processed |
|-----------|-------------|----------------|
| `.agents/knowledge/` | Yes | Default (no path needed) |
| `.agents/pitfalls.md` | — | Default (no path needed) |
| `.agents/pitfalls-live.md` | — | Default (no path needed) |
| `.agents/skills/*/` | **No** | Only when explicitly named |
| `.agents/implement.md` | — | Only when explicitly named |
| `.agents/reviewer.md` | — | Only when explicitly named |
| `.agents/orchestrator.md` | — | Only when explicitly named |
| `docs/` | Yes | Only when explicitly named |

**Rationale:** Skill files (SKILL.md) contain `description:` frontmatter that
Copilot CLI uses for skill matching. Compressing them requires care — the
frontmatter is locked, but body compression may affect keyword matching.
Process skills explicitly after verifying the compressed version still triggers
correctly.

---

## Validation Pipeline

After each compression, the Python validator checks:

| Check | Severity | What it catches |
|-------|----------|----------------|
| Frontmatter | Error | YAML `---` block modified or lost |
| Headings | Error (count), Warning (text) | Heading count mismatch, text changes |
| Code blocks | Error | Fenced code modified |
| URLs | Error | URLs lost or added |
| Inline code | Error (lost), Warning (added) | Backtick content changed |
| File paths | Warning | Path references lost |
| Bullets | Warning | Bullet count changed >15% |
| Line refs | Warning | File:line references shifted |

**Errors** trigger the fix sub-agent. **Warnings** are reported but don't block.

Run validation manually:
```bash
cd .agents/skills/caveman
python3 -m scripts validate FILE.full.md FILE.caveman.md
```

---

## Reference Integrity

After compression, check cross-file references:
```bash
cd .agents/skills/caveman
python3 -m scripts refs .agents/
```

Reports:
- Broken `#anchor` links (heading slugs changed)
- Missing file targets
- Heading drift between full/caveman variants

**Fix strategy:** If headings must stay identical (they should per validation
rules), broken anchors indicate a validation failure. Re-run compression
with stricter heading preservation.

---

## `/caveman help` — Interactive Guide

When the user invokes `/caveman help`, provide a contextual, interactive guide
tailored to their project. The help is not a static wall of text — it scans the
actual files and shows what would happen.

### Trigger Patterns

Any of these should activate the help flow:

- `/caveman help`
- `/caveman help me understand how to proceed with my instructions/skills`
- `/caveman help <topic>` — where topic is `levels`, `config`, `workflow`, `skills`, `updating`
- `/caveman help <path>` — show what would happen to a specific file or directory

### Help Flow

#### Step 1: Scan the project

Run the state listing on the agent-managed directories:

```bash
cd .agents/skills/caveman
python3 -m scripts list ../../         # .agents/ root
python3 -m scripts list ../../../docs/ # docs/ if it exists
```

Count files by state: how many pristine, caveman_active, full_active, drifted.

#### Step 2: Show the situation

Present a concise overview:

```
📊 Caveman Status for <project-name>

  Files found:     24 markdown files across .agents/ and docs/
  Never touched:   20 (pristine)
  Caveman active:   3 (full level, no drift)
  Full restored:    1
  Needs refresh:    0

  Config: .agents/config/agent-rules.yaml
    default_level: full
    model: claude-sonnet-4.6
    default_targets: .agents/knowledge/, .agents/pitfalls.md, .agents/pitfalls-live.md
    explicit_targets: .agents/skills/
```

#### Step 3: Answer the specific question

If the user asked about a **topic**, respond with the relevant section:

| Topic | What to explain |
|-------|----------------|
| `levels` | Show the three levels with before/after examples from the user's OWN files — pick a paragraph from a pristine file and show what `lite`, `full`, and `ultra` would produce |
| `config` | Show current `caveman:` config from `agent-rules.yaml`, explain each field, suggest adjustments |
| `workflow` | Walk through the typical flow: first compress → edit full → refresh → restore |
| `skills` | Explain why skills are `explicit_targets`, show how to safely compress a SKILL.md, warn about frontmatter |
| `updating` | Explain the edit-full-then-refresh pattern, show the helper command for finding the right edit target |

If the user asked about a **specific path**, show:

1. Current state of that file (`pristine`/`caveman_active`/`full_active`)
2. A **dry-run preview**: take the first ~500 chars of prose from the file, show
   what `full`-level compression would look like (generate a short example inline,
   don't actually compress)
3. What command to run: `/caveman full <path>`
4. What files will be created: `FILE.full.md`, `FILE.caveman.md`

#### Step 4: Suggest next steps

Based on the scan, recommend an action:

- If everything is pristine: *"Ready to compress. Run `/caveman` to compress default targets, or `/caveman full .agents/skills/<name>/SKILL.md` for a specific skill."*
- If some files are drifted: *"N files have been updated since last compression. Run `/caveman refresh` to bring them in sync."*
- If caveman is active and user seems confused: *"Caveman is active on N files. To see the full versions, check the `.full.md` siblings. To restore everything to full prose: `/caveman normal`."*

### Example Help Interaction

**User:** `/caveman help me to understand how to proceed with my instructions/skills`

**Agent response:**

```
📊 Caveman Status for myproject

  .agents/skills/myproject/SKILL.md  — pristine (4.2 KB, ~1100 tokens)
  .agents/implement.md               — pristine (2.1 KB, ~550 tokens)
  .agents/reviewer.md                — pristine (1.8 KB, ~470 tokens)
  .agents/orchestrator.md            — pristine (5.6 KB, ~1470 tokens)
  .agents/pitfalls.md                — pristine (1.2 KB, ~310 tokens)
  .agents/pitfalls-live.md           — pristine (0.4 KB, ~100 tokens)
  .agents/knowledge/ (6 files)       — all pristine (8.3 KB total, ~2180 tokens)

  Total input tokens (est.): ~6,180
  After full compression (est.): ~1,850–2,470 (60–70% reduction)

Here's how to proceed:

1. **Start with knowledge + pitfalls** (safe, auto-recursed):
   /caveman
   This compresses .agents/knowledge/ and pitfalls files — the default targets.

2. **Then compress your project skill** (explicit, review after):
   /caveman full .agents/skills/myproject/SKILL.md
   Check that /skills still lists your skill with the right description.

3. **Optionally compress worker/reviewer instructions** (explicit):
   /caveman full .agents/implement.md .agents/reviewer.md .agents/orchestrator.md

4. **Later, when you update pitfalls or knowledge:**
   Edit the .full.md version, then run:
   /caveman refresh

Example — what "full" level does to your SKILL.md intro:

  BEFORE: "You are an implementation worker for myproject. You receive a task
           specification and produce source code and tests."
  AFTER:  "Implementation worker for myproject. Receive task spec, produce
           source code + tests."

Frontmatter (name, description) stays untouched. Code blocks stay untouched.
Only prose gets compressed.
```

### Help for Unknown Commands

If the user types `/caveman <something>` that doesn't match any known subcommand
(`help`, `normal`, `status`, `refresh`, or a level name), treat it as a help request:

```
Unknown subcommand: "<something>"

Available commands:
  /caveman [lite|full|ultra] [path...]  — Compress files
  /caveman normal [path...]             — Restore full versions
  /caveman status [path...]             — Show file states
  /caveman refresh [path...]            — Recompress drifted files
  /caveman help [topic|path]            — Interactive guide

Run /caveman help for a full walkthrough.
```

---

## Examples

### Compress default targets at full level
```
/caveman
```

### Compress at ultra level with explicit paths
```
/caveman ultra .agents/knowledge/ .agents/pitfalls.md
```

### Compress a specific skill
```
/caveman full .agents/skills/myproject/SKILL.md
```

### Restore all to full prose
```
/caveman normal
```

### Check what's compressed
```
/caveman status
```

### Refresh after editing full versions
```
/caveman refresh
```

---

## Boundaries

- **ONLY** compress `.md`, `.txt`, `.rst`, `.typ` files (natural language)
- **NEVER** modify `.py`, `.js`, `.ts`, `.json`, `.yaml`, `.yml`, `.toml`, code files
- **NEVER** compress `FILE.full.md` or `FILE.caveman.md` directly
- **NEVER** hand-edit `FILE.caveman.md` — it is always regenerated
- Frontmatter (`---` block) is **read-only** during compression
- Code blocks and inline code are **read-only** during compression
- `.caveman-state.json` files should be in `.gitignore` (they are local state)

---

## What to Gitignore

Add to `.gitignore`:
```
# Caveman local state
**/.caveman-state.json
```

**Do commit** both `.full.md` and `.caveman.md` variants — they are project assets.
The active `FILE.md` should also be committed (it's always a copy of one variant).
