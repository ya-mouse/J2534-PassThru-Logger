# Task Template: Write Tests
# Template version: 0.1

Use this template when spawning a worker specifically to write tests for
already-implemented code. Used when a reviewer identifies insufficient test
coverage, or for adding fuzz/stress tests.

---

## Prompt Structure

```
You are writing tests for j2534-logger.

## Context
{paste relevant sections of .agents/implement.md — Testing section}

## Code Under Test
{paste the source file(s) that need tests}

## Existing Tests (if any)
{paste existing test files — the worker should ADD to these, not replace}

## Reviewer's Coverage Gaps
{paste the reviewer's feedback about missing tests}

## Task
- **Task ID:** {task_id}-tests
- **Source file:** {source path}
- **Test files to create/update:** {test file paths}

## Required Test Categories

### 1. Happy Path
- Normal usage patterns
- Common input combinations
- Expected output verification

### 2. Edge Cases
- Empty input / collections
- Maximum/minimum values
- Single-element collections
- Deeply nested structures (if applicable)
- Unicode/special characters (if string-handling code)

### 3. Error Paths
- Invalid input that should produce specific errors
- Type mismatches
- Missing required fields
- Malformed input (if parsing code)

### 4. Diagnostic Verification
- Each error path: verify the diagnostic message contains expected text
- Verify source locations are correct (if applicable)
- Verify suggestions are helpful

### 5. Stress / Fuzz Targets (if applicable)
- Property-based tests: encode → decode roundtrip
- Adversarial input: random bytes, deeply nested, very large
- Timeout/resource tests: ensure bounded execution

## Output
Create the test files using the project's test framework and conventions.
Group related tests in the same file. Use separate files for:
- Basic tests vs edge cases
- Error tests vs happy path
- Fuzz targets
```
