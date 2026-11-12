#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
import argparse, json
from pathlib import Path
from ubi_layout import verify
p = argparse.ArgumentParser(description='Verify an RN07 UBI file offline')
p.add_argument('image', type=Path)
p.add_argument('--kernel', type=Path)
p.add_argument('--rootfs', type=Path)
a = p.parse_args()
print(json.dumps(verify(a.image.read_bytes(), a.kernel.read_bytes() if a.kernel else None, a.rootfs.read_bytes() if a.rootfs else None), indent=2))
