/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-disable sdl/no-insecure-url */

// Shared by the xpcshell test and the plain-Node runner: no Gecko globals.
// Every torrent here is synthetic metadata; none names a real torrent.

const MiB = 1024 * 1024;
const GiB = 1024 * MiB;

const UNKNOWN = { kind: "unknown", origin: null };
const FINDER = { kind: "finder", origin: null };
const page = origin => ({ kind: "page", origin });
const magnet = origin => ({ kind: "magnet-page", origin });

// Paths are written as they appear on disk and are given the shape the
// helper reports: relative to the torrent's folder, which is its name, so
// "Name/a/b" becomes "a/b". A single-file torrent's only path is its name.
function torrent(name, files) {
  const prefix = name + "/";
  const list = files.map(([path, length = MiB, extra = {}], index) => ({
    index,
    path: path.startsWith(prefix) ? path.slice(prefix.length) : path,
    length,
    ...extra,
  }));
  return {
    infoHash: "0".repeat(40),
    name,
    totalSize: list.reduce((sum, file) => sum + file.length, 0),
    pieceLength: 262144,
    files: list,
    trackers: [],
    webSeeds: [],
    isPrivate: false,
  };
}

function many(count, make) {
  return Array.from({ length: count }, (_, i) => make(i));
}

export const ASSESS_CASES = [
  {
    name: "spec example: Debian ISO from a trusted origin",
    metadata: torrent("debian-12.5.0-amd64-netinst.iso", [
      ["debian-12.5.0-amd64-netinst.iso", 650 * MiB],
    ]),
    source: page("https://www.debian.org"),
    verdict: "ok",
    trusted: true,
    codes: [],
  },
  {
    name: "spec example: Debian ISO from Finder",
    metadata: torrent("debian-12.5.0-amd64-netinst.iso", [
      ["debian-12.5.0-amd64-netinst.iso", 650 * MiB],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "spec example: Movie 2025 1080p holding Movie.mkv.exe",
    metadata: torrent("Movie 2025 1080p", [["Movie.mkv.exe", 2 * MiB]]),
    source: page("https://example-torrents.net"),
    verdict: "shady",
    trusted: false,
    codes: ["windows-program", "double-extension", "program-in-media"],
  },
  {
    name: "double extension: Movie.mkv.exe in a movie folder",
    metadata: torrent("Some Movie", [
      ["Some Movie/Some.Movie.mkv", 2 * GiB],
      ["Some Movie/Some.Movie.mkv.exe", 400000],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program", "double-extension", "program-in-media"],
    files: { "double-extension": ["Some Movie/Some.Movie.mkv.exe"] },
  },
  {
    name: "double extension is case-insensitive and ignores trailing dots and spaces",
    metadata: torrent("Pictures", [["holiday.JPG.Scr. ", 1000]]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program", "double-extension"],
  },
  {
    name: "double extension hidden by a long run of spaces",
    metadata: torrent("Documents", [
      ["report.pdf                    .exe", 1000],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program", "double-extension"],
  },
  {
    name: "trailing spaces after a program extension are not a padded name",
    metadata: torrent(
      "Tools",
      many(200, i => {
        const stem = "a" + i + ".exe";
        return [stem + " ".repeat(240 - stem.length), 10];
      })
    ),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
    cap: { "windows-program": { files: 10, moreFiles: 190 } },
  },
  {
    name: "a padded name needs the run of spaces before the last dot",
    each: [
      ["Clip.mp4     x.exe", true],
      ["Clip.mp4 \u3000\u2003\u00A0\u2002.exe", true],
      ["Clip    .exe", false],
      ["Clip.exe     ", false],
    ].map(([file, padded]) => ({
      metadata: torrent("Clips", [[file, 100]]),
      source: UNKNOWN,
      verdict: "shady",
      trusted: false,
      codes: padded
        ? ["windows-program", "double-extension"]
        : ["windows-program"],
    })),
  },
  {
    name: "double extension on a macOS program",
    metadata: torrent("Album", [
      ["Album/track01.mp3", 5 * MiB],
      ["Album/track02.mp3", 5 * MiB],
      ["Album/track03.mp3.command", 1000],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["double-extension", "macos-installer", "program-in-media"],
  },
  {
    name: "ordinary dotted names are not double extensions",
    metadata: torrent("tools", [
      ["tools/setup.v1.2.exe", 1000],
      ["tools/notes.final.txt", 10],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
  },
  {
    name: "each Windows program and script extension is high",
    each: [
      "exe",
      "scr",
      "com",
      "pif",
      "bat",
      "cmd",
      "vbs",
      "vbe",
      "jse",
      "wsf",
      "hta",
      "msi",
      "ps1",
      "cpl",
      "lnk",
    ].map(ext => ({
      metadata: torrent("Stuff", [["Stuff/thing." + ext.toUpperCase(), 100]]),
      source: UNKNOWN,
      verdict: "shady",
      trusted: false,
      codes: ["windows-program"],
    })),
  },
  {
    name: "a lone macOS installer is only medium",
    metadata: torrent("Blender", [
      ["blender-4.1.0-macos-arm64.dmg", 300 * MiB],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: ["macos-installer"],
  },
  {
    name: "each macOS installer extension is one medium reason",
    each: ["dmg", "pkg", "command", "osax", "jar", "sh"].map(ext => ({
      metadata: torrent("Stuff", [["Stuff/thing." + ext, 100]]),
      source: UNKNOWN,
      verdict: "ok",
      trusted: false,
      codes: ["macos-installer"],
    })),
  },
  {
    name: "an .app bundle counts once and hides its inner files",
    metadata: torrent("Game", [
      ["Game.app/Contents/MacOS/game", 5 * MiB],
      ["Game.app/Contents/Resources/run.sh", 100],
      ["Game.app/Contents/Resources/helper.jar", 100],
      ["Game.app/Contents/Info.plist", 100],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: ["macos-installer"],
    files: { "macos-installer": ["Game/Game.app"] },
  },
  {
    name: "two medium reasons are shady: installer and shortcut",
    metadata: torrent("Software", [
      ["Software/Setup.pkg", 10 * MiB],
      ["Software/Visit our site.url", 100],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["macos-installer", "shortcut-file"],
  },
  {
    name: "a lone .webloc shortcut is medium",
    metadata: torrent("Photos", [
      ["Photos/a.jpg", MiB],
      ["Photos/Get more.webloc", 100],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: ["shortcut-file"],
  },
  {
    name: "password.txt next to an archive plus a shortcut is shady",
    metadata: torrent("Course", [
      ["Course/lessons.rar", 500 * MiB],
      ["Course/password.txt", 20],
      ["Course/bonus.url", 100],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["shortcut-file", "password-file"],
    files: { "password-file": ["Course/password.txt"] },
  },
  {
    name: "readme next to a zip is only medium",
    metadata: torrent("Course", [
      ["Course/lessons.zip", 500 * MiB],
      ["Course/README", 20],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: ["password-file"],
  },
  {
    name: "password file in another folder is not next to the archive",
    metadata: torrent("Course", [
      ["Course/a/lessons.zip", 500 * MiB],
      ["Course/b/password.txt", 20],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "readme next to a source tarball is not flagged",
    metadata: torrent("project-1.2", [
      ["project-1.2.tar.gz", 5 * MiB],
      ["README", 2000],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "right-to-left override in a name",
    metadata: torrent("Pictures", [["Pictures/Photo\u202Egpj.exe", 1000]]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["bidi-override", "windows-program"],
    files: { "bidi-override": ["Pictures/Photo<U+202E>gpj.exe"] },
  },
  {
    name: "right-to-left override in a folder name",
    metadata: torrent("Pictures", [["Pic\u202Etxt/readme", 1000]]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["bidi-override"],
  },
  {
    name: "isolates, zero width space, BOM and control characters are flagged",
    each: [
      "a\u2066b.mkv",
      "a\u2069b.mkv",
      "a\u200Bb.mkv",
      "a\uFEFFb.mkv",
      "a\u2060b.mkv",
      "a\u0000b.mkv",
      "a\nb.mkv",
      "a\u200Eb.mkv",
      "a\u3164b.mkv",
      "a\u061Cb.mkv",
    ].map(path => ({
      metadata: torrent("Video", [[path, 100]]),
      source: UNKNOWN,
      verdict: "shady",
      trusted: false,
      codes: ["bidi-override"],
    })),
  },
  {
    name: "joiners inside words and emoji are not flagged",
    metadata: torrent("Music", [
      ["Music/می\u200Cخواهم.mp3", 100],
      ["Music/family \u{1F468}\u200D\u{1F469}\u200D\u{1F467}.jpg", 100],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "joiner at the edge of a name is flagged",
    metadata: torrent("Odd", [["Odd/\u200Dfile.txt", 100]]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["bidi-override"],
  },
  {
    name: "piracy tool names in an archive",
    each: [
      "Photoshop 2025 Keygen.zip",
      "Photoshop.KeyGen.zip",
      "photoshop_key-gen.rar",
      "Office Activator.7z",
      "crack.rar",
      "Photoshop_Cracked.zip",
      "PhotoshopCrack.zip",
    ].map(file => ({
      metadata: torrent("Adobe Tools", [[file, 2 * MiB]]),
      source: UNKNOWN,
      verdict: "shady",
      trusted: false,
      codes: ["piracy-tool-name"],
    })),
  },
  {
    name: "a folder named Crack flags its contents",
    metadata: torrent("Game", [
      ["Game/setup.bin", 100 * MiB],
      ["Game/Crack/game", 4 * MiB],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["piracy-tool-name"],
    files: { "piracy-tool-name": ["Game/Crack/game"] },
  },
  {
    name: "weak piracy words count for programs and archives",
    each: [
      "Game Patch 1.2.zip",
      "serial.zip",
      "Universal Loader.rar",
      "Photoshop.patcher.dmg",
    ].map(file => ({
      metadata: torrent("Stuff", [[file, MiB]]),
      source: UNKNOWN,
      verdict: "shady",
      trusted: false,
      codes: file.endsWith(".dmg")
        ? ["macos-installer", "piracy-tool-name"]
        : ["piracy-tool-name"],
    })),
  },
  {
    name: "piracy words as part of other words are not matched",
    metadata: torrent("Stuff", [
      ["Stuff/patchwork.zip", MiB],
      ["Stuff/serialization.rar", MiB],
      ["Stuff/cracker-barrel.zip", MiB],
      ["Stuff/downloader-ui.7z", MiB],
      ["Stuff/payloader.zip", MiB],
      ["Stuff/Sherlock.zip", MiB],
      ["Stuff/deactivator.zip", MiB],
      ["Stuff/keygenerator.zip", MiB],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "piracy words in media and documents are ignored",
    metadata: torrent("Films", [
      ["Films/Patch Adams (1998).mkv", 2 * GiB],
      ["Films/Serial Experiments Lain - 01.mkv", 300 * MiB],
      ["Films/Crack the Coding Interview.pdf", 5 * MiB],
      ["Films/Loader.png", 1000],
      ["Films/Patch notes.txt", 100],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "weak piracy words in a source tarball are ignored",
    metadata: torrent("patch-2.7.6", [
      ["patch-2.7.6.tar.xz", 800000],
      ["patch-2.7.6.tar.xz.sig", 500],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "program or archive in an otherwise video torrent",
    metadata: torrent("Show S01E01 720p", [
      ["Show S01E01 720p/show.s01e01.mkv", 700 * MiB],
      ["Show S01E01 720p/codec-setup.zip", 2 * MiB],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["program-in-media"],
    files: { "program-in-media": ["Show S01E01 720p/codec-setup.zip"] },
  },
  {
    name: "a macOS program in a music torrent is high",
    metadata: torrent("Great Album", [
      ["Great Album/01.flac", 30 * MiB],
      ["Great Album/02.flac", 30 * MiB],
      ["Great Album/installer.pkg", 2 * MiB],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["macos-installer", "program-in-media"],
  },
  {
    name: "subtitle and extras archives next to a film are fine",
    metadata: torrent("Film 2020 1080p BluRay", [
      ["Film 2020 1080p BluRay/film.mkv", 8 * GiB],
      ["Film 2020 1080p BluRay/Subs.zip", MiB],
      ["Film 2020 1080p BluRay/Extras.rar", 100 * MiB],
      ["Film 2020 1080p BluRay/cover.jpg", MiB],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "a scene release in rar volumes is fine",
    metadata: torrent("Show.S02E03.720p.WEB-DL", [
      ["Show.S02E03.720p.WEB-DL/show.part1.rar", 100 * MiB],
      ["Show.S02E03.720p.WEB-DL/show.part2.rar", 100 * MiB],
      ["Show.S02E03.720p.WEB-DL/show.nfo", 2000],
      ["Show.S02E03.720p.WEB-DL/show.sfv", 200],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "installer.exe in a software torrent is high but not program-in-media",
    metadata: torrent("Some Editor", [["Some Editor/setup.exe", 50 * MiB]]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
  },
  {
    name: "plain MP3 album",
    metadata: torrent("Artist - Album (2019) [FLAC]", [
      ["Artist - Album/01 - Intro.flac", 30 * MiB],
      ["Artist - Album/02 - Song.flac", 35 * MiB],
      ["Artist - Album/cover.jpg", MiB],
      ["Artist - Album/album.cue", 2000],
      ["Artist - Album/album.log", 4000],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "document set",
    metadata: torrent("Papers", [
      ["Papers/a.pdf", 5 * MiB],
      ["Papers/b.epub", 2 * MiB],
      ["Papers/notes.docx", MiB],
      ["Papers/data.csv", 100000],
      ["Papers/README.txt", 100],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "source tarball",
    metadata: torrent("linux-6.8", [["linux-6.8.tar.xz", 140 * MiB]]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "Fedora ISO from the project site",
    metadata: torrent("Fedora-Workstation-Live-x86_64-40", [
      ["Fedora-Workstation-Live-x86_64-40-1.14.iso", 2 * GiB],
      ["Fedora-Spins-40-1.14-x86_64-CHECKSUM", 1000],
    ]),
    source: page("https://torrent.fedoraproject.org"),
    verdict: "ok",
    trusted: true,
    codes: [],
  },
  {
    name: "trusted source keeps the reasons for information",
    metadata: torrent("Release", [
      ["Release/tool-setup.exe", 10 * MiB],
      ["Release/tool.dmg", 10 * MiB],
    ]),
    source: page("https://www.libreoffice.org"),
    verdict: "ok",
    trusted: true,
    codes: ["windows-program", "macos-installer"],
  },
  {
    name: "trusted magnet page",
    metadata: torrent("Release", [["Release/tool-setup.exe", 10 * MiB]]),
    source: magnet("https://archlinux.org"),
    verdict: "ok",
    trusted: true,
    codes: ["windows-program"],
  },
  {
    name: "http origin is never trusted",
    metadata: torrent("Release", [["Release/tool-setup.exe", 10 * MiB]]),
    source: page("http://www.debian.org"),
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
  },
  {
    name: "look-alike origin evil-debian.org",
    metadata: torrent("Release", [["Release/tool-setup.exe", 10 * MiB]]),
    source: page("https://evil-debian.org"),
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
  },
  {
    name: "look-alike origin debian.org.evil.com",
    metadata: torrent("Release", [["Release/tool-setup.exe", 10 * MiB]]),
    source: page("https://debian.org.evil.com"),
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
  },
  {
    name: "the finder kind is never trusted even with an origin",
    metadata: torrent("Release", [["Release/tool-setup.exe", 10 * MiB]]),
    source: { kind: "finder", origin: "https://www.debian.org" },
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
  },
  {
    name: "missing source and missing origin",
    metadata: torrent("Release", [["Release/tool-setup.exe", 10 * MiB]]),
    source: null,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
  },
  {
    name: "extra trusted site makes a shady torrent ok",
    metadata: torrent("Release", [["Release/tool-setup.exe", 10 * MiB]]),
    source: page("https://downloads.example.org"),
    extra: ["https://downloads.example.org"],
    verdict: "ok",
    trusted: true,
    codes: ["windows-program"],
  },
  {
    name: "extra trusted site does not cover its subdomains",
    metadata: torrent("Release", [["Release/tool-setup.exe", 10 * MiB]]),
    source: page("https://downloads.example.org"),
    extra: ["https://example.org"],
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
  },
  {
    name: "extra trusted site does not cover its parent or siblings",
    metadata: torrent("Release", [["Release/tool-setup.exe", 10 * MiB]]),
    source: page("https://www.example.org"),
    extra: ["https://downloads.example.org"],
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
  },
  {
    name: "extra trusted http site is ignored",
    metadata: torrent("Release", [["Release/tool-setup.exe", 10 * MiB]]),
    source: page("https://example.org"),
    extra: ["http://example.org"],
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
  },
  {
    name: "padding files are ignored",
    metadata: {
      ...torrent("Release", [["Release/a.txt", 10]]),
      files: [
        { index: 0, path: "Release/a.txt", length: 10 },
        { index: 1, path: "Release/.pad/4096.exe", length: 4096, pad: true },
        { index: 2, path: ".pad/100", length: 100 },
      ],
    },
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "Unicode names are handled",
    metadata: torrent("Vidéos 日本語 \u{1F3AC}", [
      ["Vidéos/映画 \u{1F3AC}.mkv", 2 * GiB],
      ["Vidéos/фильм.mp4", GiB],
      ["Vidéos/الملف.srt", 1000],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "Unicode name with a fullwidth look-alike is not an extension",
    metadata: torrent("Odd", [["Odd/movie.ｅｘｅ", 1000]]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "Unicode piracy word with fullwidth letters",
    metadata: torrent("Odd", [["Odd/ｋｅｙｇｅｎ.zip", MiB]]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["piracy-tool-name"],
  },
  {
    name: "malformed metadata does not throw",
    metadata: { files: [null, {}, { path: 5 }, { path: "ok.txt" }] },
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "metadata without files",
    metadata: {},
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "a very long file list is capped and counted",
    metadata: torrent(
      "Pack",
      many(5000, i => ["Pack/tool" + i + ".exe", 1000])
    ),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
    cap: { "windows-program": { files: 10, moreFiles: 4990 } },
  },
  {
    name: "a huge benign file list",
    metadata: torrent(
      "Photos 2020",
      many(100000, i => ["Photos 2020/IMG_" + i + ".jpg", 3 * MiB])
    ),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "a very long file name is shortened for display",
    metadata: torrent("Long", [["a".repeat(2000) + ".exe", 100]]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
    maxFileLength: 203,
  },
  {
    name: "a crafted .pad path that is not a number is not padding",
    metadata: torrent("Release", [
      ["Release/a.txt", 10],
      ["Release/.pad/evil.exe", 4096],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
  },
  {
    name: "a hybrid torrent with padding files and one large ISO",
    metadata: {
      ...torrent("Linux-Distro-1.0", [
        ["Linux-Distro-1.0-x86_64.iso", 4 * GiB],
      ]),
      files: [
        { index: 0, path: "Linux-Distro-1.0-x86_64.iso", length: 4 * GiB },
        { index: 1, path: ".pad/12345", length: 12345, pad: true },
        { index: 2, path: "CHECKSUM", length: 400 },
        { index: 3, path: ".pad/3696", length: 3696 },
      ],
    },
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "large single ISOs and images from Finder show nothing",
    each: [
      "Fedora-Workstation-Live-x86_64-45-1.6.iso",
      "debian-13.0.0-arm64-DVD-1.iso",
      "ubuntu-26.04-live-server-amd64.iso",
      "kali-linux-2026.3-installer-everything-amd64.iso",
      "Win11_25H2_English_x64.iso",
      "Rocky-10-dvd1-update-setup-install.iso",
      "2026-10-06-raspios-trixie-arm64-lite.img.xz",
    ].map(file => ({
      metadata: torrent(file, [[file, 8 * GiB]]),
      source: FINDER,
      verdict: "ok",
      trusted: false,
      codes: [],
    })),
  },
  {
    name: "an ISO with checksum and signature files",
    metadata: torrent("distro-12", [
      ["distro-12/distro-12-netinst.iso", 700 * MiB],
      ["distro-12/SHA256SUMS", 1000],
      ["distro-12/SHA256SUMS.sign", 500],
      ["distro-12/distro-12-netinst.iso.zsync", 100000],
      ["distro-12/README.txt", 1000],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "a source release with scripts and a tarball is at most medium",
    metadata: torrent("tool-2.0", [
      ["tool-2.0/tool-2.0.tar.gz", 5 * MiB],
      ["tool-2.0/install.sh", 2000],
      ["tool-2.0/build.jar", 100000],
      ["tool-2.0/README", 1000],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: ["macos-installer"],
  },
  {
    name: "a macOS installer with a demo video is not a video torrent",
    metadata: torrent("Editor 1.0", [
      ["Editor 1.0/Editor-1.0.dmg", 200 * MiB],
      ["Editor 1.0/demo.mp4", 20 * MiB],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: ["macos-installer"],
  },
  {
    name: "a macOS installer filled out with a film is program-in-media",
    metadata: torrent("Holiday", [
      ["Holiday/holiday.mkv", 2 * GiB],
      ["Holiday/player.dmg", MiB],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["macos-installer", "program-in-media"],
  },
  {
    name: "a jar in an audio torrent is program-in-media",
    metadata: torrent("Live Set", [
      ["Live Set/01.flac", 40 * MiB],
      ["Live Set/02.flac", 40 * MiB],
      ["Live Set/viewer.jar", 100000],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["macos-installer", "program-in-media"],
  },
  {
    name: "a script in a torrent named like a series is program-in-media",
    metadata: torrent("Show.S01E01.WEB-DL", [
      ["Show.S01E01.WEB-DL/show.s01e01.mkv", 500 * MiB],
      ["Show.S01E01.WEB-DL/run.sh", 100],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["macos-installer", "program-in-media"],
  },
  {
    name: "a bundle with a media double extension is a disguised program",
    metadata: torrent("Movie 2024", [
      ["Movie 2024/Movie.mkv.app/Contents/MacOS/run", 100000],
      ["Movie 2024/Movie.mkv.app/Contents/Info.plist", 500],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["double-extension", "macos-installer"],
    files: { "double-extension": ["Movie 2024/Movie.mkv.app"] },
  },
  {
    name: "double extension in upper case and with a tab run",
    each: [
      ["Movie.MKV.EXE", ["windows-program", "double-extension"]],
      [
        "report.pdf\t\t\t\t\t.exe",
        ["windows-program", "double-extension", "bidi-override"],
      ],
      ["song.mp3.PKG", ["macos-installer", "double-extension"]],
    ].map(([file, codes]) => ({
      metadata: torrent("Stuff", [[file, 100]]),
      source: UNKNOWN,
      verdict: "shady",
      trusted: false,
      codes,
    })),
  },
  {
    name: "media titles that contain piracy words are not flagged",
    metadata: torrent("Crack House 2019", [
      ["Crack House 2019/Crack.House.2019.mkv", 2 * GiB],
      ["Crack House 2019/Crack Music/Crack Theme.mp3", 5 * MiB],
      ["Crack House 2019/Crack House.srt", 10000],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "a piracy word in a folder name still flags non-media files",
    metadata: torrent("Crack House 2019", [
      ["Crack House 2019/Crack.House.2019.mkv", 2 * GiB],
      ["Crack House 2019/run.bin", 4 * MiB],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["piracy-tool-name"],
    files: { "piracy-tool-name": ["Crack House 2019/run.bin"] },
  },
  {
    name: "installer and password file together are shady",
    metadata: torrent("Software", [
      ["Software/Setup.pkg", 10 * MiB],
      ["Software/data.7z", 10 * MiB],
      ["Software/unrar password.txt", 20],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["macos-installer", "password-file"],
  },
  {
    name: "password file name variants next to an archive",
    each: [
      "Password.TXT",
      "passwords.txt",
      "read me first.txt",
      "README.html",
      "How To Open.pdf",
      "readme",
    ].map(file => ({
      metadata: torrent("Course", [
        ["Course/lessons.7z", 100 * MiB],
        ["Course/" + file, 20],
      ]),
      source: UNKNOWN,
      verdict: "ok",
      trusted: false,
      codes: ["password-file"],
    })),
  },
  {
    name: "invisible line and paragraph separators, braille blank and grapheme joiner are flagged",
    each: ["a\u2028b.mkv", "a\u2029b.mkv", "a\u2800b.mkv", "a\u034Fb.mkv"].map(
      path => ({
        metadata: torrent("Video", [[path, 100]]),
        source: UNKNOWN,
        verdict: "shady",
        trusted: false,
        codes: ["bidi-override"],
      })
    ),
  },
  {
    name: "a right-to-left override only in the torrent name is flagged",
    metadata: torrent("Holiday\u202Egpj", [["a.jpg", 100]]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["bidi-override"],
    files: { "bidi-override": ["Holiday<U+202E>gpj"] },
  },
  {
    name: "a trusted domain used as a label of another site is not trusted",
    metadata: torrent("Movie 2025 1080p", [["Movie.mkv.exe", 2 * MiB]]),
    source: magnet("https://www.debian.org.example-torrents.net"),
    verdict: "shady",
    trusted: false,
    codes: ["windows-program", "double-extension", "program-in-media"],
  },
  {
    name: "high and medium together: shady with both severities",
    metadata: torrent("Mix", [
      ["Mix/a.exe", 10],
      ["Mix/b.sh", 10],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program", "macos-installer"],
    severities: { "windows-program": "high", "macos-installer": "medium" },
  },

  // The torrent's own name (item 2 of the 2026-11-17 review).
  {
    name: "a torrent named like a disguised bundle is a double extension",
    metadata: torrent("Holiday.mp4.app", [
      ["Holiday.mp4.app/Contents/MacOS/Holiday", 400000],
      ["Holiday.mp4.app/Contents/Info.plist", 500],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["double-extension", "macos-installer"],
    files: { "double-extension": ["Holiday.mp4.app"] },
  },
  {
    name: "a piracy word in the torrent's name flags its programs and archives",
    metadata: torrent("Product Keygen", [
      ["Product Keygen/Product.zip", 5 * MiB],
      ["Product Keygen/Product.dmg", 50 * MiB],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["macos-installer", "piracy-tool-name"],
    files: {
      "piracy-tool-name": [
        "Product Keygen/Product.dmg",
        "Product Keygen/Product.zip",
      ],
    },
  },
  {
    name: "a torrent that is a bundle with a shortcut inside is shady",
    metadata: torrent("Viewer.app", [
      ["Viewer.app/Contents/MacOS/Viewer", 900000],
      ["Viewer.app/Contents/Info.plist", 500],
      ["Viewer.app/Viewer Website.webloc", 200],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["macos-installer", "shortcut-file"],
    files: {
      "macos-installer": ["Viewer.app"],
      "shortcut-file": ["Viewer.app/Viewer Website.webloc"],
    },
  },
  {
    name: "a single-file torrent is checked once, by its name",
    metadata: torrent("Clip.mkv.exe", [["Clip.mkv.exe", 2 * MiB]]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program", "double-extension"],
    files: { "double-extension": ["Clip.mkv.exe"] },
  },
  {
    name: "a folder torrent with one file shows the folder",
    metadata: torrent("Pack", [["tool.exe", 2 * MiB]]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["windows-program"],
    files: { "windows-program": ["Pack/tool.exe"] },
  },

  // Password lures in media torrents (item 3).
  {
    name: "a media torrent with an archive and a password note is shady",
    each: [
      ["Password.txt", ["password-file", "program-in-media"]],
      ["Password.jpg", ["password-file", "program-in-media"]],
      [
        "Get Password.url",
        ["password-file", "program-in-media", "shortcut-file"],
      ],
      [
        "Get Password.webloc",
        ["password-file", "program-in-media", "shortcut-file"],
      ],
    ].map(([note, codes]) => ({
      metadata: torrent("Movie 2025 1080p WEB-DL", [
        ["Movie 2025 1080p WEB-DL/Movie.2025.1080p.WEB-DL.rar", 700 * MiB],
        ["Movie 2025 1080p WEB-DL/" + note, 2000],
      ]),
      source: UNKNOWN,
      verdict: "shady",
      trusted: false,
      codes,
      files: {
        "program-in-media": [
          "Movie 2025 1080p WEB-DL/Movie.2025.1080p.WEB-DL.rar",
        ],
      },
    })),
  },
  {
    name: "a torrent filled with video, an archive and a password note",
    metadata: torrent("Holiday", [
      ["Holiday/holiday.mkv", 2 * GiB],
      ["Holiday/extras.rar", 50 * MiB],
      ["Holiday/password.txt", 20],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["password-file", "program-in-media"],
  },
  {
    name: "an archive and a password picture outside media stay medium",
    metadata: torrent("Course", [
      ["Course/lessons.zip", 500 * MiB],
      ["Course/Password.jpg", 20000],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: ["password-file"],
  },
  {
    name: "a readme next to an archive in a media torrent is not a lure",
    metadata: torrent("Film 2020 1080p BluRay", [
      ["Film 2020 1080p BluRay/film.mkv", 8 * GiB],
      ["Film 2020 1080p BluRay/Extras.rar", 100 * MiB],
      ["Film 2020 1080p BluRay/README.txt", 100],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: ["password-file"],
  },

  // Names that libtorrent changed, and hidden characters it keeps (item 4).
  {
    name: "a file the helper reports as renamed counts as hidden characters",
    metadata: torrent("Pictures", [
      ["Pictures/Photogpj.exe", 1000, { renamed: true }],
      ["Pictures/a.jpg", MiB],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["bidi-override", "windows-program"],
    files: { "bidi-override": ["Pictures/Photogpj.exe"] },
  },
  {
    name: "a renamed harmless file is shady by itself",
    metadata: torrent("Docs", [["Docs/report.pdf", 1000, { renamed: true }]]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["bidi-override"],
  },
  {
    name: "a renamed torrent name counts as hidden characters",
    metadata: {
      ...torrent("Holidaygpj", [["a.jpg", MiB]]),
      nameRenamed: true,
    },
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["bidi-override"],
    files: { "bidi-override": ["Holidaygpj"] },
  },
  {
    name: "renamed must be the boolean true",
    metadata: torrent("Docs", [
      ["Docs/a.pdf", 1000, { renamed: false }],
      ["Docs/b.pdf", 1000, { renamed: "true" }],
      ["Docs/c.pdf", 1000, { renamed: 1 }],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "hidden characters that libtorrent keeps are flagged",
    each: [
      "a\uFE0Fb.mkv",
      "a\u206Ab.mkv",
      "a\u206Fb.mkv",
      "a\u{1D173}b.mkv",
      "a\u{1D17A}b.mkv",
      "a\u180Bb.mkv",
      "a\u180Eb.mkv",
      "a\u{E0067}b.mkv",
      "\u{1F3F4}\u{E0067}\u{E0062}.mkv",
      "a\u{E0100}b.mkv",
      "a\u00ADb.mkv",
      "a\u115Fb.mkv",
      "a\uFFA0b.mkv",
      "a\u{1BCA0}b.mkv",
    ].map(path => ({
      metadata: torrent("Video", [[path, 100]]),
      source: UNKNOWN,
      verdict: "shady",
      trusted: false,
      codes: ["bidi-override"],
    })),
  },
  {
    name: "hidden characters used by real scripts and emoji are not flagged",
    metadata: torrent("Mixed", [
      [
        "Mixed/\u{1F3F4}\u{E0067}\u{E0062}\u{E0065}\u{E006E}\u{E0067}\u{E007F} England.png",
        MiB,
      ],
      ["Mixed/❤\uFE0F love.mp3", MiB],
      ["Mixed/1\uFE0F\u20E3 first.mp3", MiB],
      ["Mixed/ᠬᠠᠷ\u180Eᠠ.mp3", MiB],
      ["Mixed/ᠭ\u180Bᠠ.mp3", MiB],
      ["Mixed/葛\u{E0100}飾.jpg", MiB],
      ["Mixed/क\u094D\u200Dष.mp3", MiB],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "hidden characters are removed before looking for piracy words",
    each: ["Key\uFE0Fgen.zip", "Cr\u206Fack.rar", "Acti\u{1D173}vator.7z"].map(
      file => ({
        metadata: torrent("Tools", [["Tools/" + file, MiB]]),
        source: UNKNOWN,
        verdict: "shady",
        trusted: false,
        codes: ["bidi-override", "piracy-tool-name"],
      })
    ),
  },

  // Piracy words in other spellings (item 5).
  {
    name: "piracy words in mixed case, glued, leet, spaced or look-alike letters",
    each: [
      "CrAcK.zip",
      "photoshopkeygen.zip",
      "OfficeKeyMaker.rar",
      "Cr4ck.zip",
      "k3yg3n.rar",
      "c.r.a.c.k.zip",
      "K E Y G E N.7z",
      "\u0441r\u0430\u0441k.zip",
      "\u03BAeygen.zip",
      "ACTIVATOR.rar",
      "Patch_and_Cr@ck.zip",
    ].map(file => ({
      metadata: torrent("Tools", [["Tools/" + file, MiB]]),
      source: UNKNOWN,
      verdict: "shady",
      trusted: false,
      codes: ["piracy-tool-name"],
    })),
  },
  {
    name: "new spellings do not match ordinary words",
    metadata: torrent("Stuff", [
      ["Stuff/Crackers and Cheese.zip", MiB],
      ["Stuff/Monkey Gen.zip", MiB],
      ["Stuff/Key Generation.zip", MiB],
      ["Stuff/Keystone Gen2.zip", MiB],
      ["Stuff/K.E.Y.zip", MiB],
      ["Stuff/S01E01 Recap.zip", MiB],
      ["Stuff/Rack 4 Mount.zip", MiB],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "a codec archive written in mixed case in a video torrent",
    metadata: torrent("Show S01E01 720p", [
      ["Show S01E01 720p/show.s01e01.mkv", 700 * MiB],
      ["Show S01E01 720p/CoDec.zip", 2 * MiB],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["program-in-media"],
    files: { "program-in-media": ["Show S01E01 720p/CoDec.zip"] },
  },

  // Decoy extensions (item 6).
  {
    name: "a program behind any media, picture, document or archive extension",
    each: [
      ["IMG_1234.HEIC.app", "macos-installer"],
      ["Clip.m2ts.dmg", "macos-installer"],
      ["Song.opus.command", "macos-installer"],
      ["Scan.tiff.dmg", "macos-installer"],
      ["Pic.webp.dmg", "macos-installer"],
      ["Disc.vob.sh", "macos-installer"],
      ["Notes.nfo.exe", "windows-program"],
      ["Backup.tar.exe", "windows-program"],
      ["Disk.iso.exe", "windows-program"],
      ["Raw.cr2.pkg", "macos-installer"],
    ].map(([file, kind]) => ({
      metadata: torrent("Stuff", [["Stuff/" + file, 100]]),
      source: UNKNOWN,
      verdict: "shady",
      trusted: false,
      codes: ["double-extension", kind],
    })),
  },

  // Long names in the banner (item 8).
  {
    name: "a long name keeps its extension and programs come first",
    metadata: torrent("Pack", [
      ...many(12, i => ["Pack/x" + i + "\u200Bnote.txt", 10]),
      ["Pack/" + "\u{1F600}".repeat(300) + "\u202E.exe", 10],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["bidi-override", "windows-program"],
    cap: { "bidi-override": { files: 10, moreFiles: 3 } },
    firstFileEndsWith: { "bidi-override": "<U+202E>.exe" },
    maxFileLength: 200,
  },

  // Filled like video (item 9).
  {
    name: "tiny extra files do not hide a program in a video torrent",
    metadata: torrent("Holiday", [
      ["Holiday/holiday.mkv", 2 * GiB],
      ["Holiday/player.dmg", MiB],
      ["Holiday/a.dat", 10],
      ["Holiday/b.dat", 10],
      ["Holiday/c.dat", 10],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["macos-installer", "program-in-media"],
  },
  {
    name: "small media files do not make a software torrent media",
    metadata: torrent("Editor 1.0", [
      ["Editor 1.0/Editor-1.0.dmg", 200 * MiB],
      ...many(20, i => ["Editor 1.0/sound" + i + ".wav", 50000]),
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: ["macos-installer"],
  },

  // macOS bundles and launchers (item 10).
  {
    name: "a folder with a macOS bundle extension is one program",
    each: [
      "pkg",
      "mpkg",
      "framework",
      "bundle",
      "workflow",
      "prefPane",
      "plugin",
      "kext",
      "qlgenerator",
      "saver",
      "appex",
    ].map(ext => ({
      metadata: torrent("Stuff", [
        ["Stuff/Thing." + ext + "/Contents/MacOS/Thing", 100000],
        ["Stuff/Thing." + ext + "/Contents/Info.plist", 500],
        ["Stuff/Thing." + ext + "/Contents/Resources/run.sh", 500],
      ]),
      source: UNKNOWN,
      verdict: "ok",
      trusted: false,
      codes: ["macos-installer"],
      files: { "macos-installer": ["Stuff/Thing." + ext] },
    })),
  },
  {
    name: "macOS launchers and location files",
    each: [
      ["Open.terminal", "macos-installer"],
      ["Run.tool", "macos-installer"],
      ["Flow.workflow", "macos-installer"],
      ["Setup.mpkg", "macos-installer"],
      ["Site.inetloc", "shortcut-file"],
      ["App.fileloc", "shortcut-file"],
    ].map(([file, code]) => ({
      metadata: torrent("Stuff", [["Stuff/" + file, 100]]),
      source: UNKNOWN,
      verdict: "ok",
      trusted: false,
      codes: [code],
    })),
  },
  {
    name: "a Windows program inside an .app is still listed",
    metadata: torrent("Game.app", [
      ["Game.app/Contents/MacOS/game", 5 * MiB],
      ["Game.app/Contents/Resources/setup.exe", MiB],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["macos-installer", "windows-program"],
    files: {
      "macos-installer": ["Game.app"],
      "windows-program": ["Game.app/Contents/Resources/setup.exe"],
    },
  },

  // Password notes (item 11).
  {
    name: "a password note next to a split archive",
    each: [
      "lessons.7z.001",
      "lessons.zip.001",
      "lessons.z01",
      "lessons.r00",
    ].map(archive => ({
      metadata: torrent("Course", [
        ["Course/" + archive, 100 * MiB],
        ["Course/password.txt", 20],
      ]),
      source: UNKNOWN,
      verdict: "ok",
      trusted: false,
      codes: ["password-file"],
    })),
  },
  {
    name: "an installer disk image and a password note are shady",
    metadata: torrent("Software", [
      ["Software/Setup.dmg", 100 * MiB],
      ["Software/password.txt", 20],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["macos-installer", "password-file"],
  },
  {
    name: "a password note one folder above the archive",
    metadata: torrent("Course", [
      ["Course/data/lessons.rar", 100 * MiB],
      ["Course/password.txt", 20],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: ["password-file"],
    files: { "password-file": ["Course/password.txt"] },
  },
  {
    name: "a password note two folders above the archive is not counted",
    metadata: torrent("Course", [
      ["Course/a/b/lessons.rar", 100 * MiB],
      ["Course/password.txt", 20],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "password names in other spellings and languages",
    each: [
      "mypassword.txt",
      "Pa$$word.txt",
      "P@ssw0rd.txt",
      "Passwort.txt",
      "Contraseña.txt",
      "mot de passe.txt",
      "Senha.txt",
      "п\u0430роль.txt",
      "解压密码.txt",
      "unrar-password.nfo",
      "PASSWORD",
    ].map(note => ({
      metadata: torrent("Course", [
        ["Course/lessons.7z", 100 * MiB],
        ["Course/" + note, 20],
      ]),
      source: UNKNOWN,
      verdict: "ok",
      trusted: false,
      codes: ["password-file"],
    })),
  },
  {
    name: "a short password word inside another word is not a password note",
    metadata: torrent("Course", [
      ["Course/lessons.7z", 100 * MiB],
      ["Course/Resenha.txt", 20],
      ["Course/Passage.txt", 20],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },

  // False positives (item 14).
  {
    name: "a photo folder named with a piracy word",
    metadata: torrent("Cracked Pottery", [
      ["Cracked Pottery/IMG_0001.CR2", 25 * MiB],
      ["Cracked Pottery/IMG_0001.xmp", 4000],
      ["Cracked Pottery/IMG_0002.NEF", 25 * MiB],
      ["Cracked Pottery/IMG_0003.dng", 25 * MiB],
      ["Cracked Pottery/edit.psd", 50 * MiB],
      ["Cracked Pottery/IMG_0004.jpg", 5 * MiB],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "an info file and parity files with piracy words",
    metadata: torrent("Release", [
      ["Release/release.mkv", 2 * GiB],
      ["Release/Cracked.nfo", 2000],
      ["Release/release.vol00+01.par2", 5 * MiB],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "source files and scripts in a source release do not trigger",
    metadata: torrent("secure-shell-1.0", [
      ["secure-shell-1.0/ssh-keygen.c", 90000],
      ["secure-shell-1.0/ssh-keygen.h", 2000],
      ["secure-shell-1.0/Makefile", 9000],
      ["secure-shell-1.0/configure", 400000],
      ["secure-shell-1.0/contrib/ssh-keygen", 3000],
      ["secure-shell-1.0/src/Activator.java", 4000],
      ["secure-shell-1.0/lib/serial-io.jar", 80000],
      ["secure-shell-1.0/patch.sh", 800],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: ["macos-installer"],
  },
  {
    name: "outside a source release a file without an extension counts",
    metadata: torrent("tools", [
      ["tools/ssh-keygen", 300000],
      ["tools/notes.txt", 100],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["piracy-tool-name"],
  },
  {
    name: "a source release still flags Windows programs and archives",
    metadata: torrent("project-2.0", [
      ["project-2.0/main.c", 9000],
      ["project-2.0/util.c", 9000],
      ["project-2.0/Makefile", 900],
      ["project-2.0/extras/Keygen.zip", MiB],
    ]),
    source: UNKNOWN,
    verdict: "shady",
    trusted: false,
    codes: ["piracy-tool-name"],
    files: { "piracy-tool-name": ["project-2.0/extras/Keygen.zip"] },
  },
  {
    name: "names that look like addresses are not .com programs",
    metadata: torrent("Release", [
      ["Release/release.mkv", 2 * GiB],
      ["Release/www.example.com", 100],
      ["Release/WWW.EXAMPLE.COM", 100],
      ["Release/example.com", 100],
      ["Release/info@example.com", 100],
      ["Release/Visit www.example.com", 100],
    ]),
    source: UNKNOWN,
    verdict: "ok",
    trusted: false,
    codes: [],
  },
  {
    name: "a .com program that does not look like an address",
    each: ["photo.jpg.com", "SETUP.COM", "Photo Album.mp4.com"].map(file => ({
      metadata: torrent("Stuff", [["Stuff/" + file, 100]]),
      source: UNKNOWN,
      verdict: "shady",
      trusted: false,
      codes:
        file == "SETUP.COM"
          ? ["windows-program"]
          : ["windows-program", "double-extension"],
    })),
  },
];

export const TRUSTED_CASES = [
  ["https://www.debian.org", [], true],
  ["https://debian.org", [], true],
  ["https://cdimage.debian.org", [], true],
  ["https://releases.ubuntu.com", [], true],
  ["https://cdimage.ubuntu.com", [], true],
  ["https://ubuntu.com", [], true],
  ["https://torrent.fedoraproject.org", [], true],
  ["https://fedoraproject.org", [], true],
  ["https://rockylinux.org", [], true],
  ["https://download.rockylinux.org", [], true],
  ["https://almalinux.org", [], true],
  ["https://repo.almalinux.org", [], true],
  ["https://archlinux.org", [], true],
  ["https://www.linuxmint.com", [], true],
  ["https://www.kali.org", [], true],
  ["https://downloads.raspberrypi.com", [], true],
  ["https://www.libreoffice.org", [], true],
  ["https://download.documentfoundation.org", [], true],
  ["https://WWW.DEBIAN.ORG", [], true],
  ["https://www.debian.org:443", [], true],
  ["https://www.debian.org/", [], true],
  ["http://www.debian.org", [], false],
  ["ftp://www.debian.org", [], false],
  ["https://www.debian.org:8443", [], false],
  ["https://evil-debian.org", [], false],
  ["https://notdebian.org", [], false],
  ["https://debian.org.evil.com", [], false],
  ["https://www.debian.org.evil.com", [], false],
  ["https://debian.org@evil.com", [], false],
  ["https://www.debian.org@evil.com", [], false],
  ["https://evil.com\\@debian.org", [], false],
  ["https://evil.com/.debian.org", [], false],
  ["https://evil.com#.debian.org", [], false],
  ["https://www.debian.org.", [], false],
  ["https://user:pw@www.debian.org", [], false],
  ["https://debian.org.invalid", [], false],
  ["https://debian.org\u200B.evil.com", [], false],
  ["https://d\u0435bian.org", [], false],
  ["https://people.debian.org", [], false],
  ["https://salsa.debian.org", [], false],
  ["https://bbs.archlinux.org", [], false],
  ["https://wiki.archlinux.org", [], false],
  ["https://aur.archlinux.org", [], false],
  ["https://forums.linuxmint.com", [], false],
  ["https://documentfoundation.org", [], false],
  ["https://wiki.documentfoundation.org", [], false],
  ["https://cdimage.kali.org", [], true],
  ["https://answers.ubuntu.com", [], false],
  ["https://ask.fedoraproject.org", [], false],
  ["https://forums.rockylinux.org", [], false],
  ["https://git.almalinux.org", [], false],
  ["https://bugs.kali.org", [], false],
  ["https://forums.raspberrypi.com", [], false],
  ["https://templates.libreoffice.org", [], false],
  ["https://extensions.libreoffice.org", [], false],
  ["https://tails.net", [], false],
  ["https://www.freebsd.org", [], false],
  ["https://download.opensuse.org", [], false],
  ["https://dumps.wikimedia.org", [], false],
  ["https://web.archive.org", [], false],
  ["https://www.centos.org", [], false],
  ["https://sourceforge.net", [], false],
  ["https://github.com", [], false],
  ["https://archive.org", [], false],
  ["https://blender.org", [], false],
  ["https://centos.org", [], false],
  ["", [], false],
  [null, [], false],
  [undefined, [], false],
  ["not a url", [], false],
  ["null", [], false],
  ["https://www.ubuntu.com", [], true],
  ["https://www.fedoraproject.org", [], true],
  ["https://www.rockylinux.org", [], true],
  ["https://www.almalinux.org", [], true],
  ["https://www.archlinux.org", [], true],
  ["https://linuxmint.com", [], true],
  ["https://kali.org", [], true],
  ["https://raspberrypi.com", [], true],
  ["https://www.raspberrypi.com", [], true],
  ["https://libreoffice.org", [], true],
  ["https://random.debian.org", [], false],
  ["https://www.cdimage.debian.org", [], false],
  ["https://ftp.us.debian.org", [], false],
  ["https://planet.debian.org", [], false],
  ["https://archive.ubuntu.com", [], false],
  ["https://us.archive.ubuntu.com", [], false],
  ["https://help.ubuntu.com", [], false],
  ["https://forge.fedoraproject.org", [], false],
  ["https://people.fedoraproject.org", [], false],
  ["https://chat.fedoraproject.org", [], false],
  ["https://dl.rockylinux.org", [], false],
  ["https://mirrors.almalinux.org", [], false],
  ["https://md.archlinux.org", [], false],
  ["https://http.kali.org", [], false],
  ["https://help.libreoffice.org", [], false],
  ["https://gerrit.libreoffice.org", [], false],
  ["https://listarchives.libreoffice.org", [], false],
  ["https://www.debian.org/CD/torrent-cd/", [], false],
  ["https://www.debian.org/#top", [], false],
  ["https://www.debian.org#top", [], false],
  ["https://www.debian.org?x=1", [], false],
  [" https://www.debian.org", [], false],
  ["https://www.debian.org ", [], false],
  ["https://www.deb\tian.org", [], false],
  ["https://www.debian.org\t", [], false],
  ["https://www.debian.org\n", [], false],
  ["https:\\\\www.debian.org", [], false],
  ["https://www.debian.org\\", [], false],
  ["https:/www.debian.org", [], false],
  ["https://www.debian.org:", [], false],
  ["https://.debian.org", [], false],
  ["https://www..debian.org", [], false],
  ["https://example.org", ["https://example.org"], true],
  ["https://sub.example.org", ["https://example.org"], false],
  ["https://example.org", ["https://www.example.org"], false],
  ["https://co.uk", ["https://co.uk"], false],
  ["https://s3.amazonaws.com", ["https://s3.amazonaws.com"], false],
  ["https://bucket.s3.amazonaws.com", ["https://s3.amazonaws.com"], false],
  ["https://user.github.io", ["https://user.github.io"], true],
  ["https://other.github.io", ["https://user.github.io"], false],
  ["https://example.org/x", ["https://example.org"], false],
  ["https://example.org", ["example.org"], true],
  ["https://example.org", ["https://EXAMPLE.org/some/path"], true],
  ["https://example.org", ["https://sub.example.org"], false],
  ["https://evil-example.org", ["https://example.org"], false],
  ["https://example.org.evil.com", ["https://example.org"], false],
  ["http://example.org", ["https://example.org"], false],
  ["https://example.org", ["http://example.org"], false],
  ["https://example.org:8443", ["https://example.org"], false],
  ["https://example.org:8443", ["https://example.org:8443"], true],
  ["https://example.org", ["https://example.org:8443"], false],
  ["https://example.org", [""], false],
  ["https://example.org", [null, 5, {}], false],
  ["https://example.org", "https://example.org", false],
  ["https://example.org", ["com"], false],
  ["https://example.com", ["https://com"], false],
  ["https://example.org", ["ftp://example.org"], false],
];

const NEVER_TRUSTED_DOMAINS = [
  "archive.org",
  "github.com",
  "githubusercontent.com",
  "gitlab.com",
  "sourceforge.net",
  "drive.google.com",
  "dropbox.com",
  "mega.nz",
  "mediafire.com",
  "wikimedia.org",
  "blender.org",
  "tails.net",
  "freebsd.org",
  "opensuse.org",
  "centos.org",
];

export function checkTrustedList(sites) {
  const failures = [];
  const seen = new Set();
  if (!sites.length) {
    failures.push("the trusted list is empty");
  }
  for (const { name, hosts = [], domain = null } of sites) {
    const all = domain ? [...hosts, domain] : hosts;
    if (!name || !all.length) {
      failures.push("bad trusted entry " + name);
    }
    for (const host of all) {
      if (!/^[a-z0-9.-]+\.[a-z]+$/.test(host)) {
        failures.push("bad trusted host " + name + " / " + host);
      }
      if (seen.has(host)) {
        failures.push("duplicate trusted host " + host);
      }
      seen.add(host);
      for (const never of NEVER_TRUSTED_DOMAINS) {
        if (host == never || host.endsWith("." + never)) {
          failures.push("host must not be trusted: " + host);
        }
      }
    }
  }
  return failures;
}

export function checkAssess(assess, testCase) {
  if (testCase.each) {
    return testCase.each.flatMap((sub, i) =>
      checkAssess(assess, { ...sub, name: testCase.name + " #" + i })
    );
  }
  const failures = [];
  const fail = message => failures.push(testCase.name + ": " + message);
  let result;
  try {
    result = assess(testCase.metadata, testCase.source, testCase.extra);
  } catch (e) {
    return [testCase.name + ": threw " + e];
  }
  if (result.verdict != testCase.verdict) {
    fail("verdict " + result.verdict + ", expected " + testCase.verdict);
  }
  if (result.trusted !== testCase.trusted) {
    fail("trusted " + result.trusted + ", expected " + testCase.trusted);
  }
  const codes = result.reasons.map(reason => reason.code).sort();
  const expected = [...testCase.codes].sort();
  if (JSON.stringify(codes) != JSON.stringify(expected)) {
    fail("codes " + codes.join(",") + ", expected " + expected.join(","));
  }
  for (const reason of result.reasons) {
    if (!["high", "medium"].includes(reason.severity)) {
      fail("bad severity on " + reason.code);
    }
    if (
      !Array.isArray(reason.files) ||
      !reason.files.length ||
      reason.files.some(file => typeof file != "string")
    ) {
      fail("bad files on " + reason.code);
    }
    if (!Number.isInteger(reason.moreFiles) || reason.moreFiles < 0) {
      fail("bad moreFiles on " + reason.code);
    }
    if (reason.files.length > 10) {
      fail("too many files on " + reason.code);
    }
  }
  for (const [code, files] of Object.entries(testCase.files || {})) {
    const reason = result.reasons.find(r => r.code == code);
    if (!reason || JSON.stringify(reason.files) != JSON.stringify(files)) {
      fail("files of " + code + " were " + JSON.stringify(reason?.files));
    }
  }
  for (const [code, cap] of Object.entries(testCase.cap || {})) {
    const reason = result.reasons.find(r => r.code == code);
    if (
      !reason ||
      reason.files.length != cap.files ||
      reason.moreFiles != cap.moreFiles
    ) {
      fail(
        "cap of " +
          code +
          " was " +
          reason?.files.length +
          "/" +
          reason?.moreFiles
      );
    }
  }
  for (const [code, severity] of Object.entries(testCase.severities || {})) {
    const reason = result.reasons.find(r => r.code == code);
    if (reason?.severity != severity) {
      fail("severity of " + code + " was " + reason?.severity);
    }
  }
  if (testCase.maxFileLength) {
    for (const reason of result.reasons) {
      if (reason.files.some(file => file.length > testCase.maxFileLength)) {
        fail("file name too long on " + reason.code);
      }
    }
  }
  for (const reason of result.reasons) {
    if (reason.files.some(file => LONE_SURROGATE.test(file))) {
      fail("a file name of " + reason.code + " splits a character");
    }
  }
  for (const [code, end] of Object.entries(testCase.firstFileEndsWith || {})) {
    const first = result.reasons.find(r => r.code == code)?.files[0];
    if (!first?.endsWith(end)) {
      fail("first file of " + code + " was " + JSON.stringify(first));
    }
  }
  return failures;
}

const LONE_SURROGATE =
  /[\uD800-\uDBFF](?![\uDC00-\uDFFF])|(?<![\uD800-\uDBFF])[\uDC00-\uDFFF]/;

// [text, max, expected] for shortenForDisplay.
export const DISPLAY_CASES = [
  ["short.exe", 20, "short.exe"],
  ["Folder/Sub/name.exe", 19, "Folder/Sub/name.exe"],
  ["Folder/Sub/name.exe", 15, "Folde\u2026/name.exe"],
  [
    "a".repeat(40) + ".exe",
    20,
    "a".repeat(10) + "\u2026" + "a".repeat(5) + ".exe",
  ],
  [
    "\u{1F600}".repeat(20) + ".exe",
    21,
    "\u{1F600}".repeat(5) + "\u2026" + "\u{1F600}".repeat(3) + ".exe",
  ],
  [
    "x/" + "b".repeat(30) + "<U+202E>.exe",
    40,
    "b".repeat(23) + "\u2026bbbb<U+202E>.exe",
  ],
  [
    "Pack/" + "<U+202E>".repeat(10) + ".exe",
    30,
    "<U+202E><U+202E>\u2026<U+202E>.exe",
  ],
];

export function checkDisplay(shorten, [text, max, expected]) {
  const actual = shorten(text, max);
  const failures = [];
  if (actual !== expected) {
    failures.push(
      "shortenForDisplay(" +
        JSON.stringify(text) +
        ", " +
        max +
        ") was " +
        JSON.stringify(actual) +
        ", expected " +
        JSON.stringify(expected)
    );
  }
  if (actual.length > max || LONE_SURROGATE.test(actual)) {
    failures.push(
      "shortenForDisplay broke the limit on " + JSON.stringify(text)
    );
  }
  return failures;
}

// [origin, expected] for canTrustSite, with the public suffixes of
// TEST_PUBLIC_SUFFIXES.
export const TRUST_SITE_CASES = [
  ["https://example.org", true],
  ["https://downloads.example.org", true],
  ["https://user.github.io", true],
  ["https://example.org:8443", true],
  ["https://co.uk", false],
  ["https://s3.amazonaws.com", false],
  ["https://github.io", false],
  ["https://localhost", false],
  ["http://example.org", false],
  ["https://example.org/path", false],
  [" https://example.org", false],
  ["", false],
  [null, false],
];

export const TEST_PUBLIC_SUFFIXES = new Set([
  "com",
  "org",
  "net",
  "uk",
  "co.uk",
  "io",
  "github.io",
  "s3.amazonaws.com",
  "localhost",
]);

// [host, expected] for isExcludedHost.
export const EXCLUDED_CASES = [
  ["people.debian.org", true],
  ["PEOPLE.debian.org", true],
  ["x.people.debian.org", true],
  ["ftp.us.debian.org", true],
  ["ftp.debian.org", false],
  ["us.archive.ubuntu.com", true],
  ["a.b.archive.ubuntu.com", false],
  ["listarchives.libreoffice.org", true],
  ["www.debian.org", false],
  ["cdimage.debian.org", false],
  ["mirrors.debian.org", false],
  ["example.org", false],
  ["", false],
  [null, false],
];

export function checkExcluded(isExcludedHost, [host, expected]) {
  const actual = isExcludedHost(host);
  return actual === expected
    ? []
    : ["isExcludedHost(" + JSON.stringify(host) + ") was " + actual];
}

export function checkTrustSite(canTrustSite, [origin, expected]) {
  const actual = canTrustSite(origin);
  return actual === expected
    ? []
    : ["canTrustSite(" + JSON.stringify(origin) + ") was " + actual];
}

// Checks of the rules data itself: no duplicates, known severities, the
// renamed-file reason, and hosts that are not also excluded.
export function checkRulesData(raw, isExcludedHost) {
  const failures = [];
  const lists = [
    ...Object.entries(raw.extensions),
    ["harmlessNames", raw.harmlessNames],
    ["sourceRelease.extensions", raw.sourceRelease.extensions],
    ["sourceRelease.names", raw.sourceRelease.names],
    ["piracy.strong", raw.piracy.strong],
    ["piracy.weak", raw.piracy.weak],
    ["mediaName.tokens", raw.mediaName.tokens],
    ["suspiciousArchiveNames", raw.suspiciousArchiveNames.tokens],
    ["passwordFiles.words", raw.passwordFiles.words],
    ["passwordFiles.wholeWords", raw.passwordFiles.wholeWords],
    ["passwordFiles.names", raw.passwordFiles.names],
    ["passwordFiles.extensions", raw.passwordFiles.extensions],
  ];
  for (const [name, list] of lists) {
    if (new Set(list).size != list.length) {
      failures.push("duplicate entries in " + name);
    }
  }
  for (const [code, severity] of Object.entries(raw.severities)) {
    if (!["high", "medium"].includes(severity)) {
      failures.push("bad severity for " + code);
    }
  }
  if (!(raw.renamedFileReason in raw.severities)) {
    failures.push("renamedFileReason is not a reason");
  }
  const hosts = new Set();
  for (const site of raw.trustedSites) {
    for (const host of site.hosts ?? []) {
      if (hosts.has(host)) {
        failures.push("host listed twice: " + host);
      }
      hosts.add(host);
      if (isExcludedHost(host)) {
        failures.push("host is both trusted and excluded: " + host);
      }
    }
    if (new Set(site.exclude).size != site.exclude.length) {
      failures.push("duplicate exclusions for " + site.name);
    }
  }
  return failures;
}

export function checkTrusted(isTrustedOrigin, [origin, extra, expected]) {
  let actual;
  try {
    actual = isTrustedOrigin(origin, extra);
  } catch (e) {
    return ["isTrustedOrigin(" + origin + ") threw " + e];
  }
  return actual === expected
    ? []
    : [
        "isTrustedOrigin(" +
          JSON.stringify(origin) +
          ", " +
          JSON.stringify(extra) +
          ") was " +
          actual +
          ", expected " +
          expected,
      ];
}
