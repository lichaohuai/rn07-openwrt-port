# Validation record and open issues

One physical RN07, stock 1.0.24, was used. Evidence below is a curated summary; raw captures contain device-specific data and are not public.

| Area | Evidence | Boundary |
|---|---|---|
| NAND | correct 64 MiB geometry; slot1 writes and independent logical readback | other chip variants not exercised |
| Persistence | squashfs /rom, UBIFS /overlay, root overlay; config/auth hashes and marker survived a normal reboot | one reboot is not endurance testing |
| Wi-Fi | both WPA2 APs; Pixel client; 4 MiB each direction per band, matching SHA256 | HE20 channels 1/36 only, no throughput benchmark |
| LAN | one wired path, observed 1000baseT | not all jacks or WAN |
| DHCP/DNS | both wireless leases; local-name DNS response from a LAN Wi-Fi client | no upstream DNS validation |
| LuCI | authenticated overview plus interfaces/wireless/firewall pages | no claim every button/plugin works |
| Firewall | fw4/nft ruleset validated | no WAN/NAT traffic tested |
| Stock return | selected slot0, environment CRC/readback, original firmware identity and management page | no full factory erase; slot1 retained |

## Issues that remain

- A Q6 user-PD stop handshake was a blocker during earlier tests. The published 903 patch is bounded diagnostic logging only; waits, shutdown calls and firmware memory layout are unchanged. It is not a confirmed fix.
- Some software restarts/diagnostic exits required power cycling. A later episode had no Wi-Fi or Ethernet connectivity until power was cycled; its initiating cause was not established. Subsequent successful restarts do not close this issue.
- WAN Internet, NAT forwarding, all physical port assignments, IPv6 upstream, large channels, regulatory coverage, sustained load and long-term memory use remain open.
- LuCI station signal reporting showed zero/absent values despite real traffic; cosmetic/driver reporting requires follow-up.
- Retaining stock limits writable space: about 8.7 MiB total UBIFS, about 8.1 MiB free in the observed install. No large proxy plugin was installed. Repartitioning away the stock slot is a different untested project.

## The static-rootfs packaging defect

The first persistent install returned to stock. Its static UBI rootfs length was 7,386,580 bytes; ubiblock exposed 7,386,112 bytes, dropping the final 468 bytes. Squashfs id-table metadata began at 7,386,572. Repackaging rootfs as a dynamic volume exposed 59 full LEBs (7,491,584 bytes), preserving the payload and padding. Kernel and squashfs content were unchanged. The corrected image booted and persisted configuration. `tools/verify_ubi.py` rejects the static-rootfs layout.

Publication checks cover source patch application, DT compilation comparison and UBI packaging/validation. They do not replace a fresh full firmware build or independent device testing.
