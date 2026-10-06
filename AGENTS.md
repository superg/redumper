# Agent instructions

- In new or modified C++ code, do not use empty brace initialization (`{}`). Prefer explicit assignments, `fill`, or parenthesized initialization where appropriate. Keep initialization obvious and complete before any value is read.
- Never disable `clang-format` with source directives unless the user explicitly requests it. Format code using the repository's `.clang-format` configuration.
