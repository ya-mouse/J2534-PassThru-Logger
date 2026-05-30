---
description: "AI agent instructions for j2534-logger development"
---

# j2534-logger — Copilot Instructions

<!-- ═══════════════════════════════════════════════════════════════════════
     This file is the primary entry point for AI agents working on this
     project. It provides project identity, architecture, build/test/debug
     workflows, coding conventions, and references to deeper documentation.

     Populated by /skill-init (basics) and /skill-discover (detailed context).
     Sections marked <!-- FILL: ... --> need content.
     ═══════════════════════════════════════════════════════════════════════ -->

## Project Identity

**j2534-logger** — Windows proxy DLL that sits between a real J2534 device and a client for monitoring events and logging PassThru API calls

- **Primary language:** C++ (.cpp)
- **Status:** Active development

<!-- FILL: Add backend/platform targets, key differentiators -->

---

## Core Design Philosophy

<!-- FILL: 3-5 principles that guide all development decisions.
     These help agents understand WHY code should be written a certain way.

     Example:
     1. **Local Reasoning** — any block understandable without global context
     2. **Explicit Over Implicit** — costs are syntactically visible
     3. **No Undefined Behavior** — every operation has defined semantics
-->

---

## Key Features

<!-- FILL: Summarize the main features/capabilities of the project.
     For each, briefly describe the approach and reference deeper docs.

     Example:
     ### Error Handling: ?? Operator
     - Concise error propagation
     - See DEC-002 for design rationale
-->

---

## Architecture

<!-- FILL: High-level architecture diagram and description.
     Include pipeline/data flow if applicable.

     Example:
     ```
     Source → Lexer → Parser → AST → Checker → IR → Codegen → Output
     ```
-->

### Key Design Notes

<!-- FILL: Architectural decisions that affect daily development -->

---

## Build, Test & Debug

### Building

```bash
msbuild "J2534 PassThru Logger.sln" /p:Configuration=Release
```

<!-- FILL: Prerequisites, environment setup, PATH configuration -->

### Testing

No test suite configured.

<!-- FILL: Test conventions, how to run subsets, expected output format -->

### Debugging

**Temporary files:** Use `tmp/` for scratch files, not `/tmp`.

**IMPORTANT: Never commit the `tmp/` directory.**

<!-- FILL: Debugging workflows specific to this project.

     Example sections:
     #### Quick crash diagnosis
     ```bash
     <debugger command>
     ```

     #### Build verification
     <multi-stage or cross-platform verification steps>

     #### Common crash patterns
     | Symptom | Likely Cause |
     |---------|-------------|
     | ... | ... |
-->

---

## Project Structure

```
./
├── PassThruLogger/              # C++ proxy DLL (Win32)
├── PassThruLoggerControl/       # C# WinForms control UI
├── SampleClient/                # C# sample client
├── docs/                        # Documentation
├── .agents/                     # Agent rules, skills & config
│   ├── VERSION                  # Template version
│   ├── config/
│   │   └── agent-rules.yaml     # Agent configuration
│   ├── orchestrator.md          # Execution loop, spawning, escalation
│   ├── implement.md             # Worker coding standards
│   ├── reviewer.md              # Review checklist
│   ├── pitfalls.md              # Knowledge base of past mistakes
│   ├── pitfalls-live.md         # Live issue tracker
│   ├── task-templates/          # Prompt templates
│   └── skills/
│       ├── j2534-logger/
│       │   └── SKILL.md         # Domain knowledge reference
│       ├── skill-init/SKILL.md  # /skill-init command
│       ├── skill-discover/SKILL.md  # /skill-discover command
│       └── skill-sync/SKILL.md  # /skill-sync command
└── .github/
    └── copilot-instructions.md  # THIS FILE
```

<!-- FILL: Add project-specific directories, key files, and their purposes -->

---

## Coding Conventions

### Naming

- **Functions:** camelCase
- **Types:** PascalCase
- **Constants:** SCREAMING_SNAKE_CASE
- **Modules:** PascalCase

### Error Handling

Return J2534 error codes (STATUS_NOERROR, ERR_*). Log errors before returning.

<!-- FILL: Show code examples of correct error handling -->

### Testing Patterns

No test framework configured.

<!-- FILL: Show code examples of correct test patterns -->

### Comments

Explain 'why' not 'what', minimal comments

---

## Design Decisions Reference

<!-- FILL: Table of design documents/decisions with links.

     Example:
     | Decision | Topic | When to Check |
     |----------|-------|---------------|
     | DEC-001  | Variables | Using variables, mutable bindings |
     | ADR-005  | Error strategy | Error handling code |
-->

---

## What to Avoid

<!-- FILL: Project-specific anti-patterns.

     Example:
     1. **Implicit conversions** — always use explicit casting
     2. **Null values** — use Option<T> instead
     3. **Global mutable state** — pass state explicitly
-->

---

## Entry Points for AI Agents

1. **Understanding the project:** Read this file, then the SKILL.md
2. **Implementing a feature:** Find relevant specs/docs, read SKILL.md for conventions, generate code following patterns above
3. **Orchestrating work:** Use the orchestrator skill to manage workers and reviewers
