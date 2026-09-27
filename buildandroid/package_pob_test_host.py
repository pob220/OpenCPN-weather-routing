#!/usr/bin/env python3
# POBsoft (1985-2026): reproducible unofficial Android import test host.
# SPDX-License-Identifier: GPL-2.0-or-later
"""Retain a verified host APK, replace libgorp and version manifest, sign it.

This is a build-time packager, not an on-device signing/sandbox bypass.
Passwords are supplied through POB_APK_KEYSTORE_PASS, never printed or stored.
"""
import argparse
import hashlib
import json
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path


def sha(data):
    return hashlib.sha256(data).hexdigest()


def signature(name):
    return name == "META-INF/MANIFEST.MF" or (
        name.startswith("META-INF/") and
        name.upper().endswith((".SF", ".RSA", ".DSA", ".EC")))


def canonical(element):
    return (element.tag, sorted(element.attrib.items()),
            (element.text or "").strip(), [canonical(c) for c in element])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("original", "original-sha", "library", "resources", "output",
                 "source-sha", "sdk", "keystore", "certificate-sha"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[a-f0-9]{40}", args.source_sha):
        parser.error("source-sha must be the full committed source SHA")
    source_dir = Path(__file__).resolve().parent.parent
    actual_source = subprocess.check_output(
        ["git", "-C", str(source_dir), "rev-parse", "HEAD"], text=True).strip()
    if actual_source != args.source_sha:
        parser.error("source-sha differs from this packager's source checkout HEAD")
    if subprocess.check_output(["git", "-C", str(source_dir), "diff", "HEAD", "--name-only"]):
        parser.error("commit tracked source changes before packaging")
    original = Path(args.original)
    if sha(original.read_bytes()) != args.original_sha:
        parser.error("original APK checksum mismatch")
    sdk = Path(args.sdk)
    analyzer = sdk / "cmdline-tools/latest/bin/apkanalyzer"
    tools = sdk / "build-tools/35.0.0"
    original_cert = subprocess.check_output([str(tools/"apksigner"), "verify", "--print-certs", str(original)], text=True)
    cert = re.search(r"Signer #1 certificate SHA-256 digest: ([a-f0-9]+)", original_cert)
    if not cert or cert.group(1) != args.certificate_sha:
        parser.error("original APK certificate mismatch; do not assume in-place compatibility")
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    namespace = "{http://schemas.android.com/apk/res/android}"
    def manifest(archive):
        return ET.fromstring(subprocess.check_output(
            [str(analyzer), "manifest", "print", str(archive)]))
    old, new = manifest(original), manifest(args.resources)
    expected = {"versionName": "5.14.1-pob220-import-fix", "versionCode": "129"}
    for key, value in expected.items():
        if new.attrib.pop(namespace + key) != value:
            parser.error("unexpected compiled manifest " + key)
        old.attrib.pop(namespace + key)
    if canonical(old) != canonical(new):
        parser.error("manifest differs beyond the two authorized version fields")
    if new.get("package") != "org.opencpn.opencpn.dev":
        parser.error("only the retained development package is verified")
    library = Path(args.library).read_bytes()
    if library[:4] != b"\x7fELF" or library[4:6] != b"\x02\x01" or int.from_bytes(library[18:20], "little") != 183:
        parser.error("library must be little-endian ELF64 AArch64")
    with zipfile.ZipFile(args.resources) as resources:
        binary_manifest = resources.read("AndroidManifest.xml")
    replacements = {"AndroidManifest.xml": binary_manifest,
                    "lib/arm64-v8a/libgorp.so": library}
    with tempfile.TemporaryDirectory(prefix="pob-apk-", dir=output.parent) as temp:
        unsigned, aligned = Path(temp)/"unsigned.apk", Path(temp)/"aligned.apk"
        with zipfile.ZipFile(original) as src, zipfile.ZipFile(unsigned, "w") as dst:
            names = src.namelist()
            if len(names) != len(set(names)) or not replacements.keys() <= set(names):
                parser.error("duplicate or missing original APK entries")
            for entry in src.infolist():
                if not signature(entry.filename):
                    dst.writestr(entry, replacements.get(entry.filename, src.read(entry)))
        subprocess.run([str(tools/"zipalign"), "-P", "16", "-f", "4", str(unsigned), str(aligned)], check=True)
        subprocess.run([str(tools/"apksigner"), "sign", "--ks", args.keystore,
                        "--ks-pass", "env:POB_APK_KEYSTORE_PASS", "--key-pass",
                        "env:POB_APK_KEYSTORE_PASS", "--out", str(output), str(aligned)], check=True)
    verification = subprocess.check_output([str(tools/"apksigner"), "verify", "--print-certs", str(output)], text=True)
    match = re.search(r"Signer #1 certificate SHA-256 digest: ([a-f0-9]+)", verification)
    if not match or match.group(1) != args.certificate_sha:
        raise RuntimeError("signing certificate mismatch")
    with zipfile.ZipFile(original) as src, zipfile.ZipFile(output) as dst:
        old_names = {n for n in src.namelist() if not signature(n)}
        new_names = {n for n in dst.namelist() if not signature(n)}
        assert old_names == new_names, "non-signature entries added or removed"
        changed = sorted(n for n in old_names if src.read(n) != dst.read(n))
        assert changed == sorted(replacements), "unexpected APK content changes"
        assert dst.read("lib/arm64-v8a/libgorp.so") == library
    subprocess.run([str(tools/"zipalign"), "-c", "-P", "16", "4", str(output)], check=True)
    provenance = {"source": args.source_sha, "original_sha256": args.original_sha,
                  "apk_sha256": sha(output.read_bytes()), "library_sha256": sha(library),
                  "certificate_sha256": match.group(1), "changed_non_signature_entries": changed,
                  "package": new.get("package"), **expected}
    output.with_suffix(".provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(json.dumps(provenance, indent=2))


if __name__ == "__main__":
    main()
