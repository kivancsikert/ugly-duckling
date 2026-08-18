#!/bin/bash
# Runs the ESP-IDF clang-format.
# Used by VS Code (C_Cpp.clang_format_path) and pre-commit hooks
# to ensure everyone uses the same version as CI.

CF="$(command -v clang-format 2>/dev/null)"

if [ -z "$CF" ]; then
    # VS Code doesn't source activate_idf.sh, so clang-format won't be on
    # PATH. Fall back to the conventional ESP-IDF tools location.
    # shellcheck disable=SC2012
    CF="$(ls -1d "$HOME"/.espressif/tools/esp-clang/*/esp-clang/bin/clang-format 2>/dev/null | sort -V | tail -1)"
fi

if [ -z "$CF" ]; then
    echo "Error: clang-format not found. Source tools/activate_idf.sh or install ESP-IDF tools." >&2
    exit 1
fi

exec "$CF" "$@"
