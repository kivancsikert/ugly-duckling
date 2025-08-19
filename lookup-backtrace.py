#!/usr/bin/env python3

import re
import subprocess
import sys

HEX_PATTERN = r"0x[0-9a-fA-F]+"

def parse_and_run_addr2line(lines):
    addresses = []
    for line in lines:
        addresses.extend(re.findall(HEX_PATTERN, line))
    if addresses:
        process_addresses(addresses)

def process_addresses(addresses):
    cmd = [
        "xtensa-esp32-elf-addr2line",
        "--pretty-print",
        "--demangle",
        "--basenames",
        "--functions",
        "--exe",
        "build/ugly-duckling.elf"
    ] + addresses
    try:
        result = subprocess.run(cmd, text=True, capture_output=True)
        for line in result.stdout.strip().splitlines():
            print(f"  -- ${line}")
    except Exception as e:
        print(f"Error running addr2line: {e}")

def main():
    print("Paste backtrace here:", file=sys.stderr)
    lines = []
    try:
        # Read lines from standard input
        for line in sys.stdin:
            lines.append(line)
    except KeyboardInterrupt:
        print("\nTerminated by user.")
    except EOFError:
        print("\n")
    parse_and_run_addr2line(lines)

if __name__ == "__main__":
    main()
