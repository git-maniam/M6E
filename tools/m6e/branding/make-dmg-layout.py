#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Regenerate and check the M6E DMG presentation in browser/branding/m6e.

The build copies three files from the branding directory into the DMG (see
moz_package_extra_args in toolkit/moz.configure and mozpack/dmg.py):

    dsstore        -> /.DS_Store                     window size, icon positions
    background.png -> /.background/background.png    window background
    disk.icns      -> /.VolumeIcon.icns              volume icon

Finder normally writes .DS_Store, so this script writes it directly. It needs
only the Python standard library. It also runs tools that come with macOS or
the Xcode command line tools: swift (generate), sips (check), hdiutil, diskutil,
xattr, codesign (verify) and rsync, SetFile (build, verify).

    make-dmg-layout.py                 write background.png and dsstore
    make-dmg-layout.py check           parse the files in the branding directory
                                       and compare them with the layout below
    make-dmg-layout.py verify          build a scratch DMG from them the way
                                       mozpack/dmg.py does, attach it and check
                                       what Finder will read
    make-dmg-layout.py verify FILE.dmg attach FILE.dmg read-only and check it
    make-dmg-layout.py dump FILE       print the records of a .DS_Store file
    make-dmg-layout.py build M6E.app OUT.dmg
                                       build the DMG from an application that
                                       changed after ./mach package (signed)

The background is drawn by dmg-background.swift. If you change the volume name,
the window or the icon positions, rerun this script without arguments.
make-icons.sh runs this script as its last step.

