/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// M6E: the Add torrent dialog (Phase 8, D33, D35). It only reads what
// gM6ETorrent.openAddDialog passes in window.arguments[0]. Nothing is added
// or started until the user presses Add or Add anyway.

"use strict";

const PRIORITY_HIGH = 3;
const PRIORITY_NORMAL = 2;
const PRIORITY_LOW = 1;
const PRIORITY_SKIP = 0;
const MAX_FILES_PER_REASON = 5;
const KNOWN_REASONS = [
  "windows-program",
  "double-extension",
  "bidi-override",
  "piracy-tool-name",
  "program-in-media",
  "macos-installer",
  "shortcut-file",
  "password-file",
];
const INVISIBLE_PATTERN = /[\p{Cc}\p{Cf}\u2028\u2029]/gu;

const params = window.arguments[0];
const $ = id => document.getElementById(id);

let metadata = null;
let source = params.source ?? { kind: "unknown", origin: null };
let files = [];
let risk = null;
let busy = false;
let failed = false;
let savePath = params.defaultSavePath || "";

/**
 * Makes a name safe to show: invisible and direction-changing characters
 * become visible escapes, so a name cannot disguise itself.
 */
function displayName(name) {
  return String(name ?? "").replace(
    INVISIBLE_PATTERN,
    ch => `<U+${ch.codePointAt(0).toString(16).toUpperCase().padStart(4, "0")}>`
  );
}

/**
 * Fluent DOM parses a translation as markup when it contains "<" or "&", so
 * a name placed in a message argument is escaped to show literally.
 */
function overlaySafe(text) {
  return text
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;");
}

function selectedFiles() {
  return files.filter(f => f.priority > PRIORITY_SKIP);
}

function bannerShown() {
  return !$("m6e-torrent-shady").hidden;
}

function canAdd() {
  return (
    !!metadata &&
    !busy &&
    !failed &&
    !bannerShown() &&
    !!savePath &&
    !!selectedFiles().length
  );
}

function updateButtons() {
  $("m6e-torrent-add-confirm").disabled = !canAdd();
  $("m6e-torrent-shady-add-anyway").disabled =
    busy || failed || !savePath || !selectedFiles().length;
  $("m6e-torrent-add-footer").hidden = bannerShown();
  $("m6e-torrent-shady-cancel").disabled = false;
}

function buildTree(entries) {
  const root = { folders: new Map(), files: [] };
  for (const entry of entries) {
    const parts = entry.path.split("/");
    let node = root;
    for (const part of parts.slice(0, -1)) {
      if (!node.folders.has(part)) {
        node.folders.set(part, { folders: new Map(), files: [] });
      }
      node = node.folders.get(part);
    }
    node.files.push({ entry, name: parts[parts.length - 1] });
  }
  return root;
}

function folderFiles(node) {
  const result = node.files.map(f => f.entry);
  for (const child of node.folders.values()) {
    result.push(...folderFiles(child));
  }
  return result;
}

function renderTree(entries) {
  const container = $("m6e-torrent-add-tree");
  container.replaceChildren();
  const folderBoxes = [];

  const renderNode = (node, list) => {
    for (const [name, child] of node.folders) {
      const item = document.createElement("li");
      item.className = "folder";
      const row = document.createElement("div");
      row.className = "row";
      const box = document.createElement("input");
      box.type = "checkbox";
      document.l10n.setAttributes(box, "m6e-torrent-add-folder-checkbox", {
        name: displayName(name),
      });
      const label = document.createElement("span");
      label.className = "name";
      label.textContent = displayName(name);
      row.append(box, label);
      const sub = document.createElement("ul");
      renderNode(child, sub);
      item.append(row, sub);
      list.append(item);
      const members = folderFiles(child);
      folderBoxes.push({ box, members });
      box.addEventListener("change", () => {
        for (const entry of members) {
          setPriority(entry, box.checked ? PRIORITY_NORMAL : PRIORITY_SKIP);
        }
        refresh();
      });
    }
    for (const { entry, name } of node.files) {
      const item = document.createElement("li");
      item.className = "file";
      const row = document.createElement("div");
      row.className = "row";
      const box = document.createElement("input");
      box.type = "checkbox";
      box.className = "file-checkbox";
      document.l10n.setAttributes(box, "m6e-torrent-add-file-checkbox", {
        name: displayName(name),
      });
      const label = document.createElement("span");
      label.className = "name";
      label.textContent = displayName(name);
      const size = document.createElement("span");
      size.className = "size";
      size.textContent = params.formatBytes(entry.length);
      const select = document.createElement("select");
      select.className = "file-priority";
      document.l10n.setAttributes(select, "m6e-torrent-add-file-priority", {
        name: displayName(name),
      });
      for (const [value, id] of [
        [PRIORITY_HIGH, "m6e-torrent-priority-high"],
        [PRIORITY_NORMAL, "m6e-torrent-priority-normal"],
        [PRIORITY_LOW, "m6e-torrent-priority-low"],
        [PRIORITY_SKIP, "m6e-torrent-priority-skip"],
      ]) {
        const option = document.createElement("option");
        option.value = String(value);
        document.l10n.setAttributes(option, id);
        select.append(option);
      }
      row.append(box, label, size, select);
      item.append(row);
      list.append(item);
      entry.box = box;
      entry.select = select;
      setPriority(entry, entry.priority);
      box.addEventListener("change", () => {
        setPriority(entry, box.checked ? PRIORITY_NORMAL : PRIORITY_SKIP);
        refresh();
      });
      select.addEventListener("change", () => {
        setPriority(entry, Number(select.value));
        refresh();
      });
    }
  };

  const top = document.createElement("ul");
  renderNode(buildTree(entries), top);
  container.append(top);
  container._folderBoxes = folderBoxes;
}

