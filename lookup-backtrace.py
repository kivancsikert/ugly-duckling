#!/usr/bin/env python3

import re
import subprocess
import sys

# Define regex patterns for the two formats
PATTERN1 = r"caller ((?:0x[0-9a-fA-F]+:)+0x[0-9a-fA-F]+)"
PATTERN2 = r"Backtrace: ((?:0x[0-9a-fA-F]+:0x[0-9a-fA-F]+ ?)+)"

def parse_and_run_addr2line(line):
    # Print the input line
    print(f"Input: {line.strip()}")

    # Check for the first format
    match1 = re.search(PATTERN1, line)
    if match1:
        addresses = match1.group(1).split(":")
        process_addresses(addresses)

    # Check for the second format
    match2 = re.search(PATTERN2, line)
    if match2:
        pairs = match2.group(1).split()
        addresses = [pair.split(":")[0] for pair in pairs]
        process_addresses(addresses)

def process_addresses(addresses):
    # Prepare the addr2line command
    cmd = ["xtensa-esp32-elf-addr2line", "-f", "-e", "build/ugly-duckling.elf"] + addresses
    try:
        # Run the addr2line command
        result = subprocess.run(cmd, text=True, capture_output=True)
        # Split output into lines
        lines = result.stdout.strip().splitlines()
        # Process lines in pairs
        for i in range(0, len(lines), 2):
            function_name = lines[i].strip() if i < len(lines) else "unknown"
            source_info = lines[i + 1].strip() if i + 1 < len(lines) else "unknown"
            print(f"  -- {source_info} -- {function_name}")
    except Exception as e:
        print(f"Error running addr2line: {e}")

def main():
    try:
        # Read lines from standard input
        for line in sys.stdin:
            parse_and_run_addr2line(line)
    except KeyboardInterrupt:
        print("\nTerminated by user.")
    except EOFError:
        print("\n")

if __name__ == "__main__":
    main()
