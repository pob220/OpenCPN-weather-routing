#!/usr/bin/env python3
"""Relink pinned Qt 5.12.2 objects without changing the Qt/wx ABI.

Input support must be freshly extracted from core support v1.2 (SHA256
c4110c532e9a0bcf071bbd10fe6f7627d7e91380c803c52ac0e89ce5f993db9b).
The archive retains the complete QtBase object files and makefiles. Extras
is compiled from matching upstream sources, with the retained Qt headers.
No existing ELF is patched; LOAD and RELRO layout is produced by the linker.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import zipfile


def variable(path, key):
    source = path.read_text().replace('\\\n', ' ')
    match = re.search(r'^' + key + r'\s*=\s*(.*)$', source, re.M)
    if not match:
        raise ValueError(f'{path}: missing {key}')
    return shlex.split(match[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--support', required=True, type=Path)
    parser.add_argument('--support-archive', required=True, type=Path)
    parser.add_argument('--base-source', required=True, type=Path)
    parser.add_argument('--ndk', required=True, type=Path)
    parser.add_argument('--runtime-ndk', required=True, type=Path)
    parser.add_argument('--extras-source', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if hashlib.sha256(args.support_archive.read_bytes()).hexdigest() != 'c4110c532e9a0bcf071bbd10fe6f7627d7e91380c803c52ac0e89ce5f993db9b':
        raise ValueError('Wrong core support archive')
    archive = zipfile.ZipFile(args.support_archive)
    # The support archive omits several private forwarding-header targets.
    # Restore only missing matching upstream headers in a disposable copy.
    for path in (args.base_source / 'src').rglob('*.h'):
        dest = args.support / 'qt5/qtbase/src' / path.relative_to(args.base_source / 'src')
        if not dest.exists():
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, dest)
    base = args.support.resolve() / 'qt5/build_arm64_O3/qtbase'
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    compiler = args.ndk.resolve() / 'toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android21-clang++'
    ar = compiler.parent / 'llvm-ar'
    runtime = args.runtime_ndk.resolve() / 'toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so'
    shutil.copy2(runtime, out / runtime.name)
    inputs = {}

    def objects(makefile):
        result = [(makefile.parent / name).resolve() for name in variable(makefile, 'OBJECTS')]
        for path in result:
            if not path.is_file():
                raise ValueError(f'missing retained object {path}')
            relative = str(path.relative_to(args.support.resolve()))
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if digest != hashlib.sha256(archive.read('OCPNAndroidCoreBuildSupport/' + relative)).hexdigest():
                raise ValueError(f'retained object differs from pinned archive: {relative}')
            inputs[relative] = digest
        return result

    # Reconstruct the static Qt support libraries from the exact archived lists.
    for makefile in sorted(base.glob('src/**/Makefile')):
        target = variable(makefile, 'TARGET') if re.search(r'^TARGET\s*=', makefile.read_text(), re.M) else []
        if target and target[0].endswith('.a'):
            subprocess.run([str(ar), 'rcs', str(out / target[0]), *map(str, objects(makefile))], check=True)

    common = [str(compiler), '-shared', '-Wl,--no-undefined', '-Wl,-z,relro',
              '-Wl,-z,noexecstack', '-Wl,-z,max-page-size=16384',
              '-Wl,-z,common-page-size=16384', '-L' + str(out)]
    modules = ['corelib', 'gui', 'widgets', 'opengl', 'testlib',
               'plugins/platforms/android', 'plugins/styles/android']
    for module in modules:
        makefile = base / 'src' / module / 'Makefile'
        name = variable(makefile, 'TARGET')[0]
        if name == 'libqtforandroid.so':
            name = 'libplugins_platforms_android_libqtforandroid.so'
        elif name == 'libqandroidstyle.so':
            name = 'libplugins_styles_libqandroidstyle.so'
        libraries = []
        for token in variable(makefile, 'LIBS'):
            if token == '$(SUBLIBS)' or token.startswith('-L'):
                continue
            if token.startswith('/'):
                filename = Path(token).name
                token = str(out / filename) if filename.endswith('.a') else '-l' + filename[3:-3]
            libraries.append(token)
        command = common + ['-Wl,-soname,' + name, '-o', str(out / name)] + list(map(str, objects(makefile))) + libraries
        subprocess.run(command, check=True)

    # QtAndroidExtras has no retained object list. Its upstream sources use no
    # moc-generated QObject classes and can be built with the matching headers.
    src = args.extras_source.resolve() / 'src/androidextras'
    qtinclude = base / 'include'
    include = ['-I' + str(p) for p in [qtinclude, qtinclude / 'QtCore',
               qtinclude / 'QtCore/5.12.2', qtinclude / 'QtCore/5.12.2/QtCore',
               qtinclude / 'QtAndroidExtras', src, src / 'android', src / 'jni']]
    extraobjs = []
    for path in sorted(src.glob('*/*.cpp')):
        obj = out / (path.stem + '.o')
        subprocess.run([str(compiler), '-std=c++17', '-O2', '-fPIC',
                        '-fvisibility=hidden', '-DQT_BUILD_ANDROIDEXTRAS_LIB',
                        '-DQT_NO_DEBUG', '-DQT_NO_USING_NAMESPACE',
                        *include, '-c', str(path), '-o', str(obj)], check=True)
        extraobjs.append(str(obj))
    name = 'libQt5AndroidExtras.so'
    subprocess.run(common + ['-Wl,-soname,' + name, '-o', str(out / name),
                            *extraobjs, '-lQt5Core', '-lc++_shared', '-llog'], check=True)
    (out / 'relink-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')


if __name__ == '__main__':
    main()
