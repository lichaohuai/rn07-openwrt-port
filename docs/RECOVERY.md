# Return to preserved stock

On **this experimental slot1 layout only**, `rn07-return-stock` calls `rn07-boot-control stock`, syncs and reboots. The helper checks board identity, build marker, partition names/sizes and model. It sets normal slot0 boot flags through the stock-format environment tool and verifies the values. It does not overwrite the original system slot or restore a missing/damaged original system.

The observed return was staged: save raw environment, select stock, read the environment back, verify CRC and that only expected boot flags changed, then reboot. Stock 1.0.24, device identity, original Wi-Fi association/DHCP and the original management page were confirmed. Network settings were preserved. Slot1's OpenWrt payload remained; this was **not** a factory data erase or a two-slot stock reflash.

The environment tool configuration is `/dev/mtd0 0x0 0x10000 0x20000 0x4` only in the experimental layout. That is not the stock MTD numbering. The 64 KiB environment occupies part of a 512 KiB partition; preserving its remaining bytes matters.

If software shutdown stalls, the tested device sometimes needed a power cycle after confirming no flash write remained in progress. A power cycle does not restore erased firmware. Never treat backup files on a computer as proof that an untested recovery method can recover a brick. No generic UART/TFTP recovery procedure has been validated here.
