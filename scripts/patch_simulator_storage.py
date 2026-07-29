"""Apply Duet's storage-compatibility patch to the simulator dependency."""

Import("env")  # noqa: F821

import os
import subprocess
import sys


PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
PIO_ENV = env.subst("$PIOENV")  # noqa: F821
SIMULATOR_DIR = os.path.join(
    PROJECT_DIR, ".pio", "libdeps", PIO_ENV, "simulator"
)
PATCH_PATH = os.path.join(
    PROJECT_DIR,
    "scripts",
    "simulator_patches",
    "0001-duet-storage-compat.patch",
)


def git_apply_check(*, reverse):
    command = ["git", "apply", "--check"]
    if reverse:
        command.append("--reverse")
    command.append(PATCH_PATH)
    return subprocess.run(
        command,
        cwd=SIMULATOR_DIR,
        capture_output=True,
        text=True,
    )


def patch_simulator_storage():
    if not os.path.isdir(os.path.join(SIMULATOR_DIR, ".git")):
        return
    if not os.path.isfile(PATCH_PATH):
        raise RuntimeError("Missing simulator storage patch: %s" % PATCH_PATH)
    if git_apply_check(reverse=True).returncode == 0:
        return

    check = git_apply_check(reverse=False)
    if check.returncode != 0:
        sys.stderr.write(
            "ERROR: simulator storage patch does not apply cleanly:\n%s%s\n"
            % (check.stdout, check.stderr)
        )
        raise SystemExit(1)

    subprocess.run(
        ["git", "apply", PATCH_PATH],
        cwd=SIMULATOR_DIR,
        check=True,
    )
    print("Applied Duet simulator storage compatibility patch")


patch_simulator_storage()