dsstore does not depend on the machine. background.png is drawn with the system
font and PNG encoder, so its bytes can differ between macOS versions: commit
the pair that was generated together and run check after a regeneration.
"""

import argparse
import contextlib
import ctypes
import datetime
import functools
import os
import plistlib
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

TOP = Path(__file__).resolve().parents[3]
BRANDING = TOP / "browser/branding/m6e"
BACKGROUND_TOOL = Path(__file__).resolve().parent / "dmg-background.swift"

VOLUME_NAME = "M6E"
APP_NAME = "M6E.app"
APPLICATIONS_LINK = " "
BACKGROUND_DIR = ".background"
BACKGROUND_NAME = "background.png"

WINDOW_X, WINDOW_Y = 200, 120
WINDOW_WIDTH, WINDOW_HEIGHT = 680, 460
WINDOW_BOUNDS = (
    "{{"
    + f"{WINDOW_X}, {WINDOW_Y}"
    + "}, {"
    + f"{WINDOW_WIDTH}, {WINDOW_HEIGHT}"
    + "}}"
)
BACKGROUND_SCALE = 2
ICON_SIZE = 128
TEXT_SIZE = 12
APP_POSITION = (190, 215)
APPLICATIONS_POSITION = (490, 215)
TITLE = "Install M6E"
CAPTION = "Drag M6E into the Applications folder"

# Fixed so that the output is the same on every run. Nothing here has to match
# the volume that makehybrid creates: Finder falls back to the path.
FIXED_DATE = datetime.datetime(2026, 1, 1)
FIXED_VOLUME_UUID = "5C3B0E0A-6E36-4D8E-9A52-4B6D4E364D36"
FIXED_VOLUME_SIZE = 700 * 1024 * 1024
BACKGROUND_DIR_CNID = 19
BACKGROUND_FILE_CNID = 20
VOLUMES_DIR_CNID = 14547
VOLUME_ROOT_CNID = 2

FORBIDDEN = re.compile(r"firefox|nightly|mozilla", re.IGNORECASE)


def run(*cmd, **kwargs):
    return subprocess.run(cmd, check=True, **kwargs)


# ---------------------------------------------------------------------------
# Alias record (the legacy backgroundImageAlias in the icvp plist)


def hfs_seconds(date):
    return int((date - datetime.datetime(1904, 1, 1)).total_seconds())


def build_alias():
    created = hfs_seconds(FIXED_DATE)
    utc = struct.pack(">HIH", created >> 32, created & 0xFFFFFFFF, 0)
    name = BACKGROUND_NAME
    header = struct.pack(
        ">4sHhh28pI2shI64pII4s4shhI2s10s",
        b"\0\0\0\0",
        0,
        2,
        0,
        VOLUME_NAME.encode(),
        created,
        b"H+",
        5,
        BACKGROUND_DIR_CNID,
        name.encode(),
        BACKGROUND_FILE_CNID,
        created,
        b"\0\0\0\0",
        b"\0\0\0\0",
        -1,
        -1,
        0x0D02,
        b"\0\0",
        b"\0" * 10,
    )

    def tag(number, data):
        return struct.pack(">HH", number, len(data)) + data + (b"\0" * (len(data) % 2))

    def unicode_string(text):
        return struct.pack(">H", len(text)) + text.encode("utf-16-be")

    extras = b"".join([
        tag(0, BACKGROUND_DIR.encode()),
        tag(0x10, utc),
        tag(0x11, utc),
        tag(1, struct.pack(">I", BACKGROUND_DIR_CNID)),
        tag(2, f"{VOLUME_NAME}:{BACKGROUND_DIR}:\0{name}".encode()),
        tag(14, unicode_string(name)),
        tag(15, unicode_string(VOLUME_NAME)),
        tag(18, f"/{BACKGROUND_DIR}/{name}".encode()),
        tag(19, f"/Volumes/{VOLUME_NAME}".encode()),
        struct.pack(">Hh", 0xFFFF, 0),
    ])
    record = header + extras
    return record[:4] + struct.pack(">H", len(record)) + record[6:]


def parse_alias(data):
    fields = struct.unpack(">4sHhh28pI2shI64pII4s4shhI2s10s", data[:150])
    info = {
        "size": fields[1],
        "version": fields[2],
        "kind": fields[3],
        "volume": fields[4].decode(),
        "file": fields[9].decode(),
    }
    pos = 150
    while pos + 4 <= len(data):
        number, length = struct.unpack(">Hh", data[pos : pos + 4])
        if number == 0xFFFF:
            break
        body = data[pos + 4 : pos + 4 + length]
        pos += 4 + length + (length % 2)
        if number == 18:
            info["path"] = body.decode()
        elif number == 19:
            info["mount"] = body.decode()
        elif number == 15:
            info["volume_unicode"] = body[2:].decode("utf-16-be")
    if info["size"] != len(data):
        raise ValueError("alias size field does not match its length")
    return info


# ---------------------------------------------------------------------------
# Bookmark data (the pBBk record). Layout as written by Finder for a file on
# a mounted volume, without the chain of containing disk images.


class Bookmark:
    def __init__(self):
        self.items = bytearray()

    def add(self, kind, data):
        offset = 4 + len(self.items)
        self.items += struct.pack("<II", len(data), kind) + data
        self.items += b"\0" * (-len(data) % 4)
        return offset

    def string(self, text):
        return self.add(0x101, text.encode())

    def data(self, raw):
        return self.add(0x201, raw)

    def integer(self, value):
        return self.add(0x303, struct.pack("<i", value))

    def long(self, value):
        return self.add(0x304, struct.pack("<q", value))

    def boolean(self, value):
        return self.add(0x501 if value else 0x500, b"")

    def date(self, value):
        seconds = (value - datetime.datetime(2001, 1, 1)).total_seconds()
        return self.add(0x400, struct.pack(">d", seconds))

    def url(self, text):
        return self.add(0x901, text.encode())

    def array(self, offsets):
        return self.add(0x601, b"".join(struct.pack("<I", o) for o in offsets))


def build_bookmark():
    mount = f"/Volumes/{VOLUME_NAME}"
    path = ["Volumes", VOLUME_NAME, BACKGROUND_DIR, BACKGROUND_NAME]
    b = Bookmark()
    options = b.integer(0x20000200)
    entries = {
        0x1004: b.array([b.string(part) for part in path]),
        0x1005: b.array([
            b.long(n)
            for n in (
                VOLUMES_DIR_CNID,
                VOLUME_ROOT_CNID,
                BACKGROUND_DIR_CNID,
                BACKGROUND_FILE_CNID,
            )
        ]),
        0x1010: b.data(struct.pack("<QQQ", 1, 0x0F, 0)),
        0x1040: b.date(FIXED_DATE),
        0x2002: b.string(mount),
        0x2005: b.url(f"file://{mount}/"),
        0x2010: b.string(VOLUME_NAME),
        0x2011: b.string(FIXED_VOLUME_UUID),
        0x2012: b.long(FIXED_VOLUME_SIZE),
        0x2013: b.date(FIXED_DATE),
        0x2020: b.data(struct.pack("<QQQ", 0x100000265, 0x1000013EF, 0)),
        0xD001: b.boolean(True),
        0xD010: options,
    }
    toc_offset = 4 + len(b.items)
    toc = struct.pack("<IIII", 0xFFFFFFFE, 1, 0, len(entries))
    toc += b"".join(struct.pack("<III", k, v, 0) for k, v in sorted(entries.items()))
    toc = struct.pack("<I", len(toc) - 4) + toc
    body = struct.pack("<I", toc_offset) + bytes(b.items) + toc
    total = 0x30 + len(body)
    return struct.pack("<4sIII", b"book", total, 0x10040000, 0x30) + b"\0" * 32 + body


def parse_bookmark(data):
    magic, total, version, header_size = struct.unpack("<4sIII", data[:16])
    if magic != b"book" or total != len(data) or header_size != 0x30:
        raise ValueError("not a bookmark of the expected form")
    base = 0x30

    def value(offset):
        length, kind = struct.unpack("<II", data[base + offset : base + offset + 8])
        body = data[base + offset + 8 : base + offset + 8 + length]
        if kind in (0x101, 0x901):
            return body.decode()
        if kind == 0x601:
            return [
                value(struct.unpack("<I", body[i : i + 4])[0])
                for i in range(0, len(body), 4)
            ]
        if kind == 0x304:
            return struct.unpack("<q", body)[0]
        if kind == 0x303:
            return struct.unpack("<i", body)[0]
        return body

    (toc_offset,) = struct.unpack("<I", data[base : base + 4])
    toc = base + toc_offset
    size, magic, _, _, count = struct.unpack("<IIIII", data[toc : toc + 20])
    if magic != 0xFFFFFFFE or size != 12 + 12 * count:
        raise ValueError("bad bookmark table of contents")
    entries = {}
    for i in range(count):
        key, offset, _ = struct.unpack(
            "<III", data[toc + 20 + 12 * i : toc + 32 + 12 * i]
        )
        entries[key] = value(offset)
    return entries


# ---------------------------------------------------------------------------
# .DS_Store: a buddy allocator holding a B-tree. M6E needs a handful of records,
# so the tree is one leaf node. The block layout is the one Finder writes.

PAGE = 0x1000


def ds_record(name, code, kind, value):
    if kind == "blob":
        payload = struct.pack(">I", len(value)) + value
    elif kind == "long":
        payload = struct.pack(">I", value)
    elif kind == "type":
        payload = value.encode()
    elif kind == "bool":
        payload = struct.pack("?", value)
    else:
        raise ValueError(kind)
    return (
        struct.pack(">I", len(name))
        + name.encode("utf-16-be")
        + code.encode()
        + kind.encode()
        + payload
    )


def icon_location(x, y):
    return struct.pack(">IIII", x, y, 0xFFFFFFFF, 0xFFFF0000)


def window_plist():
    return plistlib.dumps(
        {
            "ContainerShowSidebar": False,
            "PreviewPaneVisibility": False,
            "ShowPathbar": False,
            "ShowSidebar": False,
            "ShowStatusBar": False,
            "ShowTabView": False,
            "ShowToolbar": False,
            "WindowBounds": WINDOW_BOUNDS,
        },
        fmt=plistlib.FMT_BINARY,
    )


def icon_view_plist():
    return plistlib.dumps(
        {
            "arrangeBy": "none",
            "backgroundColorBlue": 1.0,
            "backgroundColorGreen": 1.0,
            "backgroundColorRed": 1.0,
            "backgroundImageAlias": build_alias(),
            "backgroundType": 2,
            "gridOffsetX": 0.0,
            "gridOffsetY": 0.0,
            "gridSpacing": 100.0,
            "iconSize": float(ICON_SIZE),
            "labelOnBottom": True,
            "showIconPreview": True,
            "showItemInfo": False,
            "textSize": float(TEXT_SIZE),
            "viewOptionsVersion": 1,
        },
        fmt=plistlib.FMT_BINARY,
    )


def build_dsstore():
    records = {
        (".", "bwsp"): ("blob", window_plist()),
        (".", "icvp"): ("blob", icon_view_plist()),
        (".", "pBBk"): ("blob", build_bookmark()),
        (".", "vSrn"): ("long", 1),
        (APPLICATIONS_LINK, "Iloc"): ("blob", icon_location(*APPLICATIONS_POSITION)),
        (APP_NAME, "Iloc"): ("blob", icon_location(*APP_POSITION)),
    }
    ordered = sorted(records, key=lambda key: (key[0].lower(), key[1]))
    node = struct.pack(">II", 0, len(ordered))
    for name, code in ordered:
        kind, value = records[(name, code)]
        node += ds_record(name, code, kind, value)
    if len(node) > PAGE:
        raise ValueError("the layout no longer fits in one B-tree node")

    # Block 0 is the allocator table, block 1 the tree header, block 2 the node.
    info_offset, info_width = 0x2000, 11
    tree_offset, tree_width = 0x40, 5
    node_offset, node_width = 0x1000, 12
    addresses = [
        info_offset | info_width,
        tree_offset | tree_width,
        node_offset | node_width,
    ]
    table = struct.pack(">II", len(addresses), 0) + b"".join(
        struct.pack(">I", a) for a in addresses
    )
    table += b"\0" * (-len(addresses) % 256 * 4)
    table += struct.pack(">I", 1) + bytes([4]) + b"DSDB" + struct.pack(">I", 1)
    free = {
        5: [0x20, 0x60],
        7: [0x80],
        8: [0x100],
        9: [0x200],
        10: [0x400],
        11: [0x800, 0x2800],
        12: [0x3000],
    }
    free.update({width: [1 << width] for width in range(14, 31)})
    for width in range(32):
        offsets = free.get(width, [])
        table += struct.pack(">I", len(offsets))
        table += b"".join(struct.pack(">I", o) for o in offsets)

    tree = struct.pack(">IIIII", 2, 0, len(ordered), 1, PAGE).ljust(32, b"\0")
    out = bytearray(4 + info_offset + (1 << info_width))
    out[0:4] = struct.pack(">I", 1)
    header = struct.pack(">4sIII", b"Bud1", info_offset, 1 << info_width, info_offset)
    header += bytes([0, 0, 0x10, 0x0C]) + b"\0" * 12
    out[4 : 4 + len(header)] = header
    out[4 + tree_offset : 4 + tree_offset + len(tree)] = tree
    out[4 + node_offset : 4 + node_offset + len(node)] = node
    out[4 + info_offset : 4 + info_offset + len(table)] = table
    return bytes(out)


# ---------------------------------------------------------------------------
# .DS_Store reader, used by check and verify. Written separately from the
# writer above so that the two cross-check each other.


def read_dsstore(data):
    if data[:4] != b"\0\0\0\1":
        raise ValueError("bad .DS_Store signature")
    magic, info_offset, info_size, again = struct.unpack(">4sIII", data[4:20])
    if magic != b"Bud1" or info_offset != again:
        raise ValueError("bad buddy allocator header")
    info = data[4 + info_offset : 4 + info_offset + info_size]
    (count,) = struct.unpack(">I", info[:4])
    addresses = struct.unpack(f">{count}I", info[8 : 8 + 4 * count])
    pos = 8 + 4 * (count + -count % 256)
    (toc_count,) = struct.unpack(">I", info[pos : pos + 4])
    pos += 4
    toc = {}
    for _ in range(toc_count):
        length = info[pos]
        name = info[pos + 1 : pos + 1 + length].decode()
        (toc[name],) = struct.unpack(">I", info[pos + 1 + length : pos + 5 + length])
        pos += 5 + length
    for width in range(32):
        (n,) = struct.unpack(">I", info[pos : pos + 4])
        pos += 4 + 4 * n

    def block(number):
        address = addresses[number]
        start = 4 + (address & ~0x1F)
        return data[start : start + (1 << (address & 0x1F))]

    root, levels, total, nodes, page = struct.unpack(">IIIII", block(toc["DSDB"])[:20])
    records = []

    def read_value(buf, pos, kind):
        if kind == "bool":
            return bool(buf[pos]), pos + 1
        if kind in ("long", "shor"):
            return struct.unpack(">I", buf[pos : pos + 4])[0], pos + 4
        if kind == "type":
            return buf[pos : pos + 4].decode(), pos + 4
        if kind in ("comp", "dutc"):
            return struct.unpack(">Q", buf[pos : pos + 8])[0], pos + 8
        (length,) = struct.unpack(">I", buf[pos : pos + 4])
        pos += 4
        if kind == "blob":
            return bytes(buf[pos : pos + length]), pos + length
        if kind == "ustr":
            return buf[pos : pos + 2 * length].decode("utf-16-be"), pos + 2 * length
        raise ValueError("unknown record type " + kind)

    def read_record(buf, pos):
        (length,) = struct.unpack(">I", buf[pos : pos + 4])
        name = buf[pos + 4 : pos + 4 + 2 * length].decode("utf-16-be")
        pos += 4 + 2 * length
        code, kind = buf[pos : pos + 4].decode(), buf[pos + 4 : pos + 8].decode()
        value, pos = read_value(buf, pos + 8, kind)
        return (name, code, kind, value), pos

    def walk(number):
        buf = block(number)
        pointer, n = struct.unpack(">II", buf[:8])
        pos = 8
        for _ in range(n):
            if pointer:
                walk(struct.unpack(">I", buf[pos : pos + 4])[0])
                pos += 4
            record, pos = read_record(buf, pos)
            records.append(record)
        if pointer:
            walk(pointer)

    walk(root)
    if len(records) != total:
        raise ValueError(f"{len(records)} records, but the header says {total}")
    return records


def describe(records):
    lines = []
    for name, code, kind, raw in records:
        shown = raw
        if kind == "blob" and raw[:6] == b"bplist":
            shown = plistlib.loads(raw)
            if "backgroundImageAlias" in shown:
                try:
                    shown["backgroundImageAlias"] = parse_alias(
                        shown["backgroundImageAlias"]
                    )
                except (ValueError, struct.error):
                    shown["backgroundImageAlias"] = shown["backgroundImageAlias"].hex()
        elif code in ("pBBk", "pBB0"):
            try:
                shown = parse_bookmark(raw)
            except (ValueError, struct.error):
                shown = f"{len(raw)} bytes, not a whole bookmark: {raw[:16].hex()}"
        elif kind == "blob":
            shown = struct.unpack(">IIII", raw) if len(raw) == 16 else raw.hex()
        lines.append(f"{name!r} {code} {kind}: {shown}")
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# Generating


def generate():
    BRANDING.mkdir(parents=True, exist_ok=True)
    run(
        "swift",
        str(BACKGROUND_TOOL),
        str(BRANDING / "background.png"),
        str(WINDOW_WIDTH),
        str(WINDOW_HEIGHT),
        str(BACKGROUND_SCALE),
        *map(str, APP_POSITION),
        *map(str, APPLICATIONS_POSITION),
        TITLE,
        CAPTION,
    )
    (BRANDING / "dsstore").write_bytes(build_dsstore())
    print(f"Wrote {BRANDING / 'background.png'} and {BRANDING / 'dsstore'}")


# ---------------------------------------------------------------------------
# Checking


class Checks:
    def __init__(self):
        self.failures = 0

    def expect(self, ok, text):
        print(("ok    " if ok else "FAIL  ") + text)
        if not ok:
            self.failures += 1

    def attempt(self, text, check):
        """Run check(), which returns a truth value; an exception is a failure."""
        try:
            ok = check()
        except Exception as error:
            text += f" ({type(error).__name__}: {error})"
            ok = False
        self.expect(ok, text)

    def note(self, text):
        print("note  " + text)


def sips_value(path, key):
    out = run("sips", "-g", key, str(path), stdout=subprocess.PIPE, text=True).stdout
    return out.split(":")[-1].strip()


def check_records(checks, data):
    records = read_dsstore(data)
    by_key = {(name, code): value for name, code, _, value in records}
    checks.expect(
        {name for name, _, _, _ in records} == {".", APPLICATIONS_LINK, APP_NAME},
        "records exist only for the window, the app and the Applications link",
    )
    checks.expect(
        [(n.lower(), c) for n, c, _, _ in records]
        == sorted((n.lower(), c) for n, c, _, _ in records),
        "records are in Finder's sort order",
    )
    expected_bounds = WINDOW_BOUNDS

    def window():
        return plistlib.loads(by_key[(".", "bwsp")])

    def view():
        return plistlib.loads(by_key[(".", "icvp")])

    checks.attempt(
        "window bounds " + expected_bounds,
        lambda: window()["WindowBounds"] == expected_bounds,
    )
    checks.attempt(
        "no toolbar, sidebar, status bar, path bar or tab bar",
        lambda: not any(v for k, v in window().items() if k.startswith("Show")),
    )
    checks.attempt("the background is a picture", lambda: view()["backgroundType"] == 2)
    checks.attempt(f"icon size {ICON_SIZE}", lambda: view()["iconSize"] == ICON_SIZE)
    checks.attempt(
        "icons are not arranged automatically", lambda: view()["arrangeBy"] == "none"
    )

    def alias_ok():
        alias = parse_alias(view()["backgroundImageAlias"])
        return (
            alias["volume"] == VOLUME_NAME
            and alias["path"] == f"/{BACKGROUND_DIR}/{BACKGROUND_NAME}"
            and alias["mount"] == f"/Volumes/{VOLUME_NAME}"
        )

    checks.attempt(
        f"alias points at /Volumes/{VOLUME_NAME}/{BACKGROUND_DIR}/{BACKGROUND_NAME}",
        alias_ok,
    )

    def bookmark_ok():
        bookmark = parse_bookmark(by_key[(".", "pBBk")])
        return (
            bookmark[0x1004]
            == ["Volumes", VOLUME_NAME, BACKGROUND_DIR, BACKGROUND_NAME]
            and bookmark[0x2010] == VOLUME_NAME
        )

    checks.attempt(
        "bookmark points at the same file on the volume " + VOLUME_NAME, bookmark_ok
    )
    for name, position in (
        (APP_NAME, APP_POSITION),
        (APPLICATIONS_LINK, APPLICATIONS_POSITION),
    ):
        checks.attempt(
            f"icon {name!r} at {position}",
            lambda name=name, position=position: (
                struct.unpack(">II", by_key[(name, "Iloc")][:8]) == position
            ),
        )
    return by_key


def check_text(checks, label, data):
    lowered = data.lower()
    words = ("firefox", "nightly", "mozilla")
    hit = any(
        word.encode(encoding) in lowered
        for word in words
        for encoding in ("utf-8", "utf-16-be", "utf-16-le")
    )
    checks.expect(not hit, f"no Firefox, Nightly or Mozilla text in {label}")


def check_branding(checks):
    dsstore = (BRANDING / "dsstore").read_bytes()
    check_records(checks, dsstore)
    check_text(checks, "dsstore", dsstore)
    background = BRANDING / "background.png"
    size = (
        int(sips_value(background, "pixelWidth")),
        int(sips_value(background, "pixelHeight")),
    )
    checks.expect(
        size == (WINDOW_WIDTH * BACKGROUND_SCALE, WINDOW_HEIGHT * BACKGROUND_SCALE),
        f"background.png is {size[0]}x{size[1]} pixels",
    )
    checks.expect(
        float(sips_value(background, "dpiWidth")) == 72 * BACKGROUND_SCALE,
        f"background.png is tagged {72 * BACKGROUND_SCALE} DPI, so Finder shows it at "
        f"{WINDOW_WIDTH}x{WINDOW_HEIGHT} points",
    )
    check_text(checks, "background.png", background.read_bytes())
    icon = BRANDING / "disk.icns"
    checks.expect(
        icon.read_bytes() == (BRANDING / "firefox.icns").read_bytes(),
        "disk.icns is the M6E application icon (same bytes as firefox.icns)",
    )


# ---------------------------------------------------------------------------
# Verifying the real thing: build a DMG the way mozpack/dmg.py does, attach it
# and read back what Finder reads.


def stage_fake_app(stage):
    """A tiny application folder, for testing the layout without a build."""
    app = stage / APP_NAME / "Contents"
    (app / "MacOS").mkdir(parents=True)
    (app / "Resources").mkdir()
    (app / "MacOS/m6e").write_text("#!/bin/sh\n")
    (app / "MacOS/m6e").chmod(0o755)
    shutil.copy(BRANDING / "firefox.icns", app / "Resources/firefox.icns")
    (app / "Info.plist").write_bytes(
        plistlib.dumps({
            "CFBundleName": VOLUME_NAME,
            "CFBundleExecutable": "m6e",
            "CFBundleIdentifier": "org.m6e.scratch",
            "CFBundleIconFile": "firefox",
            "CFBundlePackageType": "APPL",
        })
    )


def stage_layout(stage):
    """Add what mozpack.dmg.create_dmg adds around the application."""
    shutil.copy(BRANDING / "dsstore", stage / ".DS_Store")
    (stage / BACKGROUND_DIR).mkdir()
    shutil.copy(BRANDING / "background.png", stage / BACKGROUND_DIR / BACKGROUND_NAME)
    shutil.copy(BRANDING / "disk.icns", stage / ".VolumeIcon.icns")
    os.symlink("/Applications", stage / APPLICATIONS_LINK)
    run("SetFile", "-a", "C", str(stage))
    run("chmod", "-R", "a+rX,a-st,u+w,go-w", str(stage))


def make_dmg(stage, work, out):
    hybrid = work / "hybrid.dmg"
    run(
        "hdiutil",
        "makehybrid",
        "-hfs",
        "-hfs-volume-name",
        VOLUME_NAME,
        "-hfs-openfolder",
        str(stage),
        "-ov",
        str(stage),
        "-o",
        str(hybrid),
        stdout=subprocess.DEVNULL,
    )
    run(
        "hdiutil",
        "convert",
        "-format",
        "UDBZ",
        "-imagekey",
        "bzip2-level=9",
        "-ov",
        str(hybrid),
        "-o",
        str(out),
        stdout=subprocess.DEVNULL,
    )


def build_scratch_dmg(work):
    stage = work / "stage"
    stage.mkdir()
    stage_fake_app(stage)
    stage_layout(stage)
    out = work / f"{VOLUME_NAME.lower()}-scratch.dmg"
    make_dmg(stage, work, out)
    return out


def build_dmg(app, out):
    """Build the DMG that ./mach package builds, from the given application.

    Use this for an application that was changed after packaging, such as one
    that was signed: the DMG made by ./mach package holds the unsigned copy.
    """
    app = Path(app).resolve()
    if app.name != APP_NAME or not (app / "Contents/Info.plist").is_file():
        raise SystemExit(f"{app} is not {APP_NAME}")
    out = Path(out).resolve()
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="m6e-dmg-build.") as tmp:
        stage = Path(tmp) / "stage"
        stage.mkdir()
        run("rsync", "-a", "--copy-unsafe-links", str(app), str(stage) + "/")
        stage_layout(stage)
        make_dmg(stage, Path(tmp), out)
    print(f"Wrote {out}")


@contextlib.contextmanager
def attached(dmg):
    out = run(
        "hdiutil",
        "attach",
        "-readonly",
        "-noverify",
        "-noautoopen",
        "-plist",
        str(dmg),
        stdout=subprocess.PIPE,
    ).stdout
    entities = plistlib.loads(out)["system-entities"]
    mounts = [e["mount-point"] for e in entities if "mount-point" in e]
    # The whole disk: detaching only the volume leaves the image attached.
    device = re.sub(
        r"s\d+$", "", next(e["dev-entry"] for e in entities if "mount-point" in e)
    )
    try:
        yield Path(mounts[0])
    finally:
        for attempt in range(5):
            force = ["-force"] if attempt == 4 else []
            done = subprocess.run(
                ["hdiutil", "detach", *force, device],
                capture_output=True,
                text=True,
                check=False,
            )
            if done.returncode == 0:
                break
            time.sleep(1)
        else:
            print(
                f"warning: {dmg} is still attached as {device} "
                f"({done.stderr.strip()}); run: hdiutil detach -force {device}",
                file=sys.stderr,
            )


def volume_name(mount):
    info = run("diskutil", "info", "-plist", str(mount), stdout=subprocess.PIPE).stdout
    return plistlib.loads(info)["VolumeName"]


@functools.cache
def corefoundation():
    cf = ctypes.CDLL(
        "/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation"
    )
    vp = ctypes.c_void_p
    cf.CFDataCreate.restype = vp
    cf.CFDataCreate.argtypes = [vp, ctypes.c_char_p, ctypes.c_long]
    cf.CFURLCreateBookmarkDataFromAliasRecord.restype = vp
    cf.CFURLCreateBookmarkDataFromAliasRecord.argtypes = [vp, vp]
    cf.CFURLCreateByResolvingBookmarkData.restype = vp
    cf.CFURLCreateByResolvingBookmarkData.argtypes = [
        vp,
        vp,
        ctypes.c_ulong,
        vp,
        vp,
        ctypes.POINTER(ctypes.c_ubyte),
        ctypes.POINTER(vp),
    ]
    cf.CFURLCopyFileSystemPath.restype = vp
    cf.CFURLCopyFileSystemPath.argtypes = [vp, ctypes.c_long]
    cf.CFStringGetCString.restype = ctypes.c_bool
    cf.CFStringGetCString.argtypes = [
        vp,
        ctypes.c_char_p,
        ctypes.c_long,
        ctypes.c_uint32,
    ]
    return cf


def resolve(kind, raw):
    """Resolve a bookmark or alias record with CoreFoundation; None on failure."""
    cf = corefoundation()
    data = cf.CFDataCreate(None, raw, len(raw))
    if kind == "alias":
        data = cf.CFURLCreateBookmarkDataFromAliasRecord(None, data)
        if not data:
            return None
    stale = ctypes.c_ubyte(0)
    error = ctypes.c_void_p()
    # WithoutUI (1 << 8) and WithoutMounting (1 << 9)
    url = cf.CFURLCreateByResolvingBookmarkData(
        None, data, 0x300, None, None, ctypes.byref(stale), ctypes.byref(error)
    )
    if not url:
        return None
    path = cf.CFURLCopyFileSystemPath(url, 0)
    buf = ctypes.create_string_buffer(1024)
    cf.CFStringGetCString(path, buf, 1024, 0x08000100)
    return buf.value.decode()


def check_volume(checks, mount, resolve_links):
    checks.attempt(
        f"volume name is {VOLUME_NAME!r}", lambda: volume_name(mount) == VOLUME_NAME
    )
    visible = sorted(p.name for p in mount.iterdir() if not p.name.startswith("."))
    checks.expect(
        visible == sorted([APP_NAME, APPLICATIONS_LINK]),
        f"visible items are the app and the Applications link: {visible}",
    )
    link = mount / APPLICATIONS_LINK
    checks.expect(
        link.is_symlink() and os.readlink(link) == "/Applications",
        "the link named a single space points at /Applications",
    )
    checks.expect(
        not any(FORBIDDEN.search(p.name) for p in mount.iterdir()),
        "no Firefox, Nightly or Mozilla in the names at the volume root",
    )
    finder_info = run(
        "xattr",
        "-px",
        "com.apple.FinderInfo",
        str(mount),
        stdout=subprocess.PIPE,
        text=True,
    ).stdout.split()
    checks.expect(
        len(finder_info) >= 10 and int(finder_info[8], 16) & 0x04 != 0,
        "the volume has the custom-icon flag",
    )
    for source, target in (
        ("dsstore", ".DS_Store"),
        ("background.png", f"{BACKGROUND_DIR}/{BACKGROUND_NAME}"),
        ("disk.icns", ".VolumeIcon.icns"),
    ):
        checks.expect(
            (BRANDING / source).read_bytes() == (mount / target).read_bytes(),
            f"/{target} is browser/branding/m6e/{source}",
        )
    data = (mount / ".DS_Store").read_bytes()
    by_key = check_records(checks, data)
    checks.expect(
        (mount / BACKGROUND_DIR / BACKGROUND_NAME).is_file(),
        "the background file exists at the path the .DS_Store names",
    )
    for item in (APP_NAME, APPLICATIONS_LINK):
        checks.expect(
            os.path.lexists(mount / item),
            f"{item!r} has an Iloc record and exists on the volume",
        )
    target = str(mount / BACKGROUND_DIR / BACKGROUND_NAME)
    if resolve_links and mount == Path("/Volumes") / VOLUME_NAME:
        checks.attempt(
            "CoreFoundation resolves the bookmark to the background",
            lambda: resolve("bookmark", by_key[(".", "pBBk")]) == target,
        )
        checks.attempt(
            "CoreFoundation resolves the alias to the background",
            lambda: (
                resolve(
                    "alias",
                    plistlib.loads(by_key[(".", "icvp")])["backgroundImageAlias"],
                )
                == target
            ),
        )
    else:
        checks.note(
            f"mounted at {mount}, and not alone under the name {VOLUME_NAME}: the "
            "bookmark and alias were not resolved. Detach the other volume and rerun."
        )
    info = mount / APP_NAME / "Contents/Info.plist"
    if info.exists():
        plist = plistlib.loads(info.read_bytes())
        checks.note(
            f"app bundle: {plist.get('CFBundleName')} ({plist.get('CFBundleIdentifier')})"
        )
    check_signature(checks, mount / APP_NAME)


def check_signature(checks, app):
    """Check the signature of the application on the volume, if it has one.

    The check is not --strict: hdiutil makehybrid gives every file on the volume
    a com.apple.FinderInfo attribute, which --strict refuses. Verify strictly
    before building the DMG, or on a copy after xattr -cr.
    """
    if not (app / "Contents/_CodeSignature/CodeResources").exists():
        checks.note(
            f"{APP_NAME} is not signed (no _CodeSignature), so none was checked"
        )
        return
    done = subprocess.run(
        ["codesign", "--verify", "--deep", str(app)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    detail = " ".join(done.stdout.split())
    checks.expect(
        done.returncode == 0,
        f"the signature of {APP_NAME} on the volume verifies"
        + ("" if done.returncode == 0 else f": {detail}"),
    )


def verify(dmg):
    checks = Checks()
    work = None
    try:
        if dmg is None:
            check_branding(checks)
            work = Path(tempfile.mkdtemp(prefix="m6e-dmg-layout."))
            dmg = build_scratch_dmg(work)
        taken = [n for n in os.listdir("/Volumes") if n.split(" ")[0] == VOLUME_NAME]
        with attached(dmg) as mount:
            check_volume(checks, mount, resolve_links=not taken)
    finally:
        if work:
            shutil.rmtree(work, ignore_errors=True)
    return checks.failures


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument(
        "command",
        nargs="?",
        default="generate",
        choices=("generate", "check", "verify", "dump", "build"),
    )
    parser.add_argument("paths", nargs="*")
    args = parser.parse_args(argv)
    paths = args.paths
    if args.command == "generate":
        generate()
        checks = Checks()
        check_branding(checks)
        return 1 if checks.failures else 0
    if args.command == "check":
        checks = Checks()
        check_branding(checks)
        return 1 if checks.failures else 0
    if args.command == "dump":
        if len(paths) != 1:
            parser.error("dump needs the path of one .DS_Store file")
        print(describe(read_dsstore(Path(paths[0]).read_bytes())))
        return 0
    if args.command == "build":
        if len(paths) != 2:
            parser.error("build needs the path of M6E.app and the DMG to write")
        build_dmg(*paths)
        return 0
    return 1 if verify(Path(paths[0]) if paths else None) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