function setPriority(entry, priority) {
  entry.priority = priority;
  entry.box.checked = priority > PRIORITY_SKIP;
  entry.select.value = String(priority);
}

function refresh() {
  for (const { box, members } of $("m6e-torrent-add-tree")._folderBoxes ??
    []) {
    const chosen = members.filter(m => m.priority > PRIORITY_SKIP).length;
    box.checked = chosen == members.length && chosen > 0;
    box.indeterminate = chosen > 0 && chosen < members.length;
  }
  const chosen = selectedFiles();
  const bytes = chosen.reduce((sum, f) => sum + f.length, 0);
  document.l10n.setAttributes($("m6e-torrent-add-selection"), "m6e-torrent-add-selection", {
    selected: chosen.length,
    total: files.length,
    size: params.formatBytes(bytes),
  });
  updateSpaceWarning(bytes);
  updateButtons();
}

function updateSpaceWarning(bytes) {
  const warning = $("m6e-torrent-add-no-space");
  let available = null;
  if (savePath) {
    try {
      available = params.getFreeSpace(savePath);
    } catch (e) {
      console.error(e);
    }
  }
  const short = typeof available == "number" && bytes > available;
  warning.hidden = !short;
  if (short) {
    document.l10n.setAttributes(warning, "m6e-torrent-add-no-space", {
      needed: params.formatBytes(bytes),
      available: params.formatBytes(available),
    });
  }
}

async function renderBanner() {
  const list = $("m6e-torrent-shady-reasons");
  list.replaceChildren();
  for (const reason of risk.reasons ?? []) {
    const names = (reason.files ?? []).map(f => overlaySafe(displayName(f)));
    let text = names.slice(0, MAX_FILES_PER_REASON).join(", ");
    if (names.length > MAX_FILES_PER_REASON) {
      const more = await document.l10n.formatValue(
        "m6e-torrent-shady-more-files",
        { count: names.length - MAX_FILES_PER_REASON }
      );
      text += `, ${more}`;
    }
    const item = document.createElement("li");
    const code = KNOWN_REASONS.includes(reason.code) ? reason.code : "other";
    document.l10n.setAttributes(item, `m6e-torrent-shady-${code}`, {
      files: text,
    });
    list.append(item);
  }
  await document.l10n.translateElements([...list.children]);
}

function isHttpsOrigin(origin) {
  try {
    return typeof origin == "string" && new URL(origin).protocol == "https:";
  } catch (e) {
    return false;
  }
}

async function assessAndShow() {
  try {
    risk = params.assess(metadata, source, params.trustedSites());
  } catch (e) {
    console.error("M6E: the Shady Torrent check failed", e);
    risk = null;
  }
  const shady =
    risk?.verdict == "shady" &&
    !risk.trusted &&
    !params.isAcknowledged(metadata.infoHash);
  if (shady) {
    await renderBanner();
  }
  $("m6e-torrent-shady").hidden = !shady;

  const trustLabel = $("m6e-torrent-add-trust-label");
  const canTrust = isHttpsOrigin(source?.origin) && !risk?.trusted;
  trustLabel.hidden = !canTrust;
  if (canTrust) {
    document.l10n.setAttributes(
      $("m6e-torrent-add-trust-text"),
      "m6e-torrent-add-trust-site",
      { site: source.origin }
    );
  }
}

