"""
gen_compile_commands.py

Script shared across all platformio.ini files used as a post-build step to
generate the `compile_commands.json` file for getting better LSP support.

Author: Jordan Bourdeau
"""

import subprocess

if "compiledb" not in COMMAND_LINE_TARGETS:
    subprocess.run(["pio", "run", "-t", "compiledb"])
