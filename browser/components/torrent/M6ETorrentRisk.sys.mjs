/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// M6E: the Shady Torrent check (P8.3, spec 8.3, D35). A pure function of the
// torrent metadata, where it came from and the bundled rules.

const RULES_URL = "moz-src:///browser/components/torrent/m6e-torrent-rules.json";

let gRules = null;

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

function setOf(list) {
  return new Set(list);
}

function codePointClass(ranges, extra = []) {
  const hex = n => "\\u{" + n.toString(16) + "}";
  let body = "";
  for (const [from, to] of ranges) {
    body += from == to ? hex(from) : hex(from) + "-" + hex(to);
  }
  for (const point of extra) {
    body += hex(point);
  }
  return "[" + body + "]";
}

function compileRules(raw) {
  const ext = raw.extensions;
  const hiddenClass = codePointClass(raw.hiddenCharacters);
  const joinerClass = codePointClass([], raw.joiners);
  const mediaName = raw.mediaName;
  return {
    maxFiles: raw.maxFilesPerReason,
    maxDisplay: raw.maxDisplayLength,
    mediumNeeded: raw.mediumNeeded,
    trustedSites: raw.trustedSites.map(site => ({
      name: site.name,
      domain: site.domain.toLowerCase(),
      exclude: site.exclude.map(host => host.toLowerCase()),
    })),
    windows: setOf(ext.windowsProgram),
    macos: setOf(ext.macosInstaller),
    shortcut: setOf(ext.shortcut),
    archive: setOf(ext.archive),
    passwordArchive: setOf(ext.passwordArchive),
    weakNameArchive: setOf(ext.weakNameArchive),
    video: setOf(ext.video),
    audio: setOf(ext.audio),
    harmless: setOf(ext.harmless),
    decoy: setOf(ext.decoy),
    paddedMin: raw.paddedExtensionMinSpaces,
    paddedRe: new RegExp(
      "[\\p{Zs}\\t]{" + raw.paddedExtensionMinSpaces + ",}[^/]*\\.[^./]*$",
      "u"
    ),
    strong: setOf(raw.piracy.strong),
    strongPairs: raw.piracy.strongPairs,
    weak: setOf(raw.piracy.weak),
    mediaTokens: setOf(mediaName.tokens),
    mediaPairs: mediaName.pairs,
    mediaPatterns: mediaName.patterns.map(p => new RegExp(p, "u")),
    suspiciousArchive: setOf(raw.suspiciousArchiveNames.tokens),
    passwordNames: raw.passwordFiles.names,
    passwordExts: setOf(raw.passwordFiles.extensions),
    hiddenRe: new RegExp(hiddenClass, "u"),
    hiddenGlobalRe: new RegExp(hiddenClass, "gu"),
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

function tokenize(text) {
  return text
    .normalize("NFKC")
    .replace(/(\p{Ll})(\p{Lu})/gu, "$1 $2")
    .toLowerCase()
    .split(/[^\p{L}\p{N}]+/u)
    .filter(Boolean);
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

function hostMatches(host, domain) {
  return host == domain || host.endsWith("." + domain);
}

function parseHttpsOrigin(value) {
  if (typeof value != "string" || !value) {
    return null;
  }
  let url;
  try {
    url = new URL(value);
  } catch (e) {
    return null;
  }
  if (
    url.protocol != "https:" ||
    url.username ||
    url.password ||
    !url.hostname ||
    url.hostname.endsWith(".")
  ) {
    return null;
  }
  return { host: url.hostname, port: url.port };
}

function parseExtraSite(value) {
  if (typeof value != "string" || !value.trim()) {
    return null;
  }
  const text = value.trim();
  const parsed = parseHttpsOrigin(text.includes("://") ? text : "https://" + text);
  if (!parsed || !parsed.host.includes(".")) {
    return null;
  }
  if (text.includes("://") && !text.toLowerCase().startsWith("https://")) {
    return null;
  }
  return parsed;
}

function isTrustedOriginWith(rules, origin, extraTrustedSites) {
  const parsed = parseHttpsOrigin(origin);
  if (!parsed) {
    return false;
  }
  if (!parsed.port) {
    for (const site of rules.trustedSites) {
      if (
        hostMatches(parsed.host, site.domain) &&
        !site.exclude.some(excluded => hostMatches(parsed.host, excluded))
      ) {
        return true;
      }
    }
  }
  if (Array.isArray(extraTrustedSites)) {
    for (const entry of extraTrustedSites) {
      const extra = parseExtraSite(entry);
      if (
        extra &&
        extra.port == parsed.port &&
        hostMatches(parsed.host, extra.host)
      ) {
        return true;
      }
    }
  }
  return false;
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
    prevDot > 0 ? stem.slice(prevDot + 1).trim().toLowerCase() : "";
  return { stem, ext, prevExt };
}

function isPadFile(file) {
  if (file.pad === true) {
    return true;
  }
  return /(^|\/)\.pad\/\d+$/.test(file.path);
}

function findBundle(parts) {
  for (let i = 0; i < parts.length - 1; i++) {
    if (parts[i].toLowerCase().endsWith(".app") && parts[i].length > 4) {
      return parts.slice(0, i + 1).join("/");
    }
  }
  return null;
}

function hasHidden(rules, text) {
  return rules.hiddenRe.test(text) || rules.joinerEdgeRe.test(text);
}

function describe(rules, path) {
  let text = path.replace(
    rules.hiddenGlobalRe,
    ch => "<U+" + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, "0") + ">"
  );
  if (text.length > rules.maxDisplay) {
    text = text.slice(0, rules.maxDisplay) + "...";
  }
  return text;
}

function nameLooksLikeMedia(rules, name) {
  const tokens = tokenize(name);
  for (const token of tokens) {
    if (
      rules.mediaTokens.has(token) ||
      rules.mediaPatterns.some(re => re.test(token))
    ) {
      return true;
    }
  }
  return hasPair(tokens, rules.mediaPairs);
}

function hasStrongPiracyToken(rules, tokens) {
  return (
    tokens.some(token => rules.strong.has(token)) ||
    hasPair(tokens, rules.strongPairs)
  );
}

function isPasswordName(rules, stem) {
  const tokens = tokenize(stem);
  if (!tokens.length) {
    return false;
  }
  if (tokens.includes("password") || tokens.includes("passwords")) {
    return true;
  }
  const joined = tokens.join(" ");
  return rules.passwordNames.some(
    name => joined == name || joined.startsWith(name + " ")
  );
}

class ReasonList {
  constructor(rules) {
    this.rules = rules;
    this.files = [];
    this.seen = new Set();
  }

  add(path) {
    if (!this.seen.has(path)) {
      this.seen.add(path);
      this.files.push(path);
    }
  }

  get size() {
    return this.files.length;
  }

  toReason(code, severity) {
    const max = this.rules.maxFiles;
    return {
      code,
      severity,
      files: this.files.slice(0, max).map(path => describe(this.rules, path)),
      moreFiles: Math.max(0, this.files.length - max),
    };
  }
}

function analyze(rules, metadata) {
  const lists = {
    "windows-program": new ReasonList(rules),
    "double-extension": new ReasonList(rules),
    "bidi-override": new ReasonList(rules),
    "piracy-tool-name": new ReasonList(rules),
    "program-in-media": new ReasonList(rules),
    "macos-installer": new ReasonList(rules),
    "shortcut-file": new ReasonList(rules),
    "password-file": new ReasonList(rules),
  };

  const entries = [];
  const bundles = new Set();
  const dirInfo = new Map();
  const archivesByDir = new Map();
  const passwordCandidates = [];
  let mediaCount = 0;
  let contentCount = 0;
  let mediaBytes = 0;
  let totalBytes = 0;

  const files = Array.isArray(metadata?.files) ? metadata.files : [];
  for (const file of files) {
    if (!file || typeof file.path != "string" || isPadFile(file)) {
      continue;
    }
    const path = file.path;
    const length = Number.isFinite(file.length) ? file.length : 0;
    const parts = path.split("/");
    const base = parts[parts.length - 1];
    const dirPath = parts.slice(0, -1).join("/");

    if (hasHidden(rules, path)) {
      lists["bidi-override"].add(path);
    }

    totalBytes += length;
    const bundle = findBundle(parts);
    if (bundle) {
      if (!bundles.has(bundle)) {
        bundles.add(bundle);
        entries.push({
          path: bundle,
          dirPath: bundle.includes("/")
            ? bundle.slice(0, bundle.lastIndexOf("/"))
            : "",
          base: bundle.slice(bundle.lastIndexOf("/") + 1),
          ...splitName(bundle.slice(bundle.lastIndexOf("/") + 1)),
          bundle: true,
        });
      }
      continue;
    }

    const { ext, prevExt } = splitName(base);
    entries.push({ path, dirPath, base, ext, prevExt, bundle: false });

    if (rules.video.has(ext) || rules.audio.has(ext)) {
      mediaCount++;
      mediaBytes += length;
    }
    if (!rules.harmless.has(ext)) {
      contentCount++;
    }

    if (rules.passwordArchive.has(ext) || /^r\d\d$/.test(ext)) {
      archivesByDir.set(dirPath, true);
    }
    if (rules.passwordExts.has(ext)) {
      passwordCandidates.push({ path, dirPath, stem: splitName(base).stem });
    }
  }

  if (
    !lists["bidi-override"].size &&
    typeof metadata?.name == "string" &&
    hasHidden(rules, metadata.name)
  ) {
    lists["bidi-override"].add(metadata.name);
  }

  const programFiles = [];
  for (const entry of entries) {
    const { path, base, dirPath, ext, prevExt } = entry;
    const isWindows = !entry.bundle && rules.windows.has(ext);
    const isMacos = rules.macos.has(ext);
    const isShortcut = !entry.bundle && rules.shortcut.has(ext);
    const isProgram = isWindows || isMacos;

    if (isWindows) {
      lists["windows-program"].add(path);
    }
    if (isMacos) {
      lists["macos-installer"].add(path);
    }
    if (isShortcut) {
      lists["shortcut-file"].add(path);
    }

    if (isProgram || isShortcut) {
      const padded = rules.paddedRe.test(base);
      if (rules.decoy.has(prevExt) || padded) {
        lists["double-extension"].add(path);
      }
    }

    const baseTokens = tokenize(base);
    let dir = dirInfo.get(dirPath);
    if (!dir) {
      dir = { strong: hasStrongPiracyToken(rules, tokenize(dirPath)) };
      dirInfo.set(dirPath, dir);
    }
    const harmless =
      !entry.bundle &&
      (rules.harmless.has(ext) ||
        rules.video.has(ext) ||
        rules.audio.has(ext));
    let piracy = false;
    if (!harmless) {
      if (dir.strong || hasStrongPiracyToken(rules, baseTokens)) {
        piracy = true;
      } else if (
        baseTokens.some(token => rules.weak.has(token)) &&
        (isProgram || rules.weakNameArchive.has(ext))
      ) {
        piracy = true;
      }
    }
    if (piracy) {
      lists["piracy-tool-name"].add(path);
    }

    if (isProgram) {
      programFiles.push(path);
    } else if (rules.archive.has(ext)) {
      const nameTokens = tokenize(base);
      if (nameTokens.some(token => rules.suspiciousArchive.has(token))) {
        programFiles.push(path);
      }
    }
  }

  for (const candidate of passwordCandidates) {
    if (
      archivesByDir.has(candidate.dirPath) &&
      isPasswordName(rules, candidate.stem)
    ) {
      lists["password-file"].add(candidate.path);
    }
  }

  const mediaLike =
    nameLooksLikeMedia(rules, metadata?.name || "") ||
    (mediaCount > 0 &&
      mediaCount * 2 >= contentCount &&
      mediaBytes * 2 >= totalBytes);
  if (mediaLike) {
    for (const path of programFiles) {
      lists["program-in-media"].add(path);
    }
  }

  return lists;
}

const SEVERITY = {
  "windows-program": "high",
  "double-extension": "high",
  "bidi-override": "high",
  "piracy-tool-name": "high",
  "program-in-media": "high",
  "macos-installer": "medium",
  "shortcut-file": "medium",
  "password-file": "medium",
};

function assessWith(rules, metadata, source, extraTrustedSites) {
  const kind = source?.kind;
  const origin = source?.origin;
  const trusted =
    (kind == "page" || kind == "magnet-page") &&
    isTrustedOriginWith(rules, origin, extraTrustedSites);

  const lists = analyze(rules, metadata);
  const reasons = [];
  let medium = 0;
  for (const severity of ["high", "medium"]) {
    for (const [code, list] of Object.entries(lists)) {
      if (SEVERITY[code] == severity && list.size) {
        reasons.push(list.toReason(code, severity));
        if (severity == "medium") {
          medium++;
        }
      }
    }
  }

  const high = reasons.some(reason => reason.severity == "high");
  const shady = !trusted && (high || medium >= rules.mediumNeeded);
  return { verdict: shady ? "shady" : "ok", trusted, reasons };
}

export const M6ETorrentRisk = {
  assess(metadata, source, extraTrustedSites = []) {
    return assessWith(getRules(), metadata, source, extraTrustedSites);
  },

  isTrustedOrigin(origin, extraTrustedSites = []) {
    return isTrustedOriginWith(getRules(), origin, extraTrustedSites);
  },

  getTrustedSites() {
    return getRules().trustedSites.map(site => ({
      name: site.name,
      domain: site.domain,
    }));
  },

  /**
   * Replaces the bundled rules, which are otherwise read from
   * m6e-torrent-rules.json on first use. Pass null to go back to the file.
   * Used by tests and by Node, where there is no moz-src protocol.
   */
  setRules(raw) {
    gRules = raw ? compileRules(raw) : null;
  },
};
