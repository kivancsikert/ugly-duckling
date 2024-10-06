#!/usr/bin/env python3

import os
import subprocess
import sys

# Define the Docker image name
IMAGE_NAME = "espressif/idf:v4.4.8"

# Get environment variables (or set defaults if not present)
UD_GEN = os.getenv("UD_GEN")
if not UD_GEN:
    print("Error: UD_GEN environment variable is not set.", file=sys.stderr)
    sys.exit(1)
UD_DEBUG = os.getenv("UD_DEBUG", "0")

# Get the current working directory
current_directory = os.getcwd()

# Build the Docker command
docker_command = [
    "docker", "run", "--rm", "-it",
    "-v", f"{current_directory}:/workspace",
    "-w", "/workspace",
    "-e", f"UD_GEN={UD_GEN}",
    "-e", f"UD_DEBUG={UD_DEBUG}",
    IMAGE_NAME
]

# Add any additional arguments passed to the Python script
docker_command += sys.argv[1:]

# Run the Docker command
try:
    subprocess.run(docker_command, check=True)
except subprocess.CalledProcessError as e:
    print(f"Error: {e}")
    sys.exit(1)
