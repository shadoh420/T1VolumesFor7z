"""End-to-end test suite for T1VolumesFor7z using a sandboxed 7-Zip installation.

Usage:
    python tests/test_plugin.py --dll build-x64/Release/TribesVol.64.dll
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile


# Reference DarkStar PVOL / " VOL" decoder (matches DTS_Skinner / Tribes 1 engine)
POSITION_CODES = {
    3: (0, 0, 0),
    4: (2, 4, 1),
    5: (10, 17, 6),
    6: (36, 47, 24),
    7: (96, 119, 72),
    8: (240, 255, 192),
}


def ref_lzh_expand(data: bytes, offset: int, length: int) -> bytes:
    window_size, lookahead, threshold = 4096, 60, 2
    symbols = 256 - threshold + lookahead
    table = symbols * 2 - 1
    root = table - 1
    frequency = [1] * symbols + [0] * (table + 1 - symbols)
    child = [i + table for i in range(symbols)] + [0] * (table - symbols)
    parent = [0] * (table + symbols)
    for i in range(symbols):
        parent[i + table] = i
    leaf = 0
    for node in range(symbols, table):
        frequency[node] = frequency[leaf] + frequency[leaf + 1]
        child[node] = leaf
        parent[leaf] = parent[leaf + 1] = node
        leaf += 2
    frequency[table] = 0xFFFF
    parent[root] = 0
    position, buf, bits = offset, 0, 0

    def bit():
        nonlocal position, buf, bits
        while bits <= 8 and position < len(data):
            buf |= data[position] << (8 - bits)
            position += 1
            bits += 8
        value = buf >> 15
        buf = (buf << 1) & 0xFFFF
        bits -= 1
        return value

    def rebuild():
        count = 0
        for i in range(table):
            if child[i] >= table:
                frequency[count] = (frequency[i] + 1) // 2
                child[count] = child[i]
                count += 1
        lf = 0
        for node in range(symbols, table):
            total = frequency[lf] + frequency[lf + 1]
            ins = node
            while ins > 0 and total < frequency[ins - 1]:
                ins -= 1
            frequency[ins + 1 : node + 1] = frequency[ins:node]
            child[ins + 1 : node + 1] = child[ins:node]
            frequency[ins] = total
            child[ins] = lf
            lf += 2
        for node in range(table):
            parent[child[node]] = node
            if child[node] < table:
                parent[child[node] + 1] = node

    output = bytearray()
    window = bytearray(b" " * window_size)
    write = window_size - lookahead
    while len(output) < length:
        node = child[root]
        while node < table:
            node = child[node + bit()]
        symbol = node - table
        if frequency[root] == 0x8000:
            rebuild()
        node = parent[symbol + table]
        while True:
            frequency[node] += 1
            val = frequency[node]
            swap = node + 1
            if val > frequency[swap]:
                swap += 1
                while val > frequency[swap]:
                    swap += 1
                swap -= 1
                frequency[node], frequency[swap] = frequency[swap], val
                first, second = child[node], child[swap]
                parent[first] = swap
                if first < table:
                    parent[first + 1] = swap
                child[swap] = first
                parent[second] = node
                if second < table:
                    parent[second + 1] = node
                child[node] = second
                node = swap
            node = parent[node]
            if not node:
                break
        if symbol < 256:
            output.append(symbol)
            window[write] = symbol
            write = (write + 1) & (window_size - 1)
            continue
        code = 0
        for sz in range(1, 9):
            code = (code << 1) | bit()
            lo, hi, base = POSITION_CODES.get(sz, (1, 0, 0))
            if lo <= code <= hi:
                upper = code - base
                break
        else:
            raise ValueError("Invalid LZH position code")
        for _ in range(6):
            upper = (upper << 1) | bit()
        read = write - upper - 1
        for i in range(symbol - 255 + threshold):
            if len(output) == length:
                break
            val = window[(read + i) & (window_size - 1)]
            output.append(val)
            window[write] = val
            write = (write + 1) & (window_size - 1)
    return bytes(output)


def ref_read_vol(path: Path) -> tuple[bytes, dict[str, tuple[bytes, int]]]:
    """Return (magic, {name: (decoded_bytes, compression)}) using independent Python parser."""
    data = path.read_bytes()
    magic = data[:4]
    assert magic in (b"PVOL", b" VOL"), f"Unexpected magic {magic!r}"
    names_at = struct.unpack_from("<I", data, 4)[0]
    if magic == b" VOL" and data[names_at : names_at + 16] == b"vols\0\0\0\0voli\0\0\0\0":
        names_at += 16
    assert data[names_at : names_at + 4] == b"vols"
    nsz = struct.unpack_from("<I", data, names_at + 4)[0]
    names_blob = data[names_at + 8 : names_at + 8 + nsz]
    idx = names_at + 8 + nsz + (nsz & 1)
    assert data[idx : idx + 4] == b"voli"
    isz = struct.unpack_from("<I", data, idx + 4)[0]
    assert isz % 17 == 0
    count = isz // 17
    out: dict[str, tuple[bytes, int]] = {}
    for i in range(count):
        fid, noff, off, sz, comp = struct.unpack_from("<IIIIB", data, idx + 8 + i * 17)
        assert data[off : off + 4] == b"VBLK"
        raw_vblk_sz = struct.unpack_from("<I", data, off + 4)[0]
        assert raw_vblk_sz & 0x80000000, f"Missing 0x80 flag on VBLK at {off}"
        packed = raw_vblk_sz & 0x7FFFFFFF
        nul = names_blob.find(b"\0", noff)
        name = (names_blob[noff:nul] if nul >= 0 else names_blob[noff:]).decode("cp1252")
        payload = data[off + 8 : off + 8 + packed]
        if comp == 0:
            body = payload[:sz]
        elif comp == 1:
            dec = bytearray()
            pos = 0
            while len(dec) < sz:
                ctrl = payload[pos]
                cnt = ctrl & 0x7F
                if ctrl & 0x80:
                    dec += payload[pos + 1 : pos + 2] * cnt
                    pos += 2
                else:
                    dec += payload[pos + 1 : pos + 1 + cnt]
                    pos += 1 + cnt
            body = bytes(dec)
        elif comp == 3:
            body = ref_lzh_expand(payload, 0, sz)
        else:
            raise ValueError(f"Unsupported comp {comp}")
        assert len(body) == sz
        out[name] = (body, comp)
    return magic, out


def sha256(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def run_7z(exe: Path, args: list[str], cwd: Path | None = None, check: bool = True) -> subprocess.CompletedProcess:
    proc = subprocess.run(
        [str(exe), *args],
        cwd=str(cwd) if cwd else None,
        capture_output=True,
        text=True,
    )
    if check and proc.returncode != 0:
        raise AssertionError(
            f"7z failed (rc={proc.returncode}): {' '.join(args)}\nSTDOUT:\n{proc.stdout}\nSTDERR:\n{proc.stderr}"
        )
    return proc


def verify_extract_matches_ref(exe: Path, vol_path: Path, tmp: Path) -> int:
    magic, expected = ref_read_vol(vol_path)
    out_dir = tmp / f"ext_{vol_path.stem}"
    if out_dir.exists():
        shutil.rmtree(out_dir)
    out_dir.mkdir(parents=True)

    run_7z(exe, ["t", str(vol_path)])
    run_7z(exe, ["x", "-y", f"-o{out_dir}", str(vol_path)])

    extracted_files = {}
    for f in out_dir.rglob("*"):
        if f.is_file():
            rel = str(f.relative_to(out_dir)).replace("/", "\\")
            extracted_files[rel.lower()] = f.read_bytes()

    assert len(extracted_files) == len(expected), (
        f"{vol_path.name}: extracted {len(extracted_files)} != expected {len(expected)}"
    )
    for name, (exp_bytes, _comp) in expected.items():
        key = name.replace("/", "\\").lower()
        assert key in extracted_files, f"{vol_path.name}: missing {name!r}"
        assert sha256(extracted_files[key]) == sha256(exp_bytes), f"{vol_path.name}: hash mismatch for {name!r}"
    return len(expected)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--sevenzip-dir", type=Path, default=Path(r"C:\Program Files\7-Zip"))
    args = parser.parse_args()

    dll_path = args.dll.resolve()
    assert dll_path.is_file(), f"DLL not found: {dll_path}"
    assert (args.sevenzip_dir / "7z.exe").is_file(), f"7z.exe not found in {args.sevenzip_dir}"

    with tempfile.TemporaryDirectory(prefix="t1vol_test_") as td:
        root = Path(td)
        sandbox = root / "7z"
        (sandbox / "Formats").mkdir(parents=True)
        shutil.copy2(args.sevenzip_dir / "7z.exe", sandbox / "7z.exe")
        shutil.copy2(args.sevenzip_dir / "7z.dll", sandbox / "7z.dll")
        shutil.copy2(dll_path, sandbox / "Formats" / dll_path.name)
        exe = sandbox / "7z.exe"

        # 1. Registration check
        info = run_7z(exe, ["i"]).stdout
        assert "TribesVol" in info, f"TribesVol not registered:\n{info}"
        assert "P V O L" in info and "20 V O L" in info, f"Signatures missing:\n{info}"
        print("[PASS] 1. 7-Zip loads plugin and registers TribesVol (PVOL + ' VOL', Update enabled)")

        # 2. Create new .vol from scratch (auto-detected by .vol extension + subdirectories + empty file)
        src = root / "src"
        (src / "textures").mkdir(parents=True)
        (src / "scripts" / "sub").mkdir(parents=True)
        files_in = {
            "readme.txt": b"Hello Tribes 1!\r\nShazbot!\r\n",
            "empty.dat": b"",
            r"textures\disc.bmp": bytes(range(256)) * 64,
            r"scripts\sub\autoexec.cs": b'echo("Ready");\n' * 200,
        }
        for rel, content in files_in.items():
            p = src / rel
            p.write_bytes(content)

        created_vol = root / "created.vol"
        run_7z(exe, ["a", str(created_vol), "."], cwd=src)
        magic, parsed = ref_read_vol(created_vol)
        assert magic == b"PVOL"
        assert len(parsed) == len(files_in)
        for rel, content in files_in.items():
            assert rel in parsed, f"Missing {rel!r} in {list(parsed)}"
            assert parsed[rel][0] == content
            assert parsed[rel][1] == 0  # Default Store
        verify_extract_matches_ref(exe, created_vol, root)
        print("[PASS] 2. Create new PVOL archive (subdirs, empty file, binary) + Python/7z round-trip")

        # 3. GUI compression level slider compatibility (-mx=0, -mx=5, -mx=9)
        for level in ("0", "5", "9"):
            lvl_vol = root / f"level_{level}.vol"
            run_7z(exe, ["a", f"-mx={level}", str(lvl_vol), "readme.txt"], cwd=src)
            _, parsed_lvl = ref_read_vol(lvl_vol)
            assert parsed_lvl["readme.txt"][0] == files_in["readme.txt"]
        print("[PASS] 3. Compression level flags (-mx=0/5/9) accepted cleanly (GUI compatible)")

        # 4. RLE compression round-trip (-m0=RLE)
        rle_src = root / "rle_src"
        rle_src.mkdir()
        compressible = b"A" * 5000 + b"XYZ" + b"\x00" * 4096 + b"1234567890" + b"\xFF" * 300
        incompressible = os.urandom(1024)
        (rle_src / "repeat.bin").write_bytes(compressible)
        (rle_src / "random.bin").write_bytes(incompressible)
        rle_vol = root / "rle_test.vol"
        run_7z(exe, ["a", "-m0=RLE", str(rle_vol), "."], cwd=rle_src)
        _, parsed_rle = ref_read_vol(rle_vol)
        assert parsed_rle["repeat.bin"] == (compressible, 1), "Expected RLE compression for repetitive data"
        assert parsed_rle["random.bin"] == (incompressible, 0), "Expected Store fallback for incompressible data"
        verify_extract_matches_ref(exe, rle_vol, root)
        print("[PASS] 4. RLE write (-m0=RLE) + automatic Store fallback + RLE decode round-trip")

        # 5. Modify existing .vol (add, update, delete, rename)
        mod_vol = root / "modified.vol"
        shutil.copy2(created_vol, mod_vol)

        extra = root / "extra"
        extra.mkdir()
        (extra / "added.txt").write_bytes(b"Brand new file in volume")
        run_7z(exe, ["a", str(mod_vol), "added.txt"], cwd=extra)

        (extra / "readme.txt").write_bytes(b"Updated readme content v2!")
        run_7z(exe, ["u", str(mod_vol), "readme.txt"], cwd=extra)

        run_7z(exe, ["d", str(mod_vol), "empty.dat"])
        run_7z(exe, ["rn", str(mod_vol), "added.txt", r"renamed\moved.txt"])

        _, parsed_mod = ref_read_vol(mod_vol)
        assert "empty.dat" not in parsed_mod
        assert "added.txt" not in parsed_mod
        assert parsed_mod["readme.txt"][0] == b"Updated readme content v2!"
        assert parsed_mod[r"renamed\moved.txt"][0] == b"Brand new file in volume"
        assert parsed_mod[r"textures\disc.bmp"][0] == files_in[r"textures\disc.bmp"]
        verify_extract_matches_ref(exe, mod_vol, root)
        print("[PASS] 5. Full modify CRUD (add, update, delete, rename) verified byte-for-byte")

        # 6. Sequential stdout (-so) non-seekable ISequentialOutStream write path
        proc_so = subprocess.run(
            [str(exe), "a", "-tTribesVol", "-so", "dummy.vol", "readme.txt"],
            cwd=str(src),
            capture_output=True,
        )
        assert proc_so.returncode == 0, f"-so failed: {proc_so.stderr.decode(errors='replace')}"
        so_vol = root / "piped.vol"
        so_vol.write_bytes(proc_so.stdout)
        _, parsed_so = ref_read_vol(so_vol)
        assert parsed_so["readme.txt"][0] == files_in["readme.txt"]
        print("[PASS] 6. Non-seekable sequential output stream (-so) write path works")

        # 7. Coexistence with ZIP-based .vol files (modern Tribes 1 HD packs / T2 .vl2)
        zip_vol = root / "modern_hd.vol"
        with zipfile.ZipFile(zip_vol, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("skins/beagle.png", b"\x89PNG\r\n\x1a\nfake_png_payload" * 50)
        list_zip = run_7z(exe, ["l", str(zip_vol)]).stdout
        assert "Type = zip" in list_zip, f"ZIP .vol did not fall through to zip handler:\n{list_zip}"
        run_7z(exe, ["t", str(zip_vol)])
        print("[PASS] 7. ZIP-based .vol files still open with 7-Zip's built-in ZIP handler")

        # 8. Corrupt / truncated PVOL fails cleanly without crash
        bad_vol = root / "corrupt.vol"
        bad_vol.write_bytes(b"PVOL" + struct.pack("<I", 999999) + b"VBLK" + b"\x00" * 20)
        bad_proc = run_7z(exe, ["t", str(bad_vol)], check=False)
        assert bad_proc.returncode != 0
        print("[PASS] 8. Corrupt/truncated PVOL rejected cleanly without crash")

        # 9. Real Tribes 1 & Starsiege volumes (if installed on this machine)
        real_samples = [
            Path(r"C:\Dynamix\TribesRPG\base\gui.vol"),
            Path(r"C:\Dynamix\TribesRPG\base\Darkstar.vol"),
            Path(r"C:\Dynamix\TribesRPG\base\Die_Efficiently.vol"),
            Path(r"C:\Dynamix\Starsiege\scripts.vol"),
            Path(r"C:\Dynamix\Starsiege\campaign\Cybrid (Advanced)\CA1.ted.vol"),
        ]
        tested_real = 0
        for sample in real_samples:
            if sample.is_file():
                n = verify_extract_matches_ref(exe, sample, root)
                print(f"[PASS] 9.{tested_real + 1} Real volume {sample.name}: {n} files matched byte-for-byte")
                tested_real += 1

        # 10. Modify a copy of real Starsiege LZH scripts.vol -> verify untouched LZH entries preserved!
        ss_scripts = Path(r"C:\Dynamix\Starsiege\scripts.vol")
        if ss_scripts.is_file():
            ss_copy = root / "scripts_mod.vol"
            shutil.copy2(ss_scripts, ss_copy)
            run_7z(exe, ["a", str(ss_copy), "added.txt"], cwd=extra)
            run_7z(exe, ["d", str(ss_copy), "console.cs"])
            magic_ss, parsed_ss = ref_read_vol(ss_copy)
            assert magic_ss == b" VOL", "Expected Starsiege ' VOL' header preserved on update"
            assert "console.cs" not in parsed_ss
            assert parsed_ss["added.txt"] == (b"Brand new file in volume", 0)
            assert parsed_ss["actionTable.cs"][1] == 3, "Untouched LZH item should stay LZH-compressed!"
            verify_extract_matches_ref(exe, ss_copy, root)
            print("[PASS] 10. Modifying Starsiege LZH volume preserves ' VOL' header & untouched LZH blocks")

        # 11. Large real Tribes 1 volume stress test (Entities.vol - 970 files, 12 MB)
        entities = Path(r"C:\Dynamix\TribesRPG\base\Entities.vol")
        if entities.is_file():
            run_7z(exe, ["t", str(entities)])
            print("[PASS] 11. Stress test Entities.vol (970 files, 12 MB) passed `7z t`")

        print("\nALL TESTS PASSED!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
