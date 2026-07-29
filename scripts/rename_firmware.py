"""
PlatformIO post-build script: copy firmware.bin to convenient artifact names
in the same build directory.

Default outputs:
  .pio/build/tiny/firmware-tiny.bin
  .pio/build/xlarge/firmware-xlarge.bin
  .pio/build/tiny/Duet-X3-v<version>.bin
  .pio/build/xlarge/Duet-X4-v<version>.bin

Release-candidate outputs when CROSSPOINT_RC_ARTIFACTS=1:
  .pio/build/tiny/Duet-X3-<branch>-<hash>-RC.bin
  .pio/build/xlarge/Duet-X4-<branch>-<hash>-RC.bin

Release outputs when CROSSPOINT_RELEASE_VERSION is set:
  .pio/build/x3-public/Duet-X3-v<version>.bin
  .pio/build/x4-public/Duet-X4-v<version>.bin
"""

import configparser
import os
import re
import shutil
import subprocess
import sys


def _copy_artifact(src, dst):
    shutil.copy(src, dst)
    print(f'Firmware copied to: {dst}')


def _get_project_option(env, name):
    try:
        value = env.GetProjectOption(name)
    except Exception:
        return None
    if isinstance(value, str):
        value = value.strip()
    return value or None


def _get_git_value(project_dir, *args, fallback):
    try:
        return subprocess.check_output(
            ['git', *args],
            text=True,
            stderr=subprocess.PIPE,
            cwd=project_dir,
        ).strip()
    except Exception:
        return fallback


def _get_git_branch(project_dir):
    branch = _get_git_value(
        project_dir,
        'rev-parse',
        '--abbrev-ref',
        'HEAD',
        fallback='unknown',
    )
    if branch == 'HEAD':
        return _get_git_value(
            project_dir,
            'rev-parse',
            '--short',
            'HEAD',
            fallback='unknown',
        )
    return branch


def _sanitize_branch(branch):
    if branch.startswith('release/'):
        branch = branch[len('release/'):]
    branch = branch.strip()
    branch = re.sub(r'[^A-Za-z0-9._-]+', '-', branch)
    branch = branch.strip('-.')
    return branch or 'unknown'


def _get_device_name(env):
    configured = _get_project_option(env, 'custom_duet_device')
    if configured:
        return configured.upper()
    env_name = env['PIOENV']
    if env_name in {'default', 'debug', 'tiny', 'x3-public', 'simulator-x3'}:
        return 'X3'
    if env_name in {'xlarge', 'x4-public', 'simulator'}:
        return 'X4'
    return env_name.upper()


def _get_project_version(project_dir):
    config = configparser.RawConfigParser()
    config.read(os.path.join(project_dir, 'platformio.ini'))
    try:
        version = config.get('crosspoint', 'crossink_version').strip()
    except (configparser.Error, KeyError, AttributeError):
        version = 'dev'
    return re.sub(r'[^A-Za-z0-9._-]+', '-', version) or 'dev'


def _get_rc_artifact_name(project_dir, env):
    device_name = _get_device_name(env)
    branch = (
        _get_project_option(env, 'custom_rc_branch')
        or os.environ.get('CROSSPOINT_RC_BRANCH')
        or _get_git_branch(project_dir)
    )
    short_hash = (
        _get_project_option(env, 'custom_rc_hash')
        or os.environ.get('CROSSPOINT_RC_HASH')
        or _get_git_value(
        project_dir,
        'rev-parse',
        '--short',
        'HEAD',
        fallback='00000',
    )
    )
    branch = _sanitize_branch(branch)
    short_hash = re.sub(r'[^A-Za-z0-9]+', '', short_hash)[:12] or '00000'
    return f'Duet-{device_name}-{branch}-{short_hash}-RC.bin'


def _is_rc_artifact_build(env):
    flag = _get_project_option(env, 'custom_rc_artifacts') or os.environ.get('CROSSPOINT_RC_ARTIFACTS')
    return str(flag).strip().lower() in {'1', 'true', 'yes', 'on'}


def _get_release_version(env):
    version = _get_project_option(env, 'custom_release_version') or os.environ.get('CROSSPOINT_RELEASE_VERSION')
    if not version:
        return None
    version = version.strip()
    if not version:
        return None
    if not version.startswith('v'):
        version = f'v{version}'
    return re.sub(r'[^A-Za-z0-9._-]+', '-', version)


def _get_branded_artifact_name(project_dir, env, release_version=None):
    version = release_version or f'v{_get_project_version(project_dir)}'
    return f'Duet-{_get_device_name(env)}-{version}.bin'


def rename_firmware(source, target, env):
    env_name = env['PIOENV']
    project_dir = env['PROJECT_DIR']
    src = str(target[0])
    build_dir = os.path.dirname(src)

    default_dst = os.path.join(build_dir, f'firmware-{env_name}.bin')
    _copy_artifact(src, default_dst)

    branded_dst = os.path.join(build_dir, _get_branded_artifact_name(project_dir, env))
    _copy_artifact(src, branded_dst)

    if _is_rc_artifact_build(env):
        rc_dst = os.path.join(build_dir, _get_rc_artifact_name(project_dir, env))
        _copy_artifact(src, rc_dst)

    release_version = _get_release_version(env)
    if release_version:
        release_dst = os.path.join(build_dir, _get_branded_artifact_name(project_dir, env, release_version))
        _copy_artifact(src, release_dst)


try:
    Import('env')                                           # noqa: F821  # type: ignore[name-defined]
    env.AddPostAction(                                      # noqa: F821  # type: ignore[name-defined]
        '$BUILD_DIR/${PROGNAME}.bin',
        rename_firmware,
    )
except NameError:
    print('rename_firmware.py: must be run via PlatformIO', file=sys.stderr)
