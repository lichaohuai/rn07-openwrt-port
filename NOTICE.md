# Attribution and publication scope

The OpenWrt base is [hzyitc/openwrt-redmi-ax3000](https://github.com/hzyitc/openwrt-redmi-ax3000), pinned in `metadata/upstream.json`. It derives from OpenWrt and Linux. The RN07 board description follows the CR881x topology where applicable, with RN07 stock-device observations. Credit belongs to those upstream authors; this repository publishes the RN07-specific work.

New project scripts/documentation are GPL-2.0-only. Existing kernel copyright and SPDX notices are preserved; DTS files retain GPL-2.0-or-later. `LICENSE` contains GPL version 2. Per-file notices take precedence. Patches retain the licensing of the files they modify.

`reference/` contains the exact GPL kernel source files used for the successful local installation, for comparison against patches. `dts/rn07-install-resolved.dts` resolves the pinned upstream IPQ5018 tree and RN07 overlays; it is source, not a binary flash dump. The public patch application and packaging checks do not establish a fresh end-to-end firmware build.

Not distributed: Xiaomi/Qualcomm proprietary wireless firmware, ART/calibration, NAND or partition dumps, bootloader binaries, private keys, passwords, raw device logs, serial numbers, device MAC addresses, or prebuilt flashable images. Source availability here does not grant rights to redistribute vendor firmware. Extract any required vendor inputs from a device/firmware you are entitled to use and keep them local.
