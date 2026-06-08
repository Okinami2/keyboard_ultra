#!/usr/bin/env python3
"""Normalize CMake/Ninja response files for the Windows musl GCC toolchain."""

import os
import subprocess
import sys


def normalize_response_file(argument):
    if not argument.startswith("@"):
        return

    rsp_path = argument[1:].strip('"')
    if not os.path.isfile(rsp_path):
        return

    with open(rsp_path, "r", encoding="utf-8") as rsp_file:
        content = rsp_file.read()

    normalized = content.replace("\\", "/")
    if normalized == content:
        return

    with open(rsp_path, "w", encoding="utf-8", newline="") as rsp_file:
        rsp_file.write(normalized)


def main():
    if len(sys.argv) < 2:
        return 2

    compiler = sys.argv[1]
    arguments = sys.argv[2:]
    for argument in arguments:
        normalize_response_file(argument)

    normalized_arguments = [argument.replace("\\", "/") for argument in arguments]
    return subprocess.call([compiler] + normalized_arguments)


if __name__ == "__main__":
    sys.exit(main())
