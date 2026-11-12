#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Pack local FIT/squashfs inputs; never contact or write a device."""
from pathlib import Path
import argparse, json, shutil, subprocess, tempfile
from ubi_layout import KERNEL_LEBS, LEB, SLOT_BYTES, squashfs_check, verify
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--kernel', type=Path, required=True)
p.add_argument('--rootfs', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--sequence', type=int, default=2026111208)
a = p.parse_args(); k = a.kernel.resolve(); r = a.rootfs.resolve(); out = a.output.resolve()
if out.exists(): raise SystemExit('Refusing to overwrite an existing image')
if any('\n' in str(x) or '\r' in str(x) for x in (k, r, out)): raise SystemExit('Invalid path')
if not shutil.which('ubinize'): raise SystemExit('Install mtd-utils (ubinize) first')
kb = k.read_bytes(); rb = r.read_bytes(); squashfs_check(rb)
if not (40 <= len(kb) <= KERNEL_LEBS * LEB and kb[:4] == b'\xd0\x0d\xfe\xed'): raise SystemExit('FIT missing or too large')
if not 0 <= a.sequence <= 0xffffffff: raise SystemExit('Sequence out of range')
if KERNEL_LEBS + (len(rb)+LEB-1)//LEB + 5 + 4 > SLOT_BYTES//131072: raise SystemExit('Inputs exceed slot reservation')
out.parent.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='rn07-pack-', dir=out.parent) as tmp:
    tmp = Path(tmp); ini = tmp / 'ubi.ini'; image = tmp / 'image.ubi'
    ini.write_text(f'[kernel]\nmode=ubi\nimage={k}\nvol_id=0\nvol_type=dynamic\nvol_name=kernel\nvol_size={KERNEL_LEBS*LEB}\n\n[rootfs]\nmode=ubi\nimage={r}\nvol_id=1\nvol_type=dynamic\nvol_name=rootfs\n\n[rootfs_data]\nmode=ubi\nvol_id=2\nvol_type=dynamic\nvol_name=rootfs_data\nvol_size={5*LEB}\nvol_flags=autoresize\n')
    subprocess.run(['ubinize', '-o', str(image), '-p', '128KiB', '-m', '2048', '-s', '2048', '-O', '2048', '-Q', str(a.sequence), str(ini)], check=True)
    result = verify(image.read_bytes(), kb, rb)
    with out.open('xb') as f: f.write(image.read_bytes())
print(json.dumps(result, indent=2))