async function showMetadata(meta) {
  metadata = meta;
  files = (meta.files ?? [])
    .filter(f => !f.pad)
    .map(f => ({
      index: f.index,
      path: String(f.path),
      length: Number(f.length) || 0,
      priority: PRIORITY_NORMAL,
    }));

  $("m6e-torrent-add-name").textContent = displayName(meta.name);
  const total = files.reduce((sum, f) => sum + f.length, 0);
  document.l10n.setAttributes($("m6e-torrent-add-size"), "m6e-torrent-add-size-value", {
    size: params.formatBytes(total),
    count: files.length,
  });
  renderTree(files);
  $("m6e-torrent-add-save-path").value = savePath;
  $("m6e-torrent-add-sequential").checked = Services.prefs.getBoolPref(
    "m6e.torrent.sequentialDefault",
    false
  );

  await assessAndShow();

  $("m6e-torrent-add-fetching").hidden = true;
  $("m6e-torrent-add-details").hidden = false;
  refresh();
  await document.l10n.translateFragment(document.body);
  const active = document.activeElement;
  if (bannerShown() && (!active || active == document.body || active.id == "m6e-torrent-add-cancel")) {
    $("m6e-torrent-shady-title").focus();
  } else if (!active || active == document.body) {
    $("m6e-torrent-add-confirm").focus();
  }
  document.documentElement.setAttribute("m6e-ready", "true");
}

function showFailure(message) {
  failed = true;
  const error = $("m6e-torrent-add-error");
  document.l10n.setAttributes(error, "m6e-torrent-add-failed", { message });
  error.hidden = false;
  $("m6e-torrent-add-fetching").hidden = true;
  updateButtons();
}

async function submitAdd(anyway) {
  if (!metadata || busy || failed) {
    return;
  }
  if (bannerShown() && !anyway) {
    return;
  }
  if (!canAddIgnoringBanner()) {
    return;
  }
  busy = true;
  updateButtons();
  $("m6e-torrent-add-error").hidden = true;
  try {
    await params.service.confirmAdd(params.tid, {
      files: files.map(f => ({ index: f.index, priority: f.priority })),
      savePath,
      sequential: $("m6e-torrent-add-sequential").checked,
      start: $("m6e-torrent-add-start-now").checked,
    });
  } catch (e) {
    busy = false;
    if (e?.code == "duplicate" && e.tid) {
      params.showExisting(e.tid);
      window.close();
      return;
    }
    const error = $("m6e-torrent-add-error");
    document.l10n.setAttributes(error, "m6e-torrent-add-failed", {
      message: e?.message ?? "",
    });
    error.hidden = false;
    updateButtons();
    return;
  }
  if (anyway) {
    params.acknowledge(metadata.infoHash);
  }
  if (
    !$("m6e-torrent-add-trust-label").hidden &&
    $("m6e-torrent-add-trust").checked &&
    isHttpsOrigin(source?.origin)
  ) {
    params.addTrustedSite(source.origin);
  }
  params.added = true;
  window.close();
}

function canAddIgnoringBanner() {
  return !!savePath && !!selectedFiles().length;
}

async function chooseFolder() {
  const picked = await params.pickFolder(savePath);
  if (picked) {
    savePath = picked;
    $("m6e-torrent-add-save-path").value = picked;
    refresh();
  }
}

function onMetadataNotification(subject, topic, data) {
  let detail;
  try {
    detail = JSON.parse(data);
  } catch (e) {
    return;
  }
  if (detail.tid != params.tid) {
    return;
  }
  if (topic == "m6e-torrent-metadata" && !metadata && detail.metadata) {
    showMetadata(detail.metadata).catch(console.error);
  } else if (topic == "m6e-torrent-error" && !busy) {
    showFailure(detail.message ?? "");
  }
}

const observer = { observe: onMetadataNotification };

function init() {
  $("m6e-torrent-add-form").addEventListener("submit", event => {
    event.preventDefault();
    submitAdd(false);
  });
  $("m6e-torrent-add-cancel").addEventListener("click", () => window.close());
  $("m6e-torrent-shady-cancel").addEventListener("click", () =>
    window.close()
  );
  $("m6e-torrent-shady-add-anyway").addEventListener("click", () =>
    submitAdd(true)
  );
  $("m6e-torrent-add-browse").addEventListener("click", () => chooseFolder());

  Services.obs.addObserver(observer, "m6e-torrent-metadata");
  Services.obs.addObserver(observer, "m6e-torrent-error");
  window.addEventListener(
    "unload",
    () => {
      Services.obs.removeObserver(observer, "m6e-torrent-metadata");
      Services.obs.removeObserver(observer, "m6e-torrent-error");
    },
    { once: true }
  );

  const known = params.metadata ?? params.takeMetadata(params.tid);
  if (known) {
    return showMetadata(known);
  }
  $("m6e-torrent-add-cancel").focus();
  return Promise.resolve();
}

document.mozSubdialogReady = init().catch(console.error);
