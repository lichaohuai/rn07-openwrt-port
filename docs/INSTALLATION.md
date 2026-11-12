# Observed installation procedure — not an automatic installer

The tested route used the existing stock root access and stock dual-slot selector; no UART or replacement U-Boot was used. This does not establish that UART will never be needed for recovery. Obtaining stock SSH access is outside this source kit, and historical device-specific access helpers are not published.

1. Independently identify RN07, stock version, NAND geometry, partition map, current slot and boot flags. Read and verify full owner-local backups, including both slots, boot environment, MIBIB, ART and configuration. Record bad-block information and prepare a recovery path. A raw NAND image is not blindly interchangeable with a UBI image.
2. Validate kernel/FIT, DT memory reservations, external-module ABI, correct original wireless firmware and private calibration, UBI volume types and limits. Use a wired management path plus a separate upstream management connection.
3. On the original stock system, the experiment detached/rewrote **only the inactive slot1** as UBI. Do not infer that partition number mtd18 is universally correct; require the observed name, offset, size and active-slot check. The original slot0, bootloader, QSEE, ART and stock overlay were preserved.
4. Independently read back the full logical kernel/rootfs volume content and padding and compare with local artifacts. Do not set boot flags if readback differs.
5. Earlier trials used the stock one-shot OTA selector and an initramfs return timer. The persistent install instead used squashfs+UBIFS and explicit acceptance after mount/service verification. Exact stock selector behavior must be established for the device's bootloader; this repository does not offer a generic boot flag recipe for unknown firmware.
6. On the experimental system, verify `/rom` squashfs, writable `/overlay` UBIFS and root overlay, LAN, authentication and Wi-Fi. `rn07-boot-control accept` sets the normal slot1 flags only after checking the precise experimental layout. This writes environment flash; review it before use.
7. Reboot once and verify a private marker and configuration/authentication hashes survive. The test passed once, but later shutdown/connectivity stalls mean it is not a stability guarantee. Test WAN and every port independently before relying on routing.

The original private flasher deliberately refuses replay and depends on individual backups. It is omitted instead of being advertised as portable. The public code is intended to let developers review and finish the adaptation, not to imply a no-risk consumer installation.
