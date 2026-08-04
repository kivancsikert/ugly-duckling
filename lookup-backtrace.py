#!/usr/bin/env python3

import argparse
import re
import subprocess
import sys

HEX_PATTERN = r"0x[0-9a-fA-F]+"

DEFAULT_ELF = "build/ugly-duckling.elf"

TOOLCHAINS = {
    "xtensa": "xtensa-esp32-elf-addr2line",
    "esp32": "xtensa-esp32-elf-addr2line",
    "esp32s3": "xtensa-esp32s3-elf-addr2line",
    "esp32c6": "riscv32-esp-elf-addr2line",
}

def guess_addr2line(elf_path):
    """Pick the right addr2line binary based on the target hinted at by the ELF path."""
    lowered = elf_path.lower()
    if "carrot" in lowered or "esp32c6" in lowered:
        return TOOLCHAINS["esp32c6"]
    if "spinach" in lowered or "esp32s3" in lowered:
        return TOOLCHAINS["esp32s3"]
    return TOOLCHAINS["esp32"]

def parse_and_run_addr2line(lines, elf_path, addr2line, inlines):
    addresses = []
    for line in lines:
        addresses.extend(re.findall(HEX_PATTERN, line))
    if addresses:
        process_addresses(addresses, elf_path, addr2line, inlines)

def process_addresses(addresses, elf_path, addr2line, inlines):
    cmd = [
        addr2line,
        "--pretty-print",
        "--demangle",
        "--basenames",
        "--functions",
    ]
    if inlines:
        cmd.append("--inlines")
    cmd += ["--exe", elf_path] + addresses
    try:
        result = subprocess.run(cmd, text=True, capture_output=True)
        if result.returncode != 0:
            print(result.stderr, file=sys.stderr, end="")
        for line in result.stdout.strip().splitlines():
            print(f"  -- ${line}")
    except FileNotFoundError:
        print(f"Error: '{addr2line}' not found on PATH. Activate the matching IDF toolchain "
              f"(tools/activate_idf.sh carrot|spinach) or pass --addr2line explicitly.",
              file=sys.stderr)
    except Exception as e:
        print(f"Error running addr2line: {e}")

def main():
    parser = argparse.ArgumentParser(description="Resolve addresses in a pasted backtrace to source locations.")
    parser.add_argument("elf", nargs="?", default=DEFAULT_ELF,
                         help=f"Path to the ELF to symbolize against (default: {DEFAULT_ELF}). "
                              "Use the exact ELF that produced the crash log, not a fresh local build.")
    parser.add_argument("--addr2line", default=None,
                         help="addr2line binary to use (default: guessed from the ELF path — "
                              "'carrot'/'esp32c6' -> riscv32-esp-elf-addr2line, "
                              "'spinach'/'esp32s3' -> xtensa-esp32s3-elf-addr2line, "
                              "else xtensa-esp32-elf-addr2line)")
    parser.add_argument("--no-inlines", action="store_true",
                         help="Don't show inlined call chains (shown by default — inlined frames "
                              "are frequently exactly where the crash is)")
    args = parser.parse_args()

    addr2line = args.addr2line or guess_addr2line(args.elf)

    print(f"Using {addr2line} -e {args.elf}", file=sys.stderr)
    print("Paste backtrace here:", file=sys.stderr)
    lines = []
    try:
        for line in sys.stdin:
            lines.append(line)
    except KeyboardInterrupt:
        print("\nTerminated by user.")
    except EOFError:
        print("\n")
    parse_and_run_addr2line(lines, args.elf, addr2line, inlines=not args.no_inlines)

if __name__ == "__main__":
    main()
