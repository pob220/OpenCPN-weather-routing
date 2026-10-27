# SPDX-License-Identifier: GPL-3.0-or-later
"""Build released plugin sources and qualify normal installed-host shutdown.

Artifacts are retained for review; this workflow never publishes to a catalogue.
The wx3.3.4 host is the independently validated #5499 rebuild, identified in its
pin, while an updated official upstream installer is pending.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
import time
import xml.etree.ElementTree as ET

from sdk import (digest, download, exports, import_library, imported_symbols,
                 pe_sections, validate_wx_dependencies)

ROOT = Path(__file__).resolve().parents[2]
SPECS = {
    'celnav': ('pob220/celestial_navigation_pi', 'celestial_navigation_pi',
               'CELESTIAL_WINDOWS_IMPORT_LIBRARY'),
    'climatology': ('pob220/climatology_pi', 'climatology_pi',
                   'CLIMATOLOGY_WINDOWS_IMPORT_LIBRARY'),
    'xgrib': ('pob220/xgrib_pi', 'xgrib_pi', 'XGRIB_OPENCPN_IMPORT_LIBRARY'),
    'weather_routing': ('pob220/weather_routing_pi', 'weather_routing_pi',
                        'WEATHER_ROUTING_WINDOWS_IMPORT_LIBRARY'),
}


def run(*args, cwd=ROOT, **kwargs):
    print('+', ' '.join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), cwd=cwd, check=True, **kwargs)


def extract(archive, target):
    with tarfile.open(archive) as package:
        package.extractall(target, filter='data')


def build(plugin, version, out, work):
    repository, identity, import_option = SPECS[plugin]
    source = work / 'source'
    branch = 'ci/wx334-host-followup' if plugin == 'weather_routing' else 'fix/windows-wx334'
    pinned = json.loads((Path(__file__).with_name('sources.json')).read_text())[plugin]
    source.mkdir()
    run('git', 'init', source)
    run('git', 'config', 'core.autocrlf', 'false', cwd=source)
    run('git', 'remote', 'add', 'origin', 'https://github.com/' + repository + '.git', cwd=source)
    run('git', 'fetch', '--depth', '1', 'origin', pinned, cwd=source)
    run('git', 'checkout', '--detach', 'FETCH_HEAD', cwd=source)
    run('git', 'submodule', 'update', '--init', 'opencpn-libs', cwd=source)
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source,
                                       text=True).strip()
    assert revision == pinned, 'Plugin source does not match the qualification pin'
    pin = json.loads((Path(__file__).parent / ('wx-' + version + '.json')).read_text())
    installer = work / 'host.exe'
    download(pin['host_url'], installer, pin['host_sha256'])
    host = work / 'host'
    run('7z', 'x', '-y', '-o' + str(host), installer, stdout=subprocess.DEVNULL)
    sdk = work / 'sdk'
    sdk.mkdir()
    host_exports = import_library(host / 'opencpn.exe', sdk / 'opencpn.lib')
    import_library(host / 'z.dll', sdk / 'z.lib')
    wx = work / 'wx'
    for filename, checksum in pin['wx_archives'].items():
        archive = work / filename
        download('https://github.com/wxWidgets/wxWidgets/releases/download/v' +
                 version + '/' + filename, archive, checksum)
        run('7z', 'x', '-y', '-o' + str(wx), archive, stdout=subprocess.DEVNULL)
    wxlib = wx / 'lib/vc14x_x64_dll'
    for library in wxlib.glob('*.dll'):
        matching = host / library.name
        if matching.is_file() and digest(library) != digest(matching):
            raise RuntimeError('Host/toolkit runtime mismatch: ' + library.name)
    os.environ.update(PATH=str(host) + os.pathsep + str(wxlib) + os.pathsep + os.environ['PATH'],
                      WX_VER='32' if version.startswith('3.2.') else '33',
                      OCPN_TARGET='MSVC-x64', GITHUB_REPOSITORY=repository,
                      GITHUB_REF_NAME=branch)
    args = ['-D' + import_option + '=' + str(sdk / 'opencpn.lib'),
            '-DZLIB_LIBRARY=' + str(sdk / 'z.lib'),
            '-DZLIB_LIBRARY_RELEASE=' + str(sdk / 'z.lib')]
    if plugin == 'climatology':
        args += ['-DCLIMATOLOGY_WINDOWS_ZLIB_LIBRARY=' + str(sdk / 'z.lib')]
    if plugin == 'weather_routing':
        args += ['-DWEATHER_ROUTING_WINDOWS_ZLIB_LIBRARY=' + str(sdk / 'z.lib'),
                 '-DWEATHER_ROUTING_STANDALONE_API=ON',
                 '-DWEATHER_ROUTING_XWEATHER_IDENTITY=OFF',
                 '-DWEATHER_ROUTING_EXPECTED_WX_VERSION=' + version]
    if plugin == 'celnav':
        download('https://github.com/pob220/celestial_navigation_pi/releases/'
                 'download/eclipse-data-2026.1/de440s.bsp',
                 source / 'eclipse/data/de440s.bsp',
                 'c1c7feeab882263fc493a9d5a5b2ddd71b54826cdf65d8d17a76126b260a49f2')
        # Existing GUI capture fixtures use /tmp on the current Windows drive.
        (Path(work.anchor) / 'tmp').mkdir(exist_ok=True)
        vcpkg = Path(os.environ['VCPKG_INSTALLATION_ROOT'])
        run(vcpkg / 'vcpkg.exe', 'install', 'gtest:x64-windows')
        os.environ['PATH'] += os.pathsep + str(vcpkg / 'installed/x64-windows/bin')
        args += ['-DCMAKE_TOOLCHAIN_FILE=' + str(vcpkg / 'scripts/buildsystems/vcpkg.cmake'),
                 '-DVCPKG_TARGET_TRIPLET=x64-windows']
    if plugin == 'xgrib':
        run('git', 'submodule', 'update', '--init', 'generator', cwd=source)
        # The generator runs in a separate process and has no wxWidgets ABI.
        # Reuse the exact released 0.4.0 helper and its runtime/data unchanged.
        name = 'xgrib_pi-0.4.0.0-msvc-64-x86_64-10-MSVC-x64.tar.gz'
        helper_archive = work / name
        helper_hash = '5a2fbb76563734c2550f9f5939b07d738a5ca619e41958a190a76198508644d3'
        download('https://github.com/pob220/xgrib_pi/releases/download/v0.4.0.0/' + name,
                 helper_archive, helper_hash)
        helper_root = work / 'released-helper'
        extract(helper_archive, helper_root)
        binary = next(helper_root.rglob('environmental-grib.exe'))
        args += ['-DXGRIB_EXTERNAL_GENERATOR_DIR=' + str(binary.parent.parent),
                 '-DXGRIB_USE_BUNDLED_JASPER=ON']
        vcpkg = Path(os.environ['VCPKG_INSTALLATION_ROOT'])
        run(vcpkg / 'vcpkg.exe', 'install', 'glew:x64-windows', 'bzip2:x64-windows',
            'zlib:x64-windows')
        os.environ['PATH'] += os.pathsep + str(vcpkg / 'installed/x64-windows/bin')
        args += ['-DCMAKE_TOOLCHAIN_FILE=' + str(vcpkg / 'scripts/buildsystems/vcpkg.cmake'),
                 '-DVCPKG_TARGET_TRIPLET=x64-windows']
        pin['reused_generator_archive_sha256'] = helper_hash
    build_dir = work / 'build'
    run('cmake', '-S', source, '-B', build_dir, '-G', 'Visual Studio 17 2022', '-A', 'x64',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
        '-DBUILD_TESTING=ON', '-DOCPN_BUILD_TEST=ON',
        '-DwxWidgets_ROOT_DIR=' + str(wx), '-DwxWidgets_LIB_DIR=' + str(wxlib),
        '-DwxWidgets_CONFIGURATION=mswu', *args)
    run('cmake', '--build', build_dir, '--config', 'Release', '--parallel', '3')
    run('ctest', '--test-dir', build_dir, '-C', 'Release', '--output-on-failure',
        '--no-tests=error', '--timeout', '300' if plugin == 'celnav' else '90',
        '--output-junit', out / 'tests.xml')
    library = build_dir / 'Release' / (identity + '.dll')
    pe_sections(library)
    imported = subprocess.check_output(['dumpbin', '/imports', library], text=True)
    (out / 'imports.txt').write_text(imported)
    imports = imported_symbols(imported)
    missing = set(imports.get('opencpn.exe', [])) - host_exports.keys()
    if missing or not imports.get('opencpn.exe'):
        raise RuntimeError('Unresolved host imports: ' + str(sorted(missing)))
    wx_imports = {name: symbols for name, symbols in imports.items() if name.startswith('wx')}
    validate_wx_dependencies(wx_imports, version)
    for name, symbols in wx_imports.items():
        if set(symbols) - exports(host / name).keys():
            raise RuntimeError('Unresolved toolkit imports: ' + name)
    stage = work / 'package'
    run('cmake', '--install', build_dir, '--config', 'Release', '--prefix', stage)
    metadata_files = list(build_dir.glob(identity + '-*.xml'))
    if len(metadata_files) != 1:
        raise RuntimeError('Expected one generated metadata file: ' + str(metadata_files))
    metadata = stage / 'metadata.xml'
    document = ET.parse(metadata_files[0])
    element = document.getroot()
    # Both existing hosts use msvc-64. Keep the qualified packages in separate
    # artifact directories and filenames until catalogue ABI separation agrees.
    for key, value in [('target', 'msvc-64'), ('target-version', '10'),
                       ('target-arch', 'x86_64'),
                       ('source', 'https://github.com/' + repository + '/tree/' + revision)]:
        field = element.find(key)
        if field is None:
            field = ET.SubElement(element, key)
        field.text = value
    document.write(metadata, encoding='utf-8', xml_declaration=False)
    package = out / (identity + '-wx' + version.replace('.', '') + '.tar.gz')
    with tarfile.open(package, 'w:gz') as archive:
        for path in sorted(stage.rglob('*')):
            archive.add(path, arcname=path.relative_to(stage), recursive=False)
    receipt = dict(plugin=plugin, source_repository=repository, source_revision=revision,
                   host_pin=pin, wx_version=version, package_sha256=digest(package),
                   host_import_count=len(imports['opencpn.exe']), normal_shutdown=[])
    (out / 'provenance.json').write_text(json.dumps(receipt, indent=2))
    return host, identity, package, receipt, source


def installed_host(host, identity, package, receipt, out):
    import ctypes
    # Only disposable native CI may create this system-wide OpenCPN profile.
    assert sys.platform == 'win32' and os.environ.get('GITHUB_ACTIONS') == 'true'
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002)
    profile = Path(os.environ['PROGRAMDATA']) / 'opencpn'
    assert not (profile / 'opencpn.ini').exists(), 'CI profile already exists'
    profile.mkdir(exist_ok=True)
    binary = (host / 'opencpn.exe').read_bytes()
    versions = re.findall(rb'5\.\d+\.\d+(?:-[A-Za-z0-9._]+)?(?:\+[A-Za-z0-9._]+)?\x00', binary)
    dates = re.findall(rb'20\d{2}-\d{2}-\d{2}\x00', binary)
    version = max(versions, key=len)[:-1].decode()
    date = max(dates)[:-1].decode()
    config = ('[Settings]\nConfigVersionString=Version ' + version + ' Build ' + date +
              '\nNavMessageShown=1\nOpenGL=0\nDisableOpenGL=1\nShowMenuBar=1\nDataConnections=\n' +
              '[PlugIns/' + identity + '.dll]\nbEnabled=1\n')
    for bundled in (host / 'plugins').glob('*.dll'):
        config += '[PlugIns/' + bundled.name + ']\nbEnabled=0\n'
    (profile / 'opencpn.ini').write_text(config)
    for cycle in range(2):
        imported = run(host / 'opencpn-cmd.exe', 'import-plugin', package, cwd=host,
                       capture_output=True, text=True, timeout=90)
        (out / ('import-' + str(cycle) + '.log')).write_text(imported.stdout + imported.stderr)
        log = profile / 'opencpn.log'
        if log.exists():
            log.unlink()
        with (out / ('stdout-' + str(cycle) + '.log')).open('w') as stdout:
            process = subprocess.Popen([host / 'opencpn.exe', '--no_opengl'], cwd=host,
                                       stdout=stdout, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 90
                while time.monotonic() < deadline and process.poll() is None:
                    if log.exists() and 'Finalize Canvases' in log.read_text(errors='replace'):
                        break
                    time.sleep(0.2)
                else:
                    raise RuntimeError('Host did not complete startup')
                run(host / 'opencpn.exe', '--remote', '--quit', cwd=host, timeout=30)
                code = process.wait(timeout=30)
                if code != 0:
                    raise RuntimeError('Normal host shutdown failed: ' + str(code))
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                if log.exists():
                    shutil.copy2(log, out / ('host-' + str(cycle) + '.log'))
        text = log.read_text(errors='replace')
        if not re.search(r'PluginLoader: (?:Loading|Initializing) PlugIn:.*' +
                         re.escape(identity) + r'\.dll', text):
            raise RuntimeError('The installed plugin was not initialized')
        receipt['normal_shutdown'].append(dict(cycle=cycle, exit_code=code, plugin_initialized=True))
        (out / 'provenance.json').write_text(json.dumps(receipt, indent=2))


def main():
    assert sys.platform == 'win32' and os.environ.get('GITHUB_ACTIONS') == 'true'
    plugin, version = sys.argv[1:]
    assert plugin in SPECS and version in ('3.2.9', '3.3.4')
    out = ROOT / 'compatibility' / plugin / ('wx' + version)
    out.mkdir(parents=True, exist_ok=True)
    work = ROOT / 'compat-work'
    work.mkdir()
    host, identity, package, receipt, source = build(plugin, version, out, work)
    if plugin == 'weather_routing':
        environment = dict(os.environ, WR_HEADLESS_LIFECYCLE_CONTRACT='1')
        run(sys.executable, source / 'ci/test-installed-host.py', host / 'opencpn.exe',
            host / 'opencpn-cmd.exe', package, out / 'routing',
            '--host-version', receipt['host_pin']['host_version'],
            env=environment, timeout=900)
    else:
        installed_host(host, identity, package, receipt, out)


if __name__ == '__main__':
    main()
