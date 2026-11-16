/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// M6E: the Shady Torrent check (P8.3, spec 8.3, D35). A pure function of the
// torrent metadata, where it came from and the bundled rules. Every regular
// expression here runs in linear time, because the metadata is untrusted.

const RULES_URL =
  "moz-src:///browser/components/torrent/m6e-torrent-rules.json";

const ORIGIN_RE = /^https:\/\/[a-z0-9.-]+(?::\d{1,5})?\/?$/i;
const IGNORABLE_RE = /\p{Default_Ignorable_Code_Point}/u;
const IGNORABLE_GLOBAL_RE = /\p{Default_Ignorable_Code_Point}/gu;
const DISPLAY_UNIT_RE = /<U\+[0-9A-F]{4,6}>|[\s\S]/gu;

let gRules = null;
let gIsPublicSuffix = null;

function readRulesFile() {
  const uri = Services.io.newURI(RULES_URL);
  try {
    return JSON.parse(Cu.readUTF8URI(uri));
  } catch (e) {
    const { NetUtil } = ChromeUtils.importESModule(
      "resource://gre/modules/NetUtil.sys.mjs"
    );
    const stream = NetUtil.newChannel({
      uri,
      loadUsingSystemPrincipal: true,
    }).open();
    return JSON.parse(
      NetUtil.readInputStreamToString(stream, stream.available())
    );
  }
}

function hex(point) {
  return "\\u{" + point.toString(16) + "}";
}

function rangesBody(ranges) {
  return ranges
    .map(([from, to]) => (from == to ? hex(from) : hex(from) + "-" + hex(to)))
    .join("");
}

function escapeRe(text) {
  return text.replace(/[.*+?^${}()|[\]\\/-]/g, "\\$&");
}

function hostMatches(host, domain) {
  return host == domain || host.endsWith("." + domain);
}

function hostPattern(pattern) {
  const lower = pattern.toLowerCase();
  if (!lower.includes("*")) {
    return host => hostMatches(host, lower);
  }
  const re = new RegExp(
    "^" +
      lower
        .split(".")
        .map(label => (label == "*" ? "[a-z0-9-]+" : escapeRe(label)))
        .join("\\.") +
      "$"
  );
  return host => re.test(host);
}

function compileSite(site) {
  return {
    name: site.name,
    hosts: new Set((site.hosts ?? []).map(host => host.toLowerCase())),
    domain: site.domain ? site.domain.toLowerCase() : null,
    exclude: (site.exclude ?? []).map(hostPattern),
  };
}

// Lowercase letters and digits with marks, hidden characters and look-alike
// digits and symbols removed, so "Pa$$wörd" and "password" compare equal.
function plainLetters(text, leet) {
  return text
    .replace(IGNORABLE_GLOBAL_RE, "")
    .normalize("NFKD")
    .replace(/\p{M}/gu, "")
    .replace(/[^\p{L}\p{N}]/gu, ch => leet.get(ch) ?? " ")
    .toLowerCase()
    .split(/\s+/u)
    .filter(Boolean)
    .map(word => word.replace(/\p{N}/gu, ch => leet.get(ch) ?? ch));
}

