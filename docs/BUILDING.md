# Build recipe and its limits

The original full images were built locally. This publication extracts their source, configs and packaging into a portable research kit. **A clean full rebuild using only this public repository has not yet been completed.** Do not interpret the offline checks as a ready-made firmware release.

## Pinned base

Use Linux/x86_64, Python 3, Git, patch, device-tree-compiler, U-Boot tools, mtd-utils and the normal OpenWrt build dependencies. Pin the community base rather than today's OpenWrt main:

```sh
git clone https://github.com/hzyitc/openwrt-redmi-ax3000.git openwrt-rn07
cd openwrt-rn07
git checkout 6faaa1504fd5c5ca34f61b22ab07851907604c59
cd ..
python3 rn07-openwrt-port/tools/apply_openwrt.py openwrt-rn07
```

The helper refuses a different commit or dirty tracked files and only applies local source changes. It adds historical initramfs diagnostic profiles, not a persistent sysupgrade target. `metadata/upstream.json` records the matching ImageBuilder/toolchain release and SHA256s. Retrieve those assets from the pinned community release yourself; no toolchain or vendor files are mirrored here.

## Kernel and DT

The target is ipq50xx/arm, GCC 12.3.0 musl EABI, Linux 5.15.150 with the pinned tree's patches. Prepare the target kernel with the usual OpenWrt build, apply the supplied RN07 patches (the overlay places them in the target patch directory), then use `configs/kernel-5.15.150.config` as the historical configuration. Reconcile `olddefconfig` changes explicitly. The exact export ABI and release must match every external module, including NSS-DP, SSDK, Motorcomm and ath11k backports. Do not take kmods from another build.

`dts/rn07-install-resolved.dts` is self-contained and can be compiled with `dtc -I dts -O dtb`. Its output was compared with the historically installed DTB during publication. The layered files describe its derivation. The persistent kernel uses a forced command line:

```text
console=ttyMSM0,115200n8 ubi.mtd=rootfs_1 ubi.block=0,rootfs root=/dev/ubiblock0_1 rootfstype=squashfs rootwait
```

Create an ARM LZMA FIT using upstream `scripts/mkits.sh`, load/entry `0x41208000`, configuration `config@mp03.3`, kernel version 5.15.150. The kernel volume reserves 33 LEBs (`4190208` bytes). Validate the uncompressed kernel and FIT load regions do not overlap. Original builds used `SOURCE_DATE_EPOCH=1711145382`; this alone does not guarantee a reproducible full image.

## Userspace and private inputs

Use the matching ImageBuilder, Linux 5.15.150 modules and LuCI packages; install LuCI base/network/system/status, rpcd, uhttpd, dnsmasq/firewall4, the appropriate wpad and uboot-envtools. The source overlay supplies boot helpers. Enable `rn07_boot` with the normal OpenWrt init link only after reviewing the environment layout. Preserve the `platform.sh` refusal of generic sysupgrade.

A usable image additionally needs owner-local data, deliberately excluded from Git:

- Original RN07 **2.5-series** Q6/m3 wireless firmware and matching BDF. The initially tried 2.7-series combination failed; do not mix firmware and memory layout blindly.
- The device's own ART/calibration. Extraction offsets are in the caldata patch; do not copy another unit's data or MACs.
- Unique device addresses, private admin password hash and authorized key, private WPA2 passphrase, LAN/WAN settings. Never distribute a default shared credential or an image containing these values.

The source rootfs overlay is not a complete, provisioned rootfs. Assemble and inspect your own squashfs, keeping credentials and vendor inputs outside the repository.

## UBI packaging

Both kernel and rootfs must be **dynamic** UBI volumes. Static rootfs was a real failed attempt: squashfs was 7,386,580 bytes, ubiblock exposed only 7,386,112 bytes, truncating 468 bytes containing the id table. See VALIDATION.md.

```sh
python3 tools/pack_ubi.py --kernel private/kernel.itb --rootfs private/rootfs.squashfs --output output/rn07.ubi
python3 tools/verify_ubi.py output/rn07.ubi --kernel private/kernel.itb --rootfs private/rootfs.squashfs
```

These commands only manipulate local files. They neither flash nor connect to a router. `output/` and `private/` are ignored. Artifact verification is only one prerequisite; it is not hardware acceptance.
