#!/usr/bin/env python3
import struct
import sys
import zlib
from pathlib import Path

EGA_PALETTE = [
    (0x00, 0x00, 0x00, 0xFF), (0x00, 0x00, 0xAA, 0xFF),
    (0x00, 0xAA, 0x00, 0xFF), (0x00, 0xAA, 0xAA, 0xFF),
    (0xAA, 0x00, 0x00, 0xFF), (0xAA, 0x00, 0xAA, 0xFF),
    (0xAA, 0x55, 0x00, 0xFF), (0xAA, 0xAA, 0xAA, 0xFF),
    (0x55, 0x55, 0x55, 0xFF), (0x55, 0x55, 0xFF, 0xFF),
    (0x55, 0xFF, 0x55, 0xFF), (0x55, 0xFF, 0xFF, 0xFF),
    (0xFF, 0x55, 0x55, 0xFF), (0xFF, 0x55, 0xFF, 0xFF),
    (0xFF, 0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0xFF, 0xFF),
]

ENEMIES = [
    ("CUBE.SHP",   "spinner"),
    ("FB.SHP",     "fireball"),
    ("STAR1.SHP",  "spark"),
    ("STAR2.SHP",  "atom"),
    ("STAR3.SHP",  "firework_bros"),
    ("BALL.SHP",   "beach_ball"),
    ("FROG.SHP",   "blind_toad"),
    ("BIRD.SHP",   "brave_bird"),
    ("BIRD2.SHP",  "shy_bird"),
    ("GLOBE.SHP",  "globe"),
    ("SAUCER.SHP", "saucer"),
    ("ROCK.SHP",   "space_dust"),
    ("BUG.SHP",    "bug"),
    ("BEE.SHP",    "killer_bee"),
]

ITEM_NAMES = [
    "corkscrew", "key", "boots", "lantern", "wand",
    "gems", "crown", "gold",
    "cola_0", "cola_1", "cola_2", "cola_3", "cola_4",
    "shield", "bonus_15"
]

EFFECT_BANKS = [
    ("PLAYER.SPR",    32, "player"),
    ("FIREBALL.SPR",   8, "shot"),
    ("DEATH.SPR",     32, "death"),
    ("TELEPORT.SPR",  32, "teleport"),
    ("ARRIVAL.SPR",   32, "arrival"),
    ("ENEMY_HIT.SPR", 16, "enemy_hit"),
]

AREAS = ["lake", "forest", "space", "base", "cave", "shed", "castle", "comp"]


