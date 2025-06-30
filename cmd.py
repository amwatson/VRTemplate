#!/usr/bin/env python3
# Android build + dev cycle command runner for VR Template App
# Author: Amanda M. Watson (amwatson)

import os
import subprocess as sp
import sys

# ================
# Global constants
# ================

package = "com.amwatson.vrtemplate"
launch_activity = "com.amwatson.vrtemplate.MainActivity"
tag = "VrTemplate"

# ========================
# Global mutable variables
# ========================

has_executed_build = False

# ================
# Helper functions
# ================

def shell_cmd(cmd):
    print(f"{cmd}")
    try:
        sp.run(cmd.split(), check=True)
        return 0
    except sp.CalledProcessError as e:
        print(f"Command failed: {e}")
        return e.returncode

def adb_shell_cmd(cmd):
    return shell_cmd(f"adb shell {cmd}")

def check_submodules():
    sm_status = sp.run("git submodule status", stdout=sp.PIPE, stderr=sp.PIPE, shell=True, text=True)
    if sm_status.stderr.strip():
        print(f"Error checking submodules: {sm_status.stderr.strip()}")
        return 1

    if any(line.startswith('-') for line in sm_status.stdout.strip().splitlines()):
        print("Submodule(s) not found -- updating submodules...")
        update_error = shell_cmd("git submodule update --init --recursive")

        if update_error:
            print(f"Error updating submodules: {update_error}")
            return update_error

        print("Submodules updated successfully.")

    print("All submodules are up to date.")
    return 0

# ==================
# Available commands
# ==================

def start(_):
    return adb_shell_cmd(f"am start {package}/{launch_activity}")

def stop(_):
    return adb_shell_cmd(f"am force-stop {package}")

def install(build_config):
    return (check_submodules() == 0) and shell_cmd(f"./gradlew install{build_config}")

def uninstall(build_config):
    return shell_cmd(f"./gradlew uninstall{build_config}")

def build(build_config):
    global has_executed_build
    success = (check_submodules() == 0) and shell_cmd(f"./gradlew assemble{build_config}")
    if success == 0:
        has_executed_build = True
    return success

def clean(_):
    return shell_cmd("./gradlew clean")

def test(_):
    return shell_cmd("./gradlew connectedAndroidTest")

def native(build_config):
    return shell_cmd(f"./gradlew externalNativeBuild{build_config.capitalize()}")

def logcat(_):
    shell_script = r'''
adb logcat -s VrTemplate -s VrApi | while read -r line; do
    case "$line" in
        *VrApi*) color='\033[2;36m' ;; # VrApi - dim cyan
        *\ V\ *) color='\033[2;37m' ;;  # Verbose - dim gray
        *\ D\ *) color='\033[1;34m' ;;  # Debug - blue
        *\ I\ *) color='\033[1;32m' ;;  # Info - green
        *\ W\ *) color='\033[1;33m' ;;  # Warning - yellow
        *\ E\ *) color='\033[1;31m' ;;  # Error - red
        *\ F\ *) color='\033[1;41m' ;;  # Fatal - red background
        *) color='\033[0m' ;;           # Reset
    esac
    printf "${color}%s\033[0m\n" "$line"
done
'''
    adb_shell_cmd(f"logcat -c")
    print("Logcat started")
    return sp.call(shell_script, shell=True, executable="/bin/bash")


def devcycle(build_config):
    steps = [
        ("build", build),
        ("install", install),
        ("start", start),
        ("logcat", logcat),
    ]

    for name, func in steps:
        print(f"Running: {name}")
        if func(build_config) != 0:
            print(f"'{name}' failed. Aborting devcycle.")
            return 1
    return 0

def help(_=None):
    print("Available commands:\n")
    print("  build       → Compile the full app (Java + native)")
    print("  clean       → Wipe all Gradle build outputs")
    print("  install     → Install APK to device (requires build first)")
    print("  uninstall   → Uninstall the APK from device")
    print("  start       → Launch the VR app on device")
    print("  stop        → Force-stop the app")
    print("  test        → Run connected Android tests (if any)")
    print("  native      → Rebuild just the native C++/JNI code")
    print("  logcat      → Show filtered logs (VrTemplate tag only)")
    print("  devcycle    → Full build → install → start → logcat")
    print("  help        → Show this help text\n")
    print("Usage:")
    print("  cmd.py [debug | profile | release] <commands...>")
    print("Example:")
    print("  cmd.py debug clean build install start logcat")
    return 0

# =========
# Main loop
# =========

def main():
    argv = sys.argv[1:]
    if not argv or argv[0] in ["--help", "-h", "help"]:
        help()
        sys.exit(0)

    build_configs = ["debug", "release", "profile"]
    active_build_config = "profile"
    if argv[0] in build_configs:
        active_build_config = argv[0]
        argv = argv[1:]

    if not argv:
        help()
        sys.exit(1)

    for idx, arg in enumerate(argv):
        try:
            # Explicitly reject installRelease before calling anything
            if (arg == "install"  or arg == "devcycle") and active_build_config == "release":
                print("   installRelease is not available without a signing config.")
                print("   Either configure signing in build.gradle.kts or use debug/profile builds.")
                sys.exit(4)

            fn = globals()[arg]
            if callable(fn) and fn(active_build_config) != 0:
                if len(argv[idx + 1:]) > 0:
                    print("ERROR: The following commands were not executed:", argv[idx + 1:])
                    sys.exit(2)
        except KeyError:
            print(f"Error: unrecognized command '{arg}'")
            help()
            sys.exit(3)

if __name__ == "__main__":
    main()
