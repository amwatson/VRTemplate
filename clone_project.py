#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import re
from pathlib import Path


def run(cmd, **kwargs):
    print(f"Running: {' '.join(cmd)}")
    subprocess.run(cmd, check=True, **kwargs)


def camel_to_title(name):
    return re.sub(r'(?<!^)(?=[A-Z])', ' ', name).title()


def camel_to_lowercase(name):
    # Convert CamelCase to lowercase (e.g., MyProject -> myproject)
    return ''.join(c.lower() for c in name)


def replace_in_file(file_path, replacements):
    with open(file_path, 'r', encoding='utf-8') as f:
        contents = f.read()

    original = contents

    for pattern, repl in replacements.items():
        contents = re.sub(pattern, repl, contents)

    if contents != original:
        with open(file_path, 'w', encoding='utf-8') as f:
            f.write(contents)


def rename_package_dirs(dst_path, old_package, new_package):
    old_dir = os.path.join(dst_path, "app/src/main/java", *old_package.split('.'))
    new_dir = os.path.join(dst_path, "app/src/main/java", *new_package.split('.'))
    if os.path.isdir(old_dir):
        os.makedirs(os.path.dirname(new_dir), exist_ok=True)
        shutil.move(old_dir, new_dir)
        # Clean up any empty parent dirs
        try:
            Path(old_dir).parent.rmdir()
        except OSError:
            pass


def clone_and_rewrite(src_path, dst_root, new_app_name, new_package_name):
    dst_path = os.path.join(dst_root, new_app_name)

    # Clone the project with full git history
    run(["git", "clone", "--recurse-submodules", src_path, dst_path])

    os.chdir(dst_path)

    # Rename Android package references
    old_package = "com.amwatson.vrtemplate"
    old_package_path = old_package.replace('.', '_')
    new_package_path = new_package_name.replace('.', '_')

    # Generate lowercase version of the new app name for CMakeLists.txt
    lowercase_app_name = camel_to_lowercase(new_app_name)

    replacements = {
        re.escape(old_package): new_package_name,
        re.escape(old_package_path): new_package_path,
        r"\bVrTemplate\b": new_app_name,
        r"\bVR Template\b": camel_to_title(new_app_name),
        r"\bvrtemplate\b": lowercase_app_name,  # For CMakeLists.txt
    }

    # Replace in known file types
    extensions = [".kt", ".java", ".cpp", ".h", ".xml", ".gradle.kts", ".py", ".txt"]
    for root, _, files in os.walk(dst_path):
        for f in files:
            if any(f.endswith(ext) for ext in extensions):
                replace_in_file(os.path.join(root, f), replacements)

    # Move package directory
    rename_package_dirs(dst_path, old_package, new_package_name)

    # Remove clone_project.py from the new project
    clone_script_path = os.path.join(dst_path, "clone_project.py")
    if os.path.exists(clone_script_path):
        os.remove(clone_script_path)
        print(f"Removed clone_project.py from the new project")

    # Commit changes
    run(["git", "add", "."], cwd=dst_path)
    run(["git", "commit", "-m", f"Rename to {new_app_name} and package {new_package_name}; remove clone_project.py"], cwd=dst_path)

    print(f"Project cloned and renamed to {dst_path}")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Clone and rename VR project template.")
    parser.add_argument("destination_root", help="Path where the cloned project will be created.")
    parser.add_argument("new_app_name", help="New app/project name (e.g., CoolProject).")
    parser.add_argument("new_package_name", help="New package name (e.g., com.example.coolproject).")

    args = parser.parse_args()

    current_project_dir = Path(__file__).resolve().parent
    clone_and_rewrite(str(current_project_dir), args.destination_root, args.new_app_name, args.new_package_name)
