"""Build a pinned Touchable shell with zero-filled guest sections.

Game modules, game shaders, keys, saves and signing material are never inputs.
Populate the reserved sections and resources locally before signing the app.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import struct
import subprocess
import sys
import zipfile

TOUCHABLE_REVISION = '9864d556319acb4312284700f7bfd80f40c54635'
UPSTREAM_REVISION = "2eb5758771f110cd53082b999d88d242766610da"
PAGE = 0x4000


def aligned(n):
    return (n + PAGE - 1) & ~(PAGE - 1)


def validate_layout(layout):
    if set(layout) != {"main", "sdk"}:
        raise ValueError("Only the configured main/sdk modules are accepted")
    for name, item in layout.items():
        if set(item) != {"text_end", "patch_off", "key_page_off", "size"}:
            raise ValueError("Unexpected module dimensions: " + name)
        if any(type(v) is not int or v <= 0 or v > 128 * 1024**2 for v in item.values()):
            raise ValueError("Invalid module dimensions")
        a, p, k, n = (aligned(item["text_end"]), item["patch_off"],
                      item["key_page_off"], item["size"])
        if p % PAGE or k % PAGE or not a <= k < k + PAGE <= p < n:
            raise ValueError("Invalid section alignment/order")
    return layout


def sections(data):
    if struct.unpack_from("<II", data) != (0xFEEDFACF, 0x100000C):
        raise ValueError("Expected a thin ARM64 Mach-O")
    result, at = {}, 32
    for _ in range(struct.unpack_from("<I", data, 16)[0]):
        command, size = struct.unpack_from("<II", data, at)
        if size < 8 or at + size > len(data):
            raise ValueError("Invalid Mach-O command")
        if command == 0x19:
            count = struct.unpack_from("<I", data, at + 64)[0]
            if 72 + count * 80 > size:
                raise ValueError("Invalid Mach-O section table")
            for i in range(count):
                pos = at + 72 + i * 80
                name = data[pos:pos + 16].split(b"\0")[0].decode("ascii")
                segment = data[pos + 16:pos + 32].split(b"\0")[0].decode("ascii")
                length, offset = struct.unpack_from("<QI", data, pos + 40)
                if (segment, name) in result:
                    raise ValueError("Duplicate Mach-O section")
                result[segment, name] = (offset, length)
        at += size
    return result


def expected_sections(layout):
    result = {}
    for name, item in layout.items():
        a, p, n = aligned(item["text_end"]), item["patch_off"], aligned(item["size"])
        result["__TEXT", "__dm" + name + "x0"] = (name, 0, a)
        result["__DM" + name + "W", "__img"] = (name, a, p)
        result["__TEXT", "__dm" + name + "x1"] = (name, p, n)
    return result


def verify_zero_sections(binary, layout):
    data = Path(binary).read_bytes()
    actual = sections(data)
    for key, (_, lo, hi) in expected_sections(layout).items():
        offset, length = actual[key]
        if length != hi - lo or offset + length > len(data):
            raise ValueError("Reserved section dimensions differ: " + str(key))
        if any(data[offset:offset + length]):
            raise ValueError("Guest section contains nonzero content: " + str(key))


def fetch(url, revision, path):
    subprocess.run(["git", "clone", "--filter=blob:none", "--no-checkout", url, str(path)], check=True)
    subprocess.run(["git", "-C", str(path), "checkout", "--detach", revision], check=True)
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    layout = validate_layout(json.loads(os.environ["TOUCHABLE_LAYOUT"]))
    chain = os.environ["TOUCHABLE_SHADER_CHAIN"]
    if not re.fullmatch(r"[0-9a-f]{12}", chain):
        raise ValueError("Invalid shader fingerprint")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    tree = out / "source"
    revision = fetch("https://github.com/MZhao-ouo/Touchable.git", TOUCHABLE_REVISION, tree)
    dependencies = tree / ".touchable/dependencies"
    dependencies.mkdir(parents=True)
    upstream = dependencies / "d2r-mobile"
    if fetch("https://github.com/jhaizhou-ops/d2r-mobile.git", UPSTREAM_REVISION, upstream) != UPSTREAM_REVISION:
        raise ValueError("Unexpected upstream revision")
    subprocess.run(["git", "-C", str(upstream), "apply", "--check", str(tree / "internal/patches/d2r-mobile.patch")], check=True)
    subprocess.run(["git", "-C", str(upstream), "apply", str(tree / "internal/patches/d2r-mobile.patch")], check=True)

    # Enable only the existing shader-source fallback. Do not enable DM_SHELL:
    # its other loader/runtime choices are unrelated to this packaging method.
    metal = upstream / "dm/gfx/nvn/nvn_mtl.m"
    text = metal.read_text()
    start = text.index("static id<MTLLibrary> lib_from_file(")
    guard = text.index("#ifdef DM_SHELL", start)
    text = text[:guard] + text[guard:].replace("#ifdef DM_SHELL", "#if defined(DM_SHELL) || defined(TOUCHABLE_RUNTIME_MSL)", 1)
    metal.write_text(text)
    manifest = tree / "internal/games/rhythm-heaven/game.json"
    game = json.loads(manifest.read_text())
    game["compile_defines"].append("TOUCHABLE_RUNTIME_MSL")
    manifest.write_text(json.dumps(game, indent=2) + "\n")
    work = tree / ".touchable/work/rhythm-heaven"
    modules = work / "patched"
    modules.mkdir(parents=True)
    for name, item in layout.items():
        with (modules / (name + ".patched.bin")).open("wb") as f:
            f.truncate(item["size"])
        header = {"TEXT_END": item["text_end"], "PATCH_OFF": item["patch_off"],
                  "KEY_PAGE_OFF": item["key_page_off"], "KEY_PAGE_SIZE": PAGE}
        (modules / (name + ".fixups.tsv")).write_text("".join("# %s\t%#x\n" % pair for pair in header.items()))
    (work / "base/RomFS").mkdir(parents=True)
    shaders = work / "shaders"
    (shaders / "ios" / ("b-" + chain)).mkdir(parents=True)
    (shaders / "shader_map.tsv").write_text("package\tname\tstage\tkey\tfunctional_key\n")
    (shaders / "chain.json").write_text(json.dumps({"chain": chain}))
    subprocess.run([sys.executable, str(tree / "internal/tools/prepare_ios.py"), "--game", "rhythm-heaven"], cwd=tree, check=True)
    project = tree / ".touchable/build/games/rhythm-heaven/ios-project/RhythmHeaven.xcodeproj"
    derived = out / "derived"
    subprocess.run(["xcodebuild", "-project", str(project), "-scheme", "RhythmHeaven",
                    "-configuration", "Release", "-sdk", "iphoneos", "-derivedDataPath", str(derived),
                    "CODE_SIGNING_ALLOWED=NO", "CODE_SIGNING_REQUIRED=NO", "build"], check=True)
    app = derived / "Build/Products/Release-iphoneos/RhythmHeaven.app"
    verify_zero_sections(app / "RhythmHeaven", layout)
    if any((app / "RomFS").rglob("*")):
        raise ValueError("Unexpected game resources")
    if (app / "embedded.mobileprovision").exists():
        raise ValueError("Unexpected signing profile")
    archive = out / "Touchable-source-shell.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for path in sorted(app.rglob("*")):
            if path.is_file():
                z.write(path, "Payload/" + str(path.relative_to(app.parent)))
    report = dict(touchable_revision=revision, upstream_revision=UPSTREAM_REVISION,
                  product_name=game["product_name"], bundle_id=game["bundle_id"],
                  build_version=game["build_version"], shader_chain=chain, module_layout=layout,
                  guest_sections_verified_zero=True, game_content_included=False, signed=False,
                  requires_local_population=True, runtime_msl=True,
                  sha256=hashlib.sha256(archive.read_bytes()).hexdigest())
    (out / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
