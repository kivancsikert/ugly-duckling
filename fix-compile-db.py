#!/usr/bin/env python3

import json
import subprocess
import re
from pathlib import Path

UNSUPPORTED_FLAGS = [
    "-fanalyzer",
    "-fno-shrink-wrap",
    "-fno-tree-switch-conversion",
    "-fstrict-volatile-bitfields",
]

def run(cmd):
    return subprocess.check_output(cmd, shell=True, text=True).strip()

def get_sysroot():
    return run("xtensa-esp32-elf-g++ -print-sysroot").replace("\\", "/")

def get_gcc_version():
    output = run("xtensa-esp32-elf-g++ --version")
    match = re.search(r"\b\d+\.\d+\.\d+\b", output)
    if not match:
        raise RuntimeError("Could not parse GCC version")
    return match.group(0)

def patch_command(command, includes):
    tokens = command.split()
    tokens = [tok for tok in tokens if tok not in UNSUPPORTED_FLAGS]
    return tokens + [f"-I{inc}" for inc in includes]

def fix_compile_commands(input_path, output_path, sysroot, gcc_version):
    with open(input_path, "r", encoding="utf-8") as f:
        commands = json.load(f)

    include_dirs = [
        f"{sysroot}/include",
        f"{sysroot}/include/c++/{gcc_version}",
        f"{sysroot}/include/c++/{gcc_version}/xtensa-esp-elf",
    ]

    for entry in commands:
        if "command" in entry:
            original = entry["command"]
            entry["command"] = " ".join(patch_command(original, include_dirs))

    Path(output_path).parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(commands, f, indent=2)

    print(f"✅ Fixed compile_commands.json written to {output_path}")

# --- Main ---
if __name__ == "__main__":
    input_file = "build/compile_commands.json"
    output_file = "build/clang/compile_commands.json"

    sysroot = get_sysroot()
    version = get_gcc_version()

    fix_compile_commands(input_file, output_file, sysroot, version)
