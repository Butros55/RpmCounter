"""
PlatformIO Post-Script to configure mklittlefs tool path.

This script runs AFTER platform initialization to override MKFSTOOL.
Must run WITHOUT pre: prefix (default is post).

The data directory is set by littlefs_datadir.py (pre: script).
"""

Import("env")
import os
import glob
import sys

# For Windows: ensure mklittlefs is found in PATH
# The pioarduino platform uses MKFSTOOL="mklittlefs" without full path
# so we need to override it with the full path

packages_dir = os.path.join(os.path.expanduser("~"), ".platformio", "packages")

# Look for mklittlefs in various possible package locations (pioarduino has different versions)
mklittlefs_dirs = [
    os.path.join(packages_dir, "tool-mklittlefs"),
]

# Also check glob patterns for versioned packages
for pattern in [os.path.join(packages_dir, "tool-mklittlefs@*")]:
    mklittlefs_dirs.extend(glob.glob(pattern))

# Also check for mklittlefs4 (used by some pioarduino versions)
mklittlefs_dirs.append(os.path.join(packages_dir, "tool-mklittlefs4"))

# Find the first directory that contains mklittlefs.exe (Windows) or mklittlefs
tool_exe = "mklittlefs.exe" if sys.platform == "win32" else "mklittlefs"
found_tool_dir = None
found_tool_path = None

for tool_dir in mklittlefs_dirs:
    if not os.path.isdir(tool_dir):
        continue
    tool_path = os.path.join(tool_dir, tool_exe)
    if os.path.isfile(tool_path):
        found_tool_dir = tool_dir
        found_tool_path = tool_path
        break

if found_tool_path:
    print(f"[LittleFS Tool] Found mklittlefs at: {found_tool_path}")
    
    # Override MKFSTOOL with full path (this runs AFTER platform set it to just "mklittlefs")
    env.Replace(MKFSTOOL=found_tool_path)
    env["MKFSTOOL"] = found_tool_path
    
    # Also set MKSPIFFSTOOL for compatibility
    env.Replace(MKSPIFFSTOOL=found_tool_path)
    env["MKSPIFFSTOOL"] = found_tool_path
    
    # Also add to PATH just in case
    current_path = os.environ.get("PATH", "")
    if found_tool_dir not in current_path:
        os.environ["PATH"] = found_tool_dir + os.pathsep + current_path
    if "ENV" in env:
        env_path = env["ENV"].get("PATH", "")
        if found_tool_dir not in env_path:
            env["ENV"]["PATH"] = found_tool_dir + os.pathsep + env_path
    
else:
    # Try to get it from the platform package manager
    try:
        platform = env.PioPlatform()
        for pkg_name in ["tool-mklittlefs", "tool-mklittlefs4"]:
            try:
                pkg_dir = platform.get_package_dir(pkg_name)
                if pkg_dir:
                    tool_path = os.path.join(pkg_dir, tool_exe)
                    if os.path.isfile(tool_path):
                        print(f"[LittleFS Tool] Found mklittlefs via platform: {tool_path}")
                        os.environ["PATH"] = pkg_dir + os.pathsep + os.environ.get("PATH", "")
                        env.Replace(MKFSTOOL=tool_path)
                        break
            except:
                pass
    except Exception as e:
        print(f"[LittleFS Tool] Warning: Could not locate mklittlefs: {e}")

print(f"[LittleFS Tool] MKFSTOOL = {env.get('MKFSTOOL', 'not set')}")
print(f"[LittleFS Tool] PROJECT_DATA_DIR = {env.subst('$PROJECT_DATA_DIR')}")