function compileRules(raw) {
  const ext = raw.extensions;
  const piracy = raw.piracy;
  const password = raw.passwordFiles;
  const hidden = raw.hiddenCharacters;
  const joinerClass = "[" + rangesBody(hidden.joiners.map(p => [p, p])) + "]";
  const hiddenClass =
    "(?!" +
    joinerClass +
    ")[\\p{Default_Ignorable_Code_Point}" +
    rangesBody(hidden.extra) +
    "]";
  const leet = new Map(Object.entries(piracy.leet));
  return {
    maxFiles: raw.maxFilesPerReason,
    maxDisplay: raw.maxDisplayLength,
    mediumNeeded: raw.mediumNeeded,
    severities: raw.severities,
    renamedReason: raw.renamedFileReason,
    trustedSites: raw.trustedSites.map(compileSite),
    windows: new Set(ext.windowsProgram),
    macos: new Set(ext.macosInstaller),
    bundle: new Set(ext.macosBundle),
    shortcut: new Set(ext.shortcut),
    archive: new Set(ext.archive),
    diskImage: new Set(ext.diskImage),
    passwordArchive: new Set(ext.passwordArchive),
    weakNameArchive: new Set(ext.weakNameArchive),
    video: new Set(ext.video),
    audio: new Set(ext.audio),
    harmless: new Set(ext.harmless),
    decoy: new Set([
      ...ext.video,
      ...ext.audio,
      ...ext.harmless,
      ...ext.archive,
      ...ext.diskImage,
    ]),
    splitArchive: raw.splitArchivePatterns.map(p => new RegExp(p, "u")),
    padRe: new RegExp(raw.padPathPattern, "u"),
    paddedRe: new RegExp(
      "[\\p{Zs}\\t]{" + raw.paddedExtensionMinSpaces + "}",
      "u"
    ),
    addressExts: new Set(raw.addressLike.extensions),
    harmlessNames: new Set(raw.harmlessNames),
    sourceMin: raw.sourceRelease.minFiles,
    sourceScripts: new Set(raw.sourceRelease.scripts),
    sourceExts: new Set(raw.sourceRelease.extensions),
    sourceNames: new Set(raw.sourceRelease.names),
    strong: new Set(piracy.strong),
    strongPairs: piracy.strongPairs,
    glued: piracy.glued.map(p => new RegExp(p, "u")),
    weak: new Set(piracy.weak),
    leet,
    leetRe: new RegExp(
      "[" + Object.keys(piracy.leet).map(escapeRe).join("") + "]",
      "gu"
    ),
    confusables: new Map(Object.entries(piracy.confusables)),
    mediaTokens: new Set(raw.mediaName.tokens),
    mediaPairs: raw.mediaName.pairs,
    mediaPatterns: raw.mediaName.patterns.map(p => new RegExp(p, "u")),
    mediaMin: raw.mediaFileMinBytes,
    suspiciousArchive: new Set(raw.suspiciousArchiveNames.tokens),
    passwordWords: password.words.map(word =>
      plainLetters(word, leet).join("")
    ),
    passwordWholeWords: new Set(
      password.wholeWords.map(word => plainLetters(word, leet).join(""))
    ),
    passwordNames: password.names,
    passwordExts: new Set(password.extensions),
    hiddenRe: new RegExp(hiddenClass, "u"),
    allowedRes: hidden.allowed.map(p => new RegExp(p, "gu")),
    joinerEdgeRe: new RegExp(
      "(^|[\\s./])" +
        joinerClass +
        "|" +
        joinerClass +
        "($|[\\s./])|" +
        joinerClass +
        "{2,}",
      "u"
    ),
  };
}

function getRules() {
  if (!gRules) {
    gRules = compileRules(readRulesFile());
  }
  return gRules;
}

function geckoIsPublicSuffix(host) {
  const eTLD = globalThis.Services?.eTLD;
  if (!eTLD) {
    return !host.includes(".");
  }
  try {
    return eTLD.getPublicSuffixFromHost(host) == host;
  } catch (e) {
    return !/^[\d.]+$/.test(host);
  }
}

function isPublicSuffix(host) {
  return (gIsPublicSuffix ?? geckoIsPublicSuffix)(host);
}

function splitWords(text, camel) {
  const spaced = camel ? text.replace(/(\p{Ll})(\p{Lu})/gu, "$1 $2") : text;
  return spaced
    .toLowerCase()
    .split(/[^\p{L}\p{N}]+/u)
    .filter(Boolean);
}

function joinSingleLetters(tokens) {
  const joined = [];
  let run = "";
  for (const token of [...tokens, ""]) {
    if (token.length == 1 && /\p{L}/u.test(token)) {
      run += token;
      continue;
    }
    if (run.length > 1) {
      joined.push(run);
    }
    run = "";
  }
  return joined;
}

function skeleton(rules, token) {
  if (/^[\0-\x7f]*$/.test(token)) {
    return token;
  }
  let out = "";
  for (const ch of token) {
    out += rules.confusables.get(ch) ?? ch;
  }
  return out;
}

function cleanText(text) {
  const visible = IGNORABLE_RE.test(text)
    ? text.replace(IGNORABLE_GLOBAL_RE, "")
    : text;
  return visible.normalize("NFKD").replace(/\p{M}/gu, "");
}

