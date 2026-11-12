# Hardware and layout

Observed model: Xiaomi AX3000E RN07, stock 1.0.24; IPQ5018/AP-MP03.3, ARMv7 dual core; 256 MiB DDR; 64 MiB serial NAND; integrated IPQ5018 2.4 GHz + QCN6122 5 GHz; Motorcomm YT9215 switch. This is not interchangeable with another AX3000 marketing model.

DDR spans `0x40000000..0x4fffffff`. The tree preserves the stock reservation union. Q6 reserves `0x4b000000` length `0x03000000`. Kernel load/entry is `0x41208000`; FIT staging was `0x44000000`. Do not substitute a generic 512 MiB board tree.

| Stock partition | Offset | Size | Experimental kernel access |
|---|---:|---:|---|
| APPSBLENV | `0x480000` | `0x80000` | read/write; environment transaction only |
| ART | `0x700000` | `0x100000` | read-only |
| original rootfs slot 0 | `0x980000` | `0x1900000` | hidden |
| rootfs_1 slot 1 | `0x2280000` | `0x1900000` | read/write |

These offsets were checked against both saved MIBIB tables on the test device. They are observations, not authorization to assume another device has the same layout. Bootloader, QSEE, original overlay and master flash are not exposed by the experimental install kernel. `CONFIG_MTD_PARTITIONED_MASTER` is disabled. Runtime MTD numbering differs from stock: experimental mtd0=environment, mtd1=ART, mtd2=slot1. Never copy stock MTD numbers into OpenWrt commands.

NAND geometry: 2048-byte page, 131072-byte erase block, 64 MiB capacity. The board-scoped table includes stock IDs EF BA (W25N512GW) and CD 60 (F35UQA512M); this does not mean both physical chip variants were tested. Do not change global identification for all IPQ devices.

Switch topology: LAN ports 2/1/0 to MAC8/eth1, WAN port 3 to MAC4/eth0. Only one physical LAN path was exercised; port mapping and WAN performance still require independent verification. UART pinout/voltage was not verified and no wiring instructions are asserted here.