def save_png_rgba(filepath: Path, width: int, height: int, rows: list[bytearray], scale: int = 1) -> None:
    if scale > 1:
        scaled_rows = []
        for row in rows:
            srow = bytearray()
            for x in range(width):
                px = row[x * 4 : x * 4 + 4]
                srow.extend(px * scale)
            for _ in range(scale):
                scaled_rows.append(srow)
        width *= scale
        height *= scale
        rows = scaled_rows

    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack("!I", len(data)) + tag + data + struct.pack("!I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack("!IIBBBBB", width, height, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    filepath.write_bytes(png)


def decode_frame(data: bytes, offset: int, width: int, height: int, has_mask: bool) -> list[bytearray]:
    row_bytes = width // 8
    plane_size = row_bytes * height
    p0 = offset
    p1 = offset + plane_size
    p2 = offset + plane_size * 2
    p3 = offset + plane_size * 3
    pm = offset + plane_size * 4 if has_mask else None

    rows = []
    for y in range(height):
        row = bytearray(width * 4)
        for x in range(width):
            byte_idx = y * row_bytes + (x // 8)
            bit = 7 - (x % 8)
            if pm is not None and ((data[pm + byte_idx] >> bit) & 1) != 0:
                rgba = (0, 0, 0, 0)
            else:
                color = (
                    ((data[p0 + byte_idx] >> bit) & 1)
                    | (((data[p1 + byte_idx] >> bit) & 1) << 1)
                    | (((data[p2 + byte_idx] >> bit) & 1) << 2)
                    | (((data[p3 + byte_idx] >> bit) & 1) << 3)
                )
                rgba = EGA_PALETTE[color]
            row[x * 4 : x * 4 + 4] = bytes(rgba)
        rows.append(row)
    return rows


def load_bank_frames(filepath: Path, height: int) -> list[list[bytearray]]:
    raw = filepath.read_bytes()
    fsize = len(raw)
    frame_masked = 2 * height * 5
    frame_solid = 2 * height * 4

    data = raw
    if fsize > 4 and (fsize % frame_masked != 0) and (fsize % frame_solid != 0):
        data = raw[4:]

    has_mask = (len(data) % frame_masked == 0)
    frame_size = frame_masked if has_mask else frame_solid
    count = len(data) // frame_size

    return [decode_frame(data, i * frame_size, 16, height, has_mask) for i in range(count)]


def find_file_icase(directory: Path, filename: str) -> Path | None:
    target = filename.lower()
    for p in directory.iterdir():
        if p.name.lower() == target:
            return p
    return None


def export_scale(dos_dir: Path, base_out: Path, scale: int) -> None:
    out_dir = base_out / f"x{scale}"
    (out_dir / "enemies" / "frames").mkdir(parents=True, exist_ok=True)
    (out_dir / "items").mkdir(parents=True, exist_ok=True)
    (out_dir / "effects").mkdir(parents=True, exist_ok=True)
    (out_dir / "tiles").mkdir(parents=True, exist_ok=True)

    # 1. Враги (14 .SHP файлов)
    for idx, (shp_name, title) in enumerate(ENEMIES):
        p = find_file_icase(dos_dir, shp_name)
        if not p:
            continue
        frames = load_bank_frames(p, 16)
        if frames:
            save_png_rgba(out_dir / "enemies" / f"{idx:02d}_{title}.png", 16, 16, frames[0], scale)
            for f_idx, rows in enumerate(frames):
                save_png_rgba(out_dir / "enemies" / "frames" / f"{idx:02d}_{title}_f{f_idx}.png", 16, 16, rows, scale)

    # 2. Предметы (ITEMS.SPR)
    items_p = find_file_icase(dos_dir, "ITEMS.SPR")
    if items_p:
        frames = load_bank_frames(items_p, 16)
        for idx, rows in enumerate(frames):
            name = ITEM_NAMES[idx] if idx < len(ITEM_NAMES) else f"item_{idx}"
            save_png_rgba(out_dir / "items" / f"{idx:02d}_{name}.png", 16, 16, rows, scale)

    # 3. Игрок и эффекты
    for spr_name, height, prefix in EFFECT_BANKS:
        p = find_file_icase(dos_dir, spr_name)
        if not p:
            continue
        frames = load_bank_frames(p, height)
        for f_idx, rows in enumerate(frames):
            save_png_rgba(out_dir / "effects" / f"{prefix}_{f_idx:02d}.png", 16, height, rows, scale)

    # 4. Тайлы уровней (*.TT2)
    for area in AREAS:
        p = find_file_icase(dos_dir, f"{area}.TT2")
        if not p:
            continue
        raw = p.read_bytes()
        if len(raw) <= 4:
            continue
        tile_bytes = 0x80
        count = min((len(raw) - 4) // tile_bytes, 256)
        rows_count = max((count + 15) // 16, 1)
        atlas = [bytearray(256 * 4) for _ in range(rows_count * 16)]
        for i in range(count):
            tile = decode_frame(raw, 4 + i * tile_bytes, 16, 16, False)
            dx = (i % 16) * 16
            dy = (i // 16) * 16
            for ty in range(16):
                atlas[dy + ty][dx * 4 : (dx + 16) * 4] = tile[ty]
        save_png_rgba(out_dir / "tiles" / f"{area}_tiles.png", 256, rows_count * 16, atlas, scale)

    print(f"Готово! Спрайты (масштаб x{scale}) сохранены в: {out_dir}")


def main():
    tools_dir = Path(__file__).resolve().parent
    project_root = tools_dir.parent
    dos_dir = project_root / "assets" / "dos"
    base_out = tools_dir / "sprites"

    if not dos_dir.is_dir():
        print(f"Ошибка: каталог не найден: {dos_dir}", file=sys.stderr)
        sys.exit(1)

    scales = tuple(int(a) for a in sys.argv[1:]) if len(sys.argv) > 1 else (1, 2, 4)
    for sc in scales:
        export_scale(dos_dir, base_out, sc)


if __name__ == "__main__":
    main()