// The ways a name can be read as words, each a list of lowercase words in
// name order: split at case changes and not, with look-alike digits and
// symbols read as letters, spaced-out letters joined, and Cyrillic and Greek
// look-alikes read as Latin.
function wordForms(rules, text) {
  const clean = cleanText(text);
  const variants = [clean];
  const leet = clean.replace(rules.leetRe, ch => rules.leet.get(ch));
  if (leet != clean) {
    variants.push(leet);
  }
  const forms = [];
  for (const variant of variants) {
    const plain = splitWords(variant, false);
    forms.push(splitWords(variant, true), plain, joinSingleLetters(plain));
  }
  return forms.map(tokens => tokens.map(token => skeleton(rules, token)));
}

function hasPair(tokens, pairs) {
  for (const [first, second] of pairs) {
    for (let i = 0; i + 1 < tokens.length; i++) {
      if (tokens[i] == first && tokens[i + 1] == second) {
        return true;
      }
    }
  }
  return false;
}

function hasStrongWord(rules, forms) {
  return forms.some(
    tokens =>
      hasPair(tokens, rules.strongPairs) ||
      tokens.some(
        token =>
          rules.strong.has(token) || rules.glued.some(re => re.test(token))
      )
  );
}

function hasWord(set, forms) {
  return forms.some(tokens => tokens.some(token => set.has(token)));
}

function parseHttpsOrigin(value) {
  if (
    typeof value != "string" ||
    value.length > 300 ||
    !ORIGIN_RE.test(value)
  ) {
    return null;
  }
  let url;
  try {
    url = new URL(value);
  } catch (e) {
    return null;
  }
  const host = url.hostname;
  if (
    url.protocol != "https:" ||
    !host ||
    host.startsWith(".") ||
    host.endsWith(".") ||
    host.includes("..")
  ) {
    return null;
  }
  return { host, port: url.port };
}

function parseExtraSite(value) {
  if (typeof value != "string" || !value.trim()) {
    return null;
  }
  const text = value.trim();
  if (/[\s\\]/u.test(text)) {
    return null;
  }
  if (text.includes("://") && !/^https:\/\//i.test(text)) {
    return null;
  }
  let url;
  try {
    url = new URL(text.includes("://") ? text : "https://" + text);
  } catch (e) {
    return null;
  }
  const host = url.hostname;
  if (
    url.protocol != "https:" ||
    url.username ||
    url.password ||
    !host.includes(".") ||
    host.endsWith(".") ||
    isPublicSuffix(host)
  ) {
    return null;
  }
  return { host, port: url.port };
}

function isExcludedHostWith(rules, host) {
  return rules.trustedSites.some(site => site.exclude.some(m => m(host)));
}

function isTrustedHost(rules, host) {
  return rules.trustedSites.some(
    site =>
      (site.hosts.has(host) ||
        (!!site.domain && hostMatches(host, site.domain))) &&
      !site.exclude.some(m => m(host))
  );
}

function isTrustedOriginWith(rules, origin, extraTrustedSites) {
  const parsed = parseHttpsOrigin(origin);
  if (!parsed) {
    return false;
  }
  if (!parsed.port && isTrustedHost(rules, parsed.host)) {
    return true;
  }
  if (!Array.isArray(extraTrustedSites)) {
    return false;
  }
  return extraTrustedSites.some(entry => {
    const extra = parseExtraSite(entry);
    return extra && extra.host == parsed.host && extra.port == parsed.port;
  });
}

function splitName(name) {
  const trimmed = name.replace(/[ .]+$/, "");
  const lastDot = trimmed.lastIndexOf(".");
  if (lastDot <= 0) {
    return { stem: trimmed, ext: "", prevExt: "" };
  }
  const ext = trimmed.slice(lastDot + 1).toLowerCase();
  const stem = trimmed.slice(0, lastDot);
  const prevDot = stem.lastIndexOf(".");
  const prevExt =
    prevDot > 0
      ? stem
          .slice(prevDot + 1)
          .trim()
          .toLowerCase()
      : "";
  return { stem, ext, prevExt };
}

