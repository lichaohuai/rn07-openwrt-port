#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Apply this research overlay to a clean, pinned OpenWrt source checkout."""
from pathlib import Path
import argparse, json, shutil, subprocess
ROOT = Path(__file__).resolve().parents[1]
def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    a = p.parse_args(); source = a.source.resolve()
    lock = json.loads((ROOT / 'metadata/upstream.json').read_text())
    def git(*args):
        return subprocess.check_output(['git', '-C', str(source), *args], text=True).strip()
    if git('rev-parse', 'HEAD') != lock['commit']:
        raise SystemExit('Refusing a source commit other than the pinned base')
    if git('status', '--porcelain'):
        raise SystemExit('Refusing a dirty source checkout')
    copies = [(f, source / 'target/linux/ipq50xx/dts' / f.name)
              for f in sorted((ROOT / 'dts').glob('ipq5018-rn07*.dts'))]
    copies += [(f, source / 'target/linux/ipq50xx/patches' / f.name)
               for f in sorted((ROOT / 'patches/kernel').glob('*.patch'))]
    for _, dest in copies:
        if dest.exists(): raise SystemExit(f'Refusing to overwrite {dest}')
    patch = ROOT / 'patches/openwrt/0001-rn07-platform.patch'
    subprocess.run(['git', '-C', str(source), 'apply', '--check', str(patch)], check=True)
    subprocess.run(['git', '-C', str(source), 'apply', str(patch)], check=True)
    for src, dest in copies:
        dest.parent.mkdir(parents=True, exist_ok=True); shutil.copyfile(src, dest)
    print('Source overlay applied. No firmware built or device contacted.')
if __name__ == '__main__': main()
