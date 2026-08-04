#!/usr/bin/env python3
"""
Generate an NVS partition binary from config JSON files.

Usage: gen_config_nvs.py <config-dir> <output.bin> <size>
"""

import base64
import csv
import io
import json
import os
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <config-dir> <output.bin> <size>", file=sys.stderr)
        sys.exit(1)

    config_dir = sys.argv[1]
    output_file = sys.argv[2]
    partition_size = sys.argv[3]

    network_config_file = os.path.join(config_dir, "network-config.json")

    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        print("Error: IDF_PATH environment variable is not set", file=sys.stderr)
        sys.exit(1)

    with open(network_config_file) as f:
        network_config = json.load(f)

    # Generate NVS CSV with compact JSON values. Device/function configuration is deliberately not
    # seeded here: a freshly generated partition has no confirmed slot, so the device boots exactly
    # like an empty slot (defaults, no functions) and reconciles the full set from the server via the
    # empty-SYNC bootstrap (docs/specs/config-reconciliation.md, "Migration" -> "A missing/absent
    # confirmed slot is the one bootstrap path").
    csv_content = io.StringIO()
    writer = csv.writer(csv_content, quoting=csv.QUOTE_MINIMAL)
    writer.writerow(["key", "type", "encoding", "value"])
    writer.writerow(["config", "namespace", "", ""])
    writer.writerow(["network-config", "data", "base64", base64.b64encode(json.dumps(network_config, separators=(',', ':')).encode()).decode()])

    # Write temporary CSV file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.csv', delete=False) as f:
        f.write(csv_content.getvalue())
        csv_file = f.name

    try:
        nvs_gen = os.path.join(idf_path, "components", "nvs_flash", "nvs_partition_generator", "nvs_partition_gen.py")
        result = subprocess.run(
            [sys.executable, nvs_gen, "generate", csv_file, output_file, partition_size],
            capture_output=True, text=True
        )
        if result.returncode != 0:
            print(result.stdout, end="")
            print(result.stderr, end="", file=sys.stderr)
            sys.exit(result.returncode)
    finally:
        os.unlink(csv_file)


if __name__ == "__main__":
    main()