function isHostLabels(rules, labels, anyCase) {
  const label = anyCase ? /^[\p{L}\p{N}-]+$/u : /^[a-z0-9-]+$/;
  return (
    labels.length >= 2 &&
    labels.every(part => label.test(part)) &&
    /^\p{L}{2,}$/u.test(labels[labels.length - 1]) &&
    !rules.decoy.has(labels[labels.length - 2].toLowerCase())
  );
}

// A name like "www.example.com" or "info@example.com" (its last word) is an
// address, not a program with that extension.
function looksLikeAddress(rules, base) {
  const word = base.trim().split(/\s+/u).pop();
  const at = word.indexOf("@");
  if (at >= 0) {
    return (
      at > 0 &&
      /^[\p{L}\p{N}._%+-]+$/u.test(word.slice(0, at)) &&
      isHostLabels(rules, word.slice(at + 1).split("."), true)
    );
  }
  const labels = word.split(".");
  return isHostLabels(rules, labels, /^www\d?$/i.test(labels[0]));
}

function isPadFile(rules, file) {
  return file.pad === true || rules.padRe.test(file.path);
}

function isSplitArchive(rules, ext) {
  return rules.splitArchive.some(re => re.test(ext));
}

function bundleOf(rules, parts) {
  for (let i = 0; i < parts.length - 1; i++) {
    if (rules.bundle.has(splitName(parts[i]).ext)) {
      return parts.slice(0, i + 1).join("/");
    }
  }
  return null;
}

function looksLikeProgram(rules, path) {
  const parts = path.split("/");
  const { ext } = splitName(parts[parts.length - 1]);
  return (
    rules.windows.has(ext) || rules.macos.has(ext) || !!bundleOf(rules, parts)
  );
}

function hasHidden(rules, text) {
  if (rules.joinerEdgeRe.test(text)) {
    return true;
  }
  if (!rules.hiddenRe.test(text)) {
    return false;
  }
  let rest = text;
  for (const re of rules.allowedRes) {
    rest = rest.replace(re, "$1");
  }
  return rules.hiddenRe.test(rest);
}

function displayUnits(rules, text) {
  const units = [];
  for (const ch of text) {
    units.push(
      rules.hiddenRe.test(ch)
        ? "<U+" +
            ch.codePointAt(0).toString(16).toUpperCase().padStart(4, "0") +
            ">"
        : ch
    );
  }
  return units;
}

function unitsLength(units) {
  return units.reduce((sum, unit) => sum + unit.length, 0);
}

function takeStart(units, max) {
  let out = "";
  for (const unit of units) {
    if (out.length + unit.length > max) {
      break;
    }
    out += unit;
  }
  return out;
}

function takeEnd(units, max) {
  let out = "";
  for (let i = units.length - 1; i >= 0; i--) {
    if (out.length + units[i].length > max) {
      break;
    }
    out = units[i] + out;
  }
  return out;
}

// Shortens a path to at most max UTF-16 units by cutting in the middle. The
// last component is kept whole when it fits, else its start and its end with
// the extension. Units (code points, or escapes like "<U+202E>") are never
// split.
function shortenUnits(units, max) {
  if (unitsLength(units) <= max) {
    return units.join("");
  }
  const budget = max - 1;
  const slash = units.lastIndexOf("/");
  const base = units.slice(slash + 1);
  const baseLength = unitsLength(base);
  if (slash >= 0 && baseLength + 2 <= budget) {
    return (
      takeStart(units.slice(0, slash), budget - baseLength - 1) +
      "…/" +
      base.join("")
    );
  }
  const dot = base.lastIndexOf(".");
  const extLength = dot >= 0 ? unitsLength(base.slice(dot)) : 0;
  const tail = Math.min(Math.floor(budget / 2), Math.max(extLength + 12, 16));
  const end = takeEnd(base, tail);
  return takeStart(base, budget - end.length) + "…" + end;
}

function describe(rules, path) {
  return shortenUnits(displayUnits(rules, path), rules.maxDisplay);
}

function nameLooksLikeMedia(rules, name) {
  const clean = cleanText(name);
  for (const tokens of [splitWords(clean, true), splitWords(clean, false)]) {
    if (
      tokens.some(
        token =>
          rules.mediaTokens.has(token) ||
          rules.mediaPatterns.some(re => re.test(token))
      ) ||
      hasPair(tokens, rules.mediaPairs)
    ) {
      return true;
    }
  }
  return false;
}

