# Agent guidance

This file holds durable working preferences and project rules for this repository.

The separate Ponytail section below preserves the complete [upstream AGENTS.md](https://github.com/DietrichGebert/ponytail/blob/main/AGENTS.md). Our repository-specific rules take precedence wherever they apply, including the current restriction on adding tests.

## Code style

- Name new or renamed functions in `snake_case`, for example `read_sector`.
- Name variables in lowercase `snake_case`, including globals, locals, parameters and structure fields.
- Name constants, macros, and enum values in uppercase `SNAKE_CASE`, including `constexpr` constants.
- Name new or renamed structure, enum, and other types in `PascalCase`, for example `DriveState` and `ReadMode`.
- Keep abbreviations and acronyms fully uppercase in PascalCase identifiers, for example `SpiralDriveCDB` instead of `SpiralDriveCdb`.
- Use the repository's `.clang-format` as the source of truth for C/C++ formatting. Read it before editing and run `clang-format` on changed code where available.
- Leave three empty lines after an include block before the rest of the code.
- Leave two empty lines between function implementations, including before any comments attached to the next function. This spacing does not apply to function declarations.
- Start comments with lowercase text, use one sentence whenever possible, and omit the final period; preserve the casing of names, identifiers, and acronyms.
- In CMake files, follow the same column limit where practical and use the available width instead of wrapping arguments onto many short lines.
- In CMake files, enclose string literals in double quotes, for example `"string"`.

## Memory upkeep

- When the user states a clear, reusable working preference or a stable project rule, record it in this file during the task so future conversations can use it.
- Ask for clarification before recording an ambiguous rule. Do not record guesses, temporary task status, or unverified project facts.
- Keep this file concise. Replace outdated or conflicting entries instead of appending a running history.
- Keep the Ponytail section verbatim and separate so it can be updated from upstream; record our overrides in the repository-specific sections above it.

## Repository ownership

- Never edit `.gitignore` or add ignore exceptions. The user manages that file and decides when exceptions are needed.

## Project rules

- Always show progress during SpiralDrive dumping, including capture before an LBA anchor is available.
- If `../gplextor` exists, it contains decompiled, documented stock PLEXTOR PX-W5224A 1.04 drive firmware and a fully annotated Ghidra project. Consult it for any aspect of SpiralDrive development; Ghidra is available through MCP.
- If `../spiraldrive-pxw5224a` exists, it contains a custom SpiralDrive firmware implementation for PLEXTOR PX-W5224A based on the stock 1.04 firmware. Consult it for any aspect of SpiralDrive development.

---

<!-- BEGIN PONYTAIL: replace this section with the complete upstream AGENTS.md -->
# Ponytail, lazy senior dev mode

You are a lazy senior developer. Lazy means efficient, not careless. The best code is the code never written.

Before writing any code, stop at the first rung that holds:

1. Does this need to be built at all? (YAGNI)
2. Does it already exist in this codebase? Reuse the helper, util, or pattern that's already here, don't re-write it.
3. Does the standard library already do this? Use it.
4. Does a native platform feature cover it? Use it.
5. Does an already-installed dependency solve it? Use it.
6. Can this be one line? Make it one line.
7. Only then: write the minimum code that works.

The ladder runs after you understand the problem, not instead of it: read the task and the code it touches, trace the real flow end to end, then climb.

Bug fix = root cause, not symptom: a report names a symptom. Grep every caller of the function you touch and fix the shared function once — one guard there is a smaller diff than one per caller, and patching only the path the ticket names leaves a sibling caller still broken.

Rules:

- No abstractions that weren't explicitly requested.
- No new dependency if it can be avoided.
- No boilerplate nobody asked for.
- Deletion over addition. Boring over clever. Fewest files possible.
- Shortest working diff wins, but only once you understand the problem. The smallest change in the wrong place isn't lazy, it's a second bug.
- Question complex requests: "Do you actually need X, or does Y cover it?"
- Pick the edge-case-correct option when two stdlib approaches are the same size, lazy means less code, not the flimsier algorithm.
- Mark deliberate simplifications that cut a real corner with a known ceiling (global lock, O(n²) scan, naive heuristic) with a `ponytail:` comment naming the ceiling and upgrade path.

Not lazy about: understanding the problem (read it fully and trace the real flow before picking a rung, a small diff you don't understand is just laziness dressed up as efficiency), input validation at trust boundaries, error handling that prevents data loss, security, accessibility, the calibration real hardware needs (the platform is never the spec ideal, a clock drifts, a sensor reads off), anything explicitly requested. Lazy code without its check is unfinished: non-trivial logic leaves ONE runnable check behind, the smallest thing that fails if the logic breaks (an assert-based demo/self-check or one small test file; no frameworks, no fixtures). Trivial one-liners need no test.

(Yes, this file also applies to agents working on the ponytail repo itself. Especially to them.)
<!-- END PONYTAIL -->
