#!/usr/bin/env python3
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.

import argparse
import os
import pathlib
import json
import subprocess

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
REPO_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "..", "..", ".."))
BUILD_PY = os.path.normpath(os.path.join(REPO_DIR, "tools", "ci_build", "build.py"))
JAVA_ROOT = os.path.normpath(os.path.join(REPO_DIR, "java"))
DEFAULT_BUILD_ABIS = ["armeabi-v7a", "arm64-v8a", "x86", "x86_64"]
DEFAULT_ANDROID_API = 28


def _parse_build_config(args):
    config_file = args.build_config_file.resolve()

    if not config_file.is_file():
        raise FileNotFoundError('Build config file {} is not a file.'.format(config_file))

    with open(config_file) as f:
        config_data = json.load(f)

    build_config = {}
    build_config['android_sdk_path'] = args.android_sdk_path
    build_config['android_ndk_path'] = args.android_ndk_path

    if 'build_flavor' in config_data:
        build_config['build_flavor'] = config_data['build_flavor']
    else:
        raise ValueError('build_flavor is required in the build config file')

    if 'build_abis' in config_data:
        build_config['build_abis'] = config_data['build_abis']
    else:
        build_config['build_abis'] = DEFAULT_BUILD_ABIS

    build_params = []
    if 'build_params' in config_data:
        build_params += config_data['build_params']
    else:
        raise ValueError('build_params is required in the build config file')

    if 'android_api' in config_data:
        build_params += ['--android_api=' + str(config_data['android_api'])]
    else:
        build_params += ['--android_api=' + str(DEFAULT_ANDROID_API)]

    build_config['build_params'] = build_params
    return build_config


def _build_aar(args):
    build_config = _parse_build_config(args)
    build_dir = args.build_dir

    # Setup temp environment for building
    my_env = os.environ.copy()
    my_env['ANDROID_HOME'] = build_config['android_sdk_path']
    my_env['ANDROID_NDK_HOME'] = build_config['android_ndk_path']

    # Temp dirs to hold building results
    _intermediates_dir = os.path.join(build_dir, 'intermediates')
    _aar_dir = os.path.join(_intermediates_dir, 'aar')
    _jnilibs_dir = os.path.join(_aar_dir, 'jnilibs')

    # Build binary for each ABI, one by one
    for abi in build_config['build_abis']:
        _build_dir = os.path.join(_intermediates_dir, abi)
        _build_command = ['python3', BUILD_PY]
        _build_command += build_config['build_params']
        _build_command += [
            '--android_abi=' + abi,
            '--config=' + build_config['build_flavor'],
            '--build_dir=' + _build_dir
        ]
        if args.include_ops_by_config is not None:
            _build_command += ['--include_ops_by_config=' + args.include_ops_by_config]

        subprocess.run(_build_command, env=my_env, shell=False, check=True, cwd=REPO_DIR)

        # create symbolic link for libonnxruntime.so and libonnxruntime4j_jni.so
        # to jnilibs/[abi] for later compiling the aar package
        _jnilibs_abi_dir = os.path.join(_jnilibs_dir, abi)
        os.makedirs(_jnilibs_abi_dir, exist_ok=True)
        for lib_name in ['libonnxruntime.so', 'libonnxruntime4j_jni.so']:
            _target_lib_name = os.path.join(_jnilibs_abi_dir, lib_name)
            if os.path.exists(_target_lib_name):
                os.remove(_target_lib_name)
            os.symlink(os.path.join(_build_dir, build_config['build_flavor'], lib_name), _target_lib_name)

    # The directory to publish final AAR
    _aar_publish_dir = os.path.join(build_dir, 'aar_out')
    os.makedirs(_aar_publish_dir, exist_ok=True)

    # get the common gradle command args
    _gradle_command = [
        'gradle',
        '--no-daemon',
        '-b=build-android.gradle',
        '-c=settings-android.gradle',
        '-DjniLibsDir=' + _jnilibs_dir,
        '-DbuildDir=' + _aar_dir,
        '-DpublishDir=' + _aar_publish_dir
    ]

    # clean, build, and publish to a local directory
    subprocess.run(_gradle_command + ['clean'], env=my_env, shell=False, check=True, cwd=JAVA_ROOT)
    subprocess.run(_gradle_command + ['build'], env=my_env, shell=False, check=True, cwd=JAVA_ROOT)
    subprocess.run(_gradle_command + ['publish'], env=my_env, shell=False, check=True, cwd=JAVA_ROOT)


def parse_args():
    parser = argparse.ArgumentParser(
        os.path.basename(__file__),
        description='''Create Android Archive (AAR) package for one or more Android ABI(s)
        and building properties specified in the given build config file, see
        tools/ci_build/github/android/default_mobile_aar_config.json and
        tools/ci_build/github/android/build_aar_package.md for details
        '''
    )

    parser.add_argument(
        "--android_sdk_path", type=str, default=os.environ.get("ANDROID_HOME", ""),
        help="Path to the Android SDK")

    parser.add_argument(
        "--android_ndk_path", type=str, default=os.environ.get("ANDROID_NDK_HOME", ""),
        help="Path to the Android NDK")

    parser.add_argument(
        '--build_dir', type=pathlib.Path,
        default=os.path.join(REPO_DIR, 'build_android_aar'),
        help='Provide the root directory for build output')

    parser.add_argument(
        "--include_ops_by_config", type=str,
        help="Include ops from config file. See /docs/Reduced_Operator_Kernel_build.md for more information.")

    parser.add_argument(
        'build_config_file', type=pathlib.Path,
        help='Provide the config file for building AARs')

    return parser.parse_args()


def main():
    args = parse_args()

    # Android SDK and NDK path are required
    if not args.android_sdk_path:
        raise ValueError('android_sdk_path is required')
    if not args.android_ndk_path:
        raise ValueError('android_ndk_path is required')

    _build_aar(args)

if __name__ == '__main__':
    main()