function passwordWords(rules, stem) {
  const words = plainLetters(stem, rules.leet);
  const joined = words.join("");
  return {
    words,
    lure:
      rules.passwordWords.some(word => joined.includes(word)) ||
      words.some(word => rules.passwordWholeWords.has(word)),
  };
}

function isPasswordName(rules, stem) {
  const { words, lure } = passwordWords(rules, stem);
  if (lure) {
    return true;
  }
  const spaced = words.join(" ");
  return (
    !!spaced &&
    rules.passwordNames.some(
      name => spaced == name || spaced.startsWith(name + " ")
    )
  );
}

/**
 * The files found for one reason, programs first, each listed once.
 */
class ReasonList {
  constructor() {
    this.programs = [];
    this.others = [];
    this.seen = new Set();
  }

  add(path, program = false) {
    if (!this.seen.has(path)) {
      this.seen.add(path);
      (program ? this.programs : this.others).push(path);
    }
  }

  get size() {
    return this.seen.size;
  }

  toReason(rules, code) {
    const files = [...this.programs, ...this.others];
    return {
      code,
      severity: rules.severities[code],
      files: files.slice(0, rules.maxFiles).map(path => describe(rules, path)),
      moreFiles: Math.max(0, files.length - rules.maxFiles),
    };
  }
}

function newLists(rules) {
  return Object.fromEntries(
    Object.keys(rules.severities).map(code => [code, new ReasonList()])
  );
}

function dirOf(path) {
  const slash = path.lastIndexOf("/");
  return slash >= 0 ? path.slice(0, slash) : "";
}

function makeEntry(path, base, bundle) {
  return { path, base, dirPath: dirOf(path), ...splitName(base), bundle };
}

// Turns the helper's file list into entries with full paths. The helper's
// paths are relative to the torrent's folder, which is the torrent's name,
// except in a single-file torrent where the only path is the name itself.
function collect(rules, metadata, lists) {
  const name = typeof metadata?.name == "string" ? metadata.name : "";
  const files = (Array.isArray(metadata?.files) ? metadata.files : []).filter(
    file => file && typeof file.path == "string" && !isPadFile(rules, file)
  );
  const single =
    files.length == 1 && metadata.files.length == 1 && files[0].path == name;
  const state = {
    name,
    entries: [],
    bundles: new Map(),
    mediaBytes: 0,
    contentBytes: 0,
    bigMedia: 0,
    bigContent: 0,
    sources: 0,
  };
  if (name && (hasHidden(rules, name) || metadata.nameRenamed === true)) {
    lists["bidi-override"].add(name);
  }
  for (const file of files) {
    const path = single || !name ? file.path : name + "/" + file.path;
    const length =
      Number.isFinite(file.length) ? Math.max(0, file.length) : 0;
    const hidden = hasHidden(rules, file.path);
    if (hidden || file.renamed === true) {
      const program = looksLikeProgram(rules, path);
      if (hidden) {
        lists["bidi-override"].add(path, program);
      }
      if (file.renamed === true) {
        lists[rules.renamedReason].add(path, program);
      }
    }
    collectFile(rules, state, path, length);
  }
  for (const [path, length] of state.bundles) {
    state.entries.push(
      makeEntry(path, path.slice(path.lastIndexOf("/") + 1), true)
    );
    state.contentBytes += length;
    if (length > rules.mediaMin) {
      state.bigContent++;
    }
  }
  return state;
}

function collectFile(rules, state, path, length) {
  const parts = path.split("/");
  const base = parts[parts.length - 1];
  const entry = makeEntry(path, base, false);
  const bundle = bundleOf(rules, parts);
  if (bundle) {
    state.bundles.set(bundle, (state.bundles.get(bundle) ?? 0) + length);
    if (rules.windows.has(entry.ext) || rules.shortcut.has(entry.ext)) {
      state.entries.push({ ...entry, inBundle: true });
    }
    return;
  }
  state.entries.push(entry);
  const { ext } = entry;
  const big = length > rules.mediaMin;
  if (rules.video.has(ext) || rules.audio.has(ext)) {
    state.mediaBytes += length;
    state.bigMedia += big ? 1 : 0;
  }
  if (!rules.harmless.has(ext)) {
    state.contentBytes += length;
    state.bigContent += big ? 1 : 0;
  }
  if (rules.sourceExts.has(ext) || rules.sourceNames.has(base.toLowerCase())) {
    state.sources++;
  }
}

