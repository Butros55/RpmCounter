"""
PlatformIO Pre-Script to set LittleFS data directory.

This script runs BEFORE platform initialization to set the data directory.
Must run as pre: script.
"""

Import("env")
import os

# Set filesystem data directory to 'webserver'
project_dir = env.subst("$PROJECT_DIR")
webserver_dir = os.path.join(project_dir, "webserver")

# Override the default data directory - this must happen before platform init
env.Replace(PROJECT_DATA_DIR=webserver_dir)
env["PROJECT_DATA_DIR"] = webserver_dir

print(f"[LittleFS DataDir] Set to: {webserver_dir}")
