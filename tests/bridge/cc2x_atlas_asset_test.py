import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_cc2x_atlas import build_atla_v1, parse_atla_v1  # noqa: E402
from normalize_cc2x_aircraft_icons import decode_rgba8, make_selector_png, normalize_png  # noqa: E402


def main() -> None:
    spec = json.loads((ROOT / "mods/custom_vehicle_type_prototype/atlas.json").read_text())
    encoded = build_atla_v1(spec)
    # ATLA v1 string records carry a zero flags byte after their length.
    assert encoded[:14] == b"ATLA\x01\x00\x00\x00\x04\x00\x00\x00\x25\x00"
    parsed = parse_atla_v1(encoded)
    assert parsed["version"] == 1
    assert parsed["surfaces"] == [
        "textures/cc2x_aircraft_icons/f16.txtr",
        "textures/cc2x_aircraft_icons/f15.txtr",
        "textures/cc2x_aircraft_icons/f16_layout.txtr",
        "textures/cc2x_aircraft_icons/f15_layout.txtr",
    ]
    assert [(region["name"], region["surface"]) for region in parsed["regions"]] == [
        ("f16", 0), ("f15", 1), ("f16_layout", 2), ("f15_layout", 3)
    ]
    assert [region["bounds"] for region in parsed["regions"][:2]] == [
        [-8.0, 8.0, -8.0, 8.0], [-8.0, 8.0, -8.0, 8.0]
    ]
    # The weapons panel lays out hardpoints in a fixed 64x64 box. Keep layout
    # art on that exact canvas; only the silhouette's alpha bounds are inset.
    for region in parsed["regions"][2:]:
        assert region["bounds"] == [-32.0, 32.0, -32.0, 32.0]
        assert region["uv"] == [0.0, 1.0, 0.0, 1.0]

    source = ROOT / "mods/custom_vehicle_type_prototype/source/textures/cc2x_aircraft_icons"
    for name, source_bounds, selector_bounds in (
        ("f16.png", (8, 5, 25, 27), (2, 1, 13, 15)),
        ("f15.png", (8, 6, 24, 27), (2, 1, 13, 15)),
    ):
        normalized_png, bounds = normalize_png((source / name.replace(".png", "_source.png")).read_bytes())
        assert bounds == source_bounds
        selector_png, actual_selector_bounds = make_selector_png(normalized_png)
        assert actual_selector_bounds == selector_bounds
        selector_width, selector_height, selector_rows = decode_rgba8(selector_png)
        assert (selector_width, selector_height) == (16, 16)
        assert (source / name).read_bytes() == selector_png
        width, height, rows = decode_rgba8(normalized_png)
        colors = {tuple(rows[y][x * 4:x * 4 + 3]) for y in range(height) for x in range(width)
                  if rows[y][x * 4 + 3]}
        alpha = {rows[y][x * 4 + 3] for y in range(height) for x in range(width)}
        assert colors == {(255, 255, 255)}
        assert alpha <= {0, 255}
        txtr = (ROOT / "mods/custom_vehicle_type_prototype/content/textures/cc2x_aircraft_icons"
                / name.replace(".png", ".txtr")).read_bytes()
        compiled_alpha = txtr[26::4]
        source_alpha_bottom_up = bytes(
            selector_rows[y][x * 4 + 3]
            for y in range(15, -1, -1)
            for x in range(16)
        )
        assert compiled_alpha == source_alpha_bottom_up, (
            f"{name} TXTR rows do not match the compiler's bottom-up PNG convention"
        )

    for name, expected_bounds in (("f16_layout.png", (8, 2, 56, 64)),
                                  ("f15_layout.png", (8, 2, 55, 64))):
        width, height, rows = decode_rgba8((source / name).read_bytes())
        assert (width, height) == (64, 64)
        points = [(x, y) for y, row in enumerate(rows) for x in range(width)
                  if row[x * 4 + 3]]
        actual_bounds = (min(x for x, _ in points), min(y for _, y in points),
                         max(x for x, _ in points) + 1, max(y for _, y in points) + 1)
        assert actual_bounds == expected_bounds
        colors = {tuple(rows[y][x * 4:x * 4 + 3]) for x, y in points}
        alpha = {rows[y][x * 4 + 3] for y in range(height) for x in range(width)}
        assert colors == {(255, 255, 255)}
        assert alpha <= {0, 255}

    content = ROOT / "mods/custom_vehicle_type_prototype/content/textures/cc2x_aircraft_icons"
    assert (content / "index.atlas").read_bytes() == encoded
    fixture = ROOT / "mods/custom_vehicle_type_prototype"
    assert (fixture / "source/textures/cc2x_aircraft_icons/index.txt").read_text().splitlines() == [
        "f16.png", "f15.png"
    ]
    atlas_registration = fixture / "bin/data/assets/rom_0#textures#cc2x_aircraft_icons#index_txt.xml"
    assert 'file_path="textures/cc2x_aircraft_icons/index.txt"' in atlas_registration.read_text()
    assert 'compiler="atlas (ui)"' in atlas_registration.read_text()
    for texture in ("f16.txtr", "f15.txtr", "f16_layout.txtr", "f15_layout.txtr"):
        txtr = (content / texture).read_bytes()
        assert txtr[:4] == b"TXTR", f"not a compiled CC2 texture: {texture}"
        assert txtr[15] == 1, f"pixel-art texture unexpectedly has mipmaps: {texture}"
        expected_side = 16 if "_layout" not in texture else 64
        assert int.from_bytes(txtr[9:11], "little") == expected_side
        assert int.from_bytes(txtr[11:13], "little") == expected_side
        assert len(txtr) == 23 + expected_side * expected_side * 4

    bad = {"surfaces": [{"path": "x.txtr"}], "regions": [
        {"name": "bad", "surface": 1, "bounds": [-1, 1, -1, 1], "uv": [0, 1, 0, 1]}
    ]}
    try:
        build_atla_v1(bad)
    except ValueError:
        pass
    else:
        raise AssertionError("out-of-range surface reference was accepted")


if __name__ == "__main__":
    main()