function classify(rules, entry, sourceRelease) {
  const { ext, base } = entry;
  const address =
    !entry.bundle &&
    rules.addressExts.has(ext) &&
    looksLikeAddress(rules, base);
  const windows = !entry.bundle && !address && rules.windows.has(ext);
  const macos =
    entry.bundle || (!entry.inBundle && !address && rules.macos.has(ext));
  const shortcut = !entry.bundle && !address && rules.shortcut.has(ext);
  const harmless =
    !entry.bundle &&
    (rules.harmless.has(ext) ||
      rules.video.has(ext) ||
      rules.audio.has(ext) ||
      rules.harmlessNames.has(base.toLowerCase()));
  const archive =
    !entry.bundle && (rules.archive.has(ext) || isSplitArchive(rules, ext));
  const loose =
    sourceRelease &&
    !entry.bundle &&
    !windows &&
    (ext == "" || rules.sourceScripts.has(ext) || rules.sourceExts.has(ext));
  const target =
    !harmless &&
    !loose &&
    !address &&
    (windows || macos || archive || ext == "" || rules.diskImage.has(ext));
  return { windows, macos, shortcut, archive, target };
}

function checkPiracy(rules, entry, kind, forms, dirStrong) {
  if (!kind.target) {
    return false;
  }
  if (dirStrong(entry.dirPath) || hasStrongWord(rules, forms)) {
    return true;
  }
  return (
    (kind.windows || kind.macos || rules.weakNameArchive.has(entry.ext)) &&
    hasWord(rules.weak, forms)
  );
}

function addTo(map, key, value) {
  const list = map.get(key);
  if (list) {
    list.push(value);
  } else {
    map.set(key, [value]);
  }
}

// Lists the password notes next to an archive or one folder above it, and
// returns the archives near a note that names a password.
function checkPasswords(rules, state, lists) {
  const near = new Map();
  for (const entry of state.entries) {
    if (
      !entry.bundle &&
      (rules.passwordArchive.has(entry.ext) || isSplitArchive(rules, entry.ext))
    ) {
      addTo(near, entry.dirPath, entry.path);
      if (entry.dirPath) {
        addTo(near, dirOf(entry.dirPath), entry.path);
      }
    }
  }
  const lured = [];
  for (const entry of state.entries) {
    if (entry.bundle || !rules.passwordExts.has(entry.ext)) {
      continue;
    }
    const archives = near.get(entry.dirPath);
    if (!archives || !isPasswordName(rules, entry.stem)) {
      continue;
    }
    lists["password-file"].add(entry.path);
    if (passwordWords(rules, entry.stem).lure) {
      lured.push(...archives);
    }
  }
  return lured;
}

function analyze(rules, metadata) {
  const lists = newLists(rules);
  const state = collect(rules, metadata, lists);
  const sourceRelease = state.sources >= rules.sourceMin;
  const dirCache = new Map();
  const dirStrong = dirPath => {
    if (!dirCache.has(dirPath)) {
      dirCache.set(dirPath, hasStrongWord(rules, wordForms(rules, dirPath)));
    }
    return dirCache.get(dirPath);
  };

  const mediaItems = [];
  for (const entry of state.entries) {
    const kind = classify(rules, entry, sourceRelease);
    const program = kind.windows || kind.macos;
    if (kind.windows) {
      lists["windows-program"].add(entry.path, true);
    }
    if (kind.macos) {
      lists["macos-installer"].add(entry.path, true);
    }
    if (kind.shortcut) {
      lists["shortcut-file"].add(entry.path);
    }
    if (
      (program || kind.shortcut) &&
      (rules.decoy.has(entry.prevExt) ||
        rules.paddedRe.test(
          entry.base.slice(0, Math.max(0, entry.base.lastIndexOf(".")))
        ))
    ) {
      lists["double-extension"].add(entry.path, program);
    }
    const forms =
      kind.target || kind.archive ? wordForms(rules, entry.base) : null;
    if (checkPiracy(rules, entry, kind, forms, dirStrong)) {
      lists["piracy-tool-name"].add(entry.path, program);
    }
    if (program || (kind.archive && hasWord(rules.suspiciousArchive, forms))) {
      mediaItems.push([entry.path, program]);
    }
  }

  const lured = checkPasswords(rules, state, lists);
  const mediaLike =
    nameLooksLikeMedia(rules, state.name) ||
    (state.mediaBytes > 0 &&
      state.mediaBytes * 2 >= state.contentBytes &&
      state.bigMedia * 2 >= state.bigContent);
  if (mediaLike) {
    for (const [path, program] of mediaItems) {
      lists["program-in-media"].add(path, program);
    }
    for (const path of lured) {
      lists["program-in-media"].add(path);
    }
  }
  return lists;
}

