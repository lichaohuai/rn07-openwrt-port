# SPDX-License-Identifier: GPL-2.0-only
"""Offline RN07 persistent UBI layout inspection. No hardware operations."""
import hashlib, struct, zlib
PEB = 131072
LEB = 126976
SLOT_BYTES = 25 * 1024 * 1024
KERNEL_LEBS = 33
class LayoutError(ValueError): pass
def require(ok, message):
    if not ok: raise LayoutError(message)
def crc(data): return zlib.crc32(data) ^ 0xffffffff
def squashfs_check(data, capacity=None):
    require(len(data) >= 96 and data[:4] == b'hsqs', 'not a squashfs superblock')
    used = struct.unpack_from('<Q', data, 40)[0]
    require(96 <= used <= len(data), 'invalid squashfs bytes_used')
    if capacity is None: capacity = ((len(data) + LEB - 1) // LEB) * LEB
    require(used <= capacity, 'squashfs truncated by block capacity')
    id_start = struct.unpack_from('<Q', data, 48)[0]
    require(id_start + 8 <= used and id_start + 8 <= capacity, 'squashfs id table is outside volume')
    return {'bytes_used': used, 'id_table_start': id_start, 'capacity': capacity}
def verify(blob, kernel=None, rootfs=None):
    require(len(blob) % PEB == 0 and 0 < len(blob) <= SLOT_BYTES, 'invalid slot image length')
    tables = {}; blocks = {}; sequences = set()
    for off in range(0, len(blob), PEB):
        ec = blob[off:off+64]
        require(ec[:5] == b'UBI#\1' and crc(ec[:60]) == struct.unpack_from('>I', ec, 60)[0], 'bad erase-counter header')
        vo, do, seq = struct.unpack_from('>III', ec, 16)
        require((vo, do) == (2048, 4096), 'unexpected NAND header geometry')
        sequences.add(seq); vid = blob[off+vo:off+vo+64]
        require(vid[:5] == b'UBI!\1' and crc(vid[:60]) == struct.unpack_from('>I', vid, 60)[0], 'bad volume-id header')
        volume, number = struct.unpack_from('>II', vid, 8)
        require(vid[5] == 1, 'static volumes are not accepted (rootfs truncation regression)')
        data = blob[off+do:off+PEB]
        if volume == 0x7fffefff:
            require(number in (0, 1) and number not in tables, 'duplicate/invalid layout block')
            tables[number] = data[:128*172]
        else:
            require(volume in (0, 1), 'unexpected data-bearing volume')
            chunks = blocks.setdefault(volume, {})
            require(number not in chunks, 'duplicate logical eraseblock')
            chunks[number] = data
    require(len(sequences) == 1 and set(tables) == {0, 1} and tables[0] == tables[1], 'inconsistent layout tables/image sequence')
    volumes = []
    for i in range(128):
        rec = tables[0][i*172:(i+1)*172]
        require(crc(rec[:168]) == struct.unpack_from('>I', rec, 168)[0], 'bad volume-table CRC')
        reserved, align, pad = struct.unpack_from('>III', rec)
        if reserved == 0: continue
        n = struct.unpack_from('>H', rec, 14)[0]
        require(i < 3 and align == 1 and pad == 0 and 0 < n <= 127, 'invalid volume geometry')
        name = rec[16:16+n].decode('ascii')
        require((name, rec[12], rec[144]) == [('kernel', 1, 0), ('rootfs', 1, 0), ('rootfs_data', 1, 1)][i], 'invalid volume type/name/autoresize')
        volumes.append({'id': i, 'name': name, 'reserved_lebs': reserved})
    require(len(volumes) == 3 and volumes[0]['reserved_lebs'] == KERNEL_LEBS and volumes[2]['reserved_lebs'] == 5, 'unexpected reservations')
    require(sum(v['reserved_lebs'] for v in volumes) + 4 <= SLOT_BYTES // PEB, 'volume reservations exceed slot')
    require(set(blocks) == {0, 1}, 'kernel/rootfs content missing')
    content = {}
    for volume, expected in [(0, kernel), (1, rootfs)]:
        chunks = blocks[volume]
        require(sorted(chunks) == list(range(len(chunks))), 'missing logical eraseblock')
        require(len(chunks) <= volumes[volume]['reserved_lebs'], 'content exceeds volume reservation')
        payload = b''.join(chunks[i] for i in sorted(chunks)); content[volume] = payload
        if expected is not None:
            require(payload[:len(expected)] == expected, 'payload differs from input')
            require(payload[len(expected):] == b'\xff' * (len(payload)-len(expected)), 'unexpected payload padding')
    require(content[0][:4] == b'\xd0\x0d\xfe\xed', 'kernel is not a FIT/FDT')
    require(40 <= struct.unpack_from('>I', content[0], 4)[0] <= len(content[0]), 'invalid FIT length')
    squashfs = squashfs_check(content[1], volumes[1]['reserved_lebs'] * LEB)
    return {'sha256': hashlib.sha256(blob).hexdigest(), 'image_bytes': len(blob), 'image_sequence': next(iter(sequences)), 'volumes': volumes, 'squashfs': squashfs, 'note': 'Layout/content verification only; no hardware acceptance or signature verification'}
