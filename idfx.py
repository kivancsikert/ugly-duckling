#!/usr/bin/env python

import os
import sys
import subprocess


def determine_idf_target(gen):
    if gen == "MK4":
        return "esp32s2"
    elif gen in ["MK5", "MK6", "MK7"]:
        return "esp32s3"
    else:
        print(f"Error: Unrecognized version '{gen}'")
        sys.exit(1)


ud_gen = os.getenv("UD_GEN")
if not ud_gen:
    print("Error: UD_GEN environment variable is not set.", file=sys.stderr)
    sys.exit(1)
ud_debug = os.getenv("UD_DEBUG") == "1"

idf_path = os.getenv("IDF_PATH")
if not idf_path:
    print("Error: IDF_PATH environment variable is not set.", file=sys.stderr)
    sys.exit(1)

# TODO Remove this hack
# Delete the component hash file because it somehow gets out-of-date all the time
arduino_component_hash = r"managed_components/espressif__arduino-esp32/.component_hash"
if os.path.exists(arduino_component_hash):
    os.remove(arduino_component_hash)

build_dir = f"build/{ud_gen.lower()}-{'debug' if ud_debug else 'release'}"

idf_py = f"{idf_path}/tools/idf.py"
idf_args = sys.argv[1:]
ud_args = [
    "-B",
    build_dir,
    f"-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.{ud_gen.lower()}.defaults",
    f"-DSDKCONFIG=sdkconfig.{ud_gen.lower()}",
    # This will be available in ESP-IDF 5.1+
    # f"-DDEPENDENCIES_LOCK=dependencies.{ud_gen.lower()}.lock",
]

idf_args = ud_args + idf_args

env = {
    **os.environ,
    "IDF_TARGET": determine_idf_target(ud_gen),
    "UD_GEN": ud_gen,
    "UD_DEBUG": "1" if ud_debug else "0",
}

print(f"Running: {idf_py}")
print(f"  Arguments: {idf_args}")
print(f"  Build directory: {build_dir}")
print(f"  Environment: IDF_TARGET={env['IDF_TARGET']}")
try:
    result = subprocess.run([sys.executable, idf_py] + idf_args, check=True, env=env)
    sys.exit(result.returncode)
except subprocess.CalledProcessError as e:
    print(f"Error: idf.py failed with return code {e.returncode}", file=sys.stderr)
    sys.exit(e.returncode)