function assessWith(rules, metadata, source, extraTrustedSites) {
  const kind = source?.kind;
  const trusted =
    (kind == "page" || kind == "magnet-page") &&
    isTrustedOriginWith(rules, source?.origin, extraTrustedSites);

  const lists = analyze(rules, metadata);
  const reasons = [];
  for (const severity of ["high", "medium"]) {
    for (const [code, list] of Object.entries(lists)) {
      if (rules.severities[code] == severity && list.size) {
        reasons.push(list.toReason(rules, code));
      }
    }
  }
  const high = reasons.some(reason => reason.severity == "high");
  const medium = reasons.filter(reason => reason.severity == "medium").length;
  const shady = !trusted && (high || medium >= rules.mediumNeeded);
  return { verdict: shady ? "shady" : "ok", trusted, reasons };
}

export const M6ETorrentRisk = {
  assess(metadata, source, extraTrustedSites = []) {
    return assessWith(getRules(), metadata, source, extraTrustedSites);
  },

  /**
   * True for an https origin (scheme, host and an optional port, nothing
   * else) on an exact host of the bundled list, or equal to a site the user
   * added. A site that is a public suffix is never trusted.
   *
   * @param {string} origin
   * @param {string[]} extraTrustedSites The sites the user added.
   * @returns {boolean}
   */
  isTrustedOrigin(origin, extraTrustedSites = []) {
    return isTrustedOriginWith(getRules(), origin, extraTrustedSites);
  },

  /**
   * True when a host of a trusted site publishes other people's content, so
   * a page of that site must not vouch for a file on it.
   *
   * @param {string} host
   * @returns {boolean}
   */
  isExcludedHost(host) {
    return (
      typeof host == "string" &&
      isExcludedHostWith(getRules(), host.toLowerCase())
    );
  },

  /**
   * True when the user may add this origin with "Always trust".
   *
   * @param {string} origin
   * @returns {boolean}
   */
  canTrustSite(origin) {
    const parsed = parseHttpsOrigin(origin);
    return (
      !!parsed && parsed.host.includes(".") && !isPublicSuffix(parsed.host)
    );
  },

  getTrustedSites() {
    return getRules().trustedSites.map(site => ({
      name: site.name,
      hosts: [...site.hosts],
      domain: site.domain,
    }));
  },

  /**
   * Shortens a display name in the middle, keeping its last component and
   * extension, without splitting a character or a "<U+XXXX>" escape.
   *
   * @param {string} text
   * @param {number} max The longest result, in UTF-16 code units.
   * @returns {string}
   */
  shortenForDisplay(text, max) {
    return shortenUnits(String(text).match(DISPLAY_UNIT_RE) ?? [], max);
  },

  /**
   * Replaces the bundled rules, which are otherwise read from
   * m6e-torrent-rules.json on first use. Used by tests and by Node, where
   * there is no moz-src protocol.
   *
   * @param {object|null} raw The rules, or null to go back to the file.
   */
  setRules(raw) {
    gRules = raw ? compileRules(raw) : null;
  },

  /**
   * Replaces the public suffix check, which is Services.eTLD in Gecko.
   *
   * @param {Function|null} check host => boolean, or null for the default.
   */
  setPublicSuffixCheck(check) {
    gIsPublicSuffix = check;
  },
};
