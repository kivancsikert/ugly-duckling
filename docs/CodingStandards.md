# Coding Standards

## Formatting

- Follow `.editorconfig`: LF line endings, UTF-8, 4-space indent (2 for JSON/YAML/Markdown/JS/TS).
- C++ is formatted with a WebKit-flavored `.clang-format`: attached braces, left-aligned pointers (`int* p`), no single-line functions.
- Run `clang-format` before committing; CI will reject misformatted code.

## Static Analysis

- Static analysis is via `.clang-tidy` with warnings-as-errors.
- The clang compile DB (`<build-dir>/clang/compile_commands.json`) is regenerated automatically at the end of every `idf.py build` (`<build-dir>` is whatever `-B` was passed — `build-carrot`/`build-spinach` when building via `tools/build.sh`, which defaults to `build-carrot`).
- Run tidy manually:

  ```bash
  run-clang-tidy -p build-carrot/clang -header-filter="$(pwd)/(main|components)/" <file>
  # or, if you built spinach:
  run-clang-tidy -p build-spinach/clang -header-filter="$(pwd)/(main|components)/" <file>
  ```

- CI runs clang-tidy on every build.

## Naming Conventions

| Kind | Convention | Example |
| ---- | ---------- | ------- |
| Types (classes, structs, enums) | PascalCase | `UglyDucklingMk6`, `PlotController` |
| Functions and methods | camelCase | `startDevice()`, `getTemperature()` |
| Constants and macros | UPPER_SNAKE | `UD_GEN`, `UD_DEBUG` |
| Namespaces | compact, lowercase | `cornucopia::ugly_duckling::kernel` |

## Markdown

- Keep headings sequential (no skipped levels).
- Lists must be properly indented; blank line before lists and headings.
- Wrap all code examples in fenced code blocks with a language tag.
- No trailing spaces; insert a final newline.
- Follow markdownlint defaults.
