/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Runs M6ETorrentService against the real m6e-torrent helper
// (tools/m6e/torrent/build/m6e-torrent, built by build-helper.sh) and a
// synthetic swarm on 127.0.0.1 (swarm_server.py, which uses
// tools/m6e/torrent/fixtures). Spec 8.9, D38: no real torrent, magnet link,
// tracker or DHT node is ever used. The tests skip when the helper binary or
// python3 is missing. M6E_TORRENT_HELPER overrides the helper path.

const { M6ETorrentService } = ChromeUtils.importESModule(
  "moz-src:///browser/components/torrent/M6ETorrentService.sys.mjs"
);
const { Subprocess } = ChromeUtils.importESModule(
  "resource://gre/modules/Subprocess.sys.mjs"
);
const { FileUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/FileUtils.sys.mjs"
);
const { setTimeout, clearTimeout } = ChromeUtils.importESModule(
  "resource://gre/modules/Timer.sys.mjs"
);

const PREFIX = "m6e.torrent.";
const QUARANTINE_ATTR = "com.apple.quarantine";
const ACTIVE = ["checking", "queued", "downloading", "seeding"];
const DONE = ["seeding", "completed"];

const env = { ok: false, why: "", helper: "", python: "", fixtures: "" };
const errors = [];
const cleanups = [];

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function withTimeout(promise, ms, what) {
  let timer;
  const timeout = new Promise((resolve, reject) => {
    timer = setTimeout(() => reject(new Error(`timed out: ${what}`)), ms);
  });
  return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

async function waitFor(condition, what, ms = 60_000) {
  const end = Date.now() + ms;
  for (;;) {
    const value = await condition();
    if (value) {
      return value;
    }
    if (Date.now() > end) {
      throw new Error(`timed out: ${what}`);
    }
    await sleep(100);
  }
}

function notification(topic, predicate = () => true, ms = 60_000) {
  let observer;
  const promise = new Promise(resolve => {
    observer = {
      observe(subject, t, data) {
        const parsed = data ? JSON.parse(data) : null;
        if (predicate(parsed)) {
          Services.obs.removeObserver(observer, topic);
          resolve(parsed);
        }
      },
    };
    Services.obs.addObserver(observer, topic);
  });
  const timeout = withTimeout(promise, ms, `notification ${topic}`);
  timeout.catch(() => Services.obs.removeObserver(observer, topic));
  return timeout;
}

async function rejectsWithCode(promise, codes, what) {
  try {
    await promise;
  } catch (e) {
    Assert.ok(codes.includes(e.code), `${what}: code ${e.code} (${e.message})`);
    return e;
  }
  Assert.ok(false, `${what}: expected a rejection`);
  return null;
}

function sha256(bytes) {
  const hash = Cc["@mozilla.org/security/hash;1"].createInstance(
    Ci.nsICryptoHash
  );
  hash.init(hash.SHA256);
  hash.update(bytes, bytes.length);
  return hash.finish(true);
}

function torrentsDir() {
  return PathUtils.join(PathUtils.profileDir, "torrents");
}

function inboxDir() {
  return PathUtils.join(torrentsDir(), "inbox");
}

async function inboxFiles() {
  return IOUtils.getChildren(inboxDir()).catch(() => []);
}

async function newFolder(name) {
  return IOUtils.createUniqueDirectory(PathUtils.tempDir, "m6e-torrent-" + name);
}

function lookupEnvironment() {
  const top = Services.env.get("MOZ_DEVELOPER_REPO_DIR");
  const torrentDir = top ? PathUtils.join(top, "tools", "m6e", "torrent") : "";
  const helper =
    Services.env.get("M6E_TORRENT_HELPER") ||
    (torrentDir ? PathUtils.join(torrentDir, "build", "m6e-torrent") : "");
  env.helper = helper;
  env.fixtures = torrentDir ? PathUtils.join(torrentDir, "fixtures") : "";
}

add_setup(async function setup() {
  do_get_profile();
  lookupEnvironment();
  if (!env.helper || !(await IOUtils.exists(env.helper))) {
    env.why = `no helper at "${env.helper}"`;
    return;
  }
  if (!env.fixtures || !(await IOUtils.exists(env.fixtures))) {
    env.why = "no tools/m6e/torrent/fixtures";
    return;
  }
  try {
    env.python = await Subprocess.pathSearch("python3");
  } catch (e) {
    env.why = "no python3";
    return;
  }
  env.ok = true;
  const observer = {
    observe(subject, topic, data) {
      errors.push(JSON.parse(data));
    },
  };
  Services.obs.addObserver(observer, "m6e-torrent-error");
  registerCleanupFunction(async () => {
    Services.obs.removeObserver(observer, "m6e-torrent-error");
    await runCleanups();
    await M6ETorrentService.testing.reset();
  });
});

async function runCleanups() {
  while (cleanups.length) {
    try {
      await cleanups.pop()();
    } catch (e) {
      info(`cleanup failed: ${e}`);
    }
  }
}

// Registers a test that is skipped without the helper.
function helper_task(fn) {
  const task = async () => {
    if (!env.ok) {
      info(`skipped: ${env.why}`);
      Assert.ok(true, "skipped");
      return;
    }
    try {
      await fresh();
      await fn();
    } finally {
      await runCleanups();
    }
  };
  Object.defineProperty(task, "name", { value: fn.name });
  add_task(task);
}

async function fresh(timings = {}) {
  await M6ETorrentService.testing.reset();
  await IOUtils.remove(torrentsDir(), { recursive: true, ignoreAbsent: true });
  for (const name of Services.prefs.getChildList(PREFIX)) {
    Services.prefs.clearUserPref(name);
  }
  Services.prefs.setStringPref(PREFIX + "helperPath", env.helper);
  Services.prefs.setBoolPref(PREFIX + "dht", false);
  Services.prefs.setBoolPref(PREFIX + "pex", false);
  Services.prefs.setBoolPref(PREFIX + "lpd", false);
  Services.prefs.setBoolPref(PREFIX + "portMapping", false);
  Services.prefs.setStringPref(PREFIX + "bindInterface", "lo0");
  Services.prefs.setIntPref(PREFIX + "seedRatioPercent", 0);
  errors.length = 0;
  M6ETorrentService.testing.setTimings({
    notifyIntervalMs: 200,
    restartBackoffMs: [100, 100, 100],
    ...timings,
  });
}

// A tracker and a seed on 127.0.0.1 serving one generated torrent.
async function startSwarm({ name, kind = "multi", stall = false, hostile = false }) {
  const out = await newFolder("swarm");
  const arguments_ = [
    "-I",
    do_get_file("swarm_server.py").path,
    "--fixtures-dir",
    env.fixtures,
    "--out",
    out,
    "--name",
    name,
    "--kind",
    kind,
  ];
  if (stall) {
    arguments_.push("--stall");
  }
  if (hostile) {
    arguments_.push("--hostile");
  }
  const proc = await Subprocess.call({
    command: env.python,
    arguments: arguments_,
    stdin: "pipe",
    stdout: "pipe",
  });
  const swarm = {
    proc,
    buffer: "",
    out,
    async line() {
      while (!swarm.buffer.includes("\n")) {
        const chunk = await withTimeout(
          proc.stdout.readString(),
          30_000,
          "the swarm process"
        );
        if (!chunk) {
          throw new Error("the swarm process ended");
        }
        swarm.buffer += chunk;
      }
      const end = swarm.buffer.indexOf("\n");
      const text = swarm.buffer.slice(0, end);
      swarm.buffer = swarm.buffer.slice(end + 1);
      return JSON.parse(text);
    },
    async ask(command) {
      await proc.stdin.write(command + "\n");
      return swarm.line();
    },
    async stop() {
      await proc.stdin.close().catch(() => {});
      await withTimeout(proc.wait(), 10_000, "the swarm to end").catch(() =>
        proc.kill(0)
      );
    },
  };
  cleanups.push(() => swarm.stop());
  Object.assign(swarm, await swarm.line());
  Assert.equal(swarm.tid.length, 40, "the swarm has a torrent");
  return swarm;
}

function payloadPath(save, swarm, file) {
  return swarm.multi
    ? PathUtils.join(save, swarm.name, ...file.path)
    : PathUtils.join(save, swarm.name);
}

async function checkPayload(save, swarm, { skip = [] } = {}) {
  for (const [i, file] of swarm.files.entries()) {
    const path = payloadPath(save, swarm, file);
    if (skip.includes(i)) {
      if (await IOUtils.exists(path)) {
        const bytes = await IOUtils.read(path);
        Assert.notEqual(
          sha256(bytes),
          file.sha256,
          `${file.path.join("/")} was not selected and must not be complete`
        );
      }
      continue;
    }
    const bytes = await IOUtils.read(path);
    Assert.equal(bytes.length, file.length, `${file.path.join("/")} length`);
    Assert.equal(sha256(bytes), file.sha256, `${file.path.join("/")} content`);
  }
}

async function statusOf(tid) {
  const { torrents } = await M6ETorrentService.list();
  return torrents.find(t => t.tid == tid) ?? null;
}

function waitForState(tid, states, ms = 60_000) {
  return waitFor(
    async () => {
      const status = await statusOf(tid);
      return status && states.includes(status.state) ? status : null;
    },
    `${tid} in ${states}`,
    ms
  );
}

async function isProcessAlive(pid) {
  const proc = await Subprocess.call({
    command: "/bin/ps",
    arguments: ["-p", String(pid), "-o", "pid="],
    stdout: "pipe",
  });
  const { exitCode } = await proc.wait();
  return exitCode == 0;
}

async function killHard(pid) {
  const proc = await Subprocess.call({
    command: "/bin/kill",
    arguments: ["-9", String(pid)],
  });
  await proc.wait();
}

add_task(function test_environment_report() {
  info(env.ok ? `helper ${env.helper}` : `skipping the helper tests: ${env.why}`);
  Assert.ok(true);
});

helper_task(async function test_no_helper_at_idle() {
  await M6ETorrentService.init();
  const { torrents } = await M6ETorrentService.list();
  Assert.deepEqual(torrents, [], "no torrents");
  Assert.equal(await M6ETorrentService.hasActiveTorrents(), false);
  Assert.equal(M6ETorrentService.activeTorrentCount, 0);
  Assert.ok(
    !M6ETorrentService.testing.isHelperRunning(),
    "nothing starts the helper at startup or by listing"
  );
  Assert.equal(M6ETorrentService.getNetworkState().listening, null);
});

helper_task(async function test_add_file_download_complete() {
  const swarm = await startSwarm({ name: "m6e-svc-file" });
  const save = await newFolder("save");
  const { tid, metadata } = await M6ETorrentService.addFile(swarm.torrent, {
    kind: "unknown",
    origin: null,
  });
  Assert.equal(tid, swarm.tid);
  Assert.equal(metadata.files.length, 3);
  Assert.equal(metadata.totalSize, 135000);
  Assert.deepEqual(await inboxFiles(), [], "the staged copy is deleted");
  Assert.ok(M6ETorrentService.testing.isHelperRunning());

  await sleep(2000);
  const idle = await swarm.ask("stats");
  Assert.equal(idle.requests, 0, "adding requests no payload");
  Assert.deepEqual(await IOUtils.getChildren(save), [], "nothing written yet");

  const changes = [];
  const observer = {
    observe(subject, topic, data) {
      changes.push(JSON.parse(data));
    },
  };
  Services.obs.addObserver(observer, "m6e-torrent-changed");
  cleanups.push(() =>
    Services.obs.removeObserver(observer, "m6e-torrent-changed")
  );
  const completed = notification("m6e-torrent-completed", d => d.tid == tid);
  const confirmed = await M6ETorrentService.confirmAdd(tid, {
    savePath: save,
    start: true,
  });
  Assert.equal(confirmed.status.tid, tid);
  const done = await completed;
  if (done.warning) {
    info(`completed with a warning: ${done.warning}`);
  }
  await checkPayload(save, swarm);

  Assert.ok(
    changes.some(c => c.torrents.some(t => t.tid == tid)),
    "progress was reported"
  );
  const status = await waitForState(tid, DONE);
  Assert.equal(status.progress, 1);
  Assert.equal(status.error, null);
  for (const file of swarm.files) {
    const path = payloadPath(save, swarm, file);
    const mac = new FileUtils.File(path).QueryInterface(Ci.nsILocalFileMac);
    Assert.ok(mac.hasXAttr(QUARANTINE_ATTR), `${file.path} is quarantined`);
    const { permissions } = await IOUtils.stat(path);
    Assert.equal(permissions & 0o111, 0, `${file.path} is not executable`);
  }

  await waitFor(
    () => M6ETorrentService.testing.trackers(tid),
    "the trackers event"
  );
  Assert.equal(M6ETorrentService.testing.trackers(tid)[0].url, swarm.tracker);
  Assert.ok(
    Number.isInteger(M6ETorrentService.getNetworkState().listening),
    "the listening event reports the port while a torrent seeds"
  );
  const detail = await M6ETorrentService.detail(tid);
  Assert.equal(detail.files.length, 3);

  await M6ETorrentService.remove(tid, true);
  Assert.equal(await statusOf(tid), null, "removed");
  Assert.ok(
    !(await IOUtils.exists(PathUtils.join(save, swarm.name, "a.txt"))),
    "the files were deleted"
  );
  await waitFor(
    () => M6ETorrentService.getNetworkState().listening === null,
    "the closed event"
  );
});

helper_task(async function test_add_magnet_download_complete() {
  const swarm = await startSwarm({ name: "m6e-svc-magnet" });
  const save = await newFolder("save");
  const metadata = notification("m6e-torrent-metadata", d => d.tid == swarm.tid);
  const { tid } = await M6ETorrentService.addMagnet(swarm.magnet, {
    kind: "magnet-page",
    origin: "https://example.com",
  });
  Assert.equal(tid, swarm.tid);
  const event = await metadata;
  Assert.equal(event.metadata.files.length, 3);
  Assert.equal(event.source.origin, "https://example.com");
  Assert.deepEqual(await IOUtils.getChildren(save), [], "nothing written yet");

  await sleep(2000);
  Assert.equal((await swarm.ask("stats")).requests, 0, "no payload before confirm");

  const completed = notification("m6e-torrent-completed", d => d.tid == tid);
  await M6ETorrentService.confirmAdd(tid, { savePath: save, start: true });
  await completed;
  await checkPayload(save, swarm);
});

helper_task(async function test_selective_files() {
  const swarm = await startSwarm({ name: "m6e-svc-select" });
  const save = await newFolder("save");
  const { tid } = await M6ETorrentService.addFile(swarm.torrent, null);
  const completed = notification("m6e-torrent-completed", d => d.tid == tid);
  await M6ETorrentService.confirmAdd(tid, {
    files: [
      { index: 0, priority: 2 },
      { index: 1, priority: 0 },
      { index: 2, priority: 0 },
    ],
    savePath: save,
    start: true,
  });
  await completed;
  await checkPayload(save, swarm, { skip: [1, 2] });
  const status = await waitForState(tid, DONE);
  Assert.greaterOrEqual(status.size, 40000, "the status counts the selected files");
  Assert.less(status.size, 70000, "and not the others");
});

helper_task(async function test_single_file_torrent() {
  const swarm = await startSwarm({ name: "m6e-svc-single.bin", kind: "single" });
  const save = await newFolder("save");
  const { tid, metadata } = await M6ETorrentService.addFile(swarm.torrent, null);
  Assert.equal(metadata.files.length, 1);
  const completed = notification("m6e-torrent-completed", d => d.tid == tid);
  await M6ETorrentService.confirmAdd(tid, { savePath: save, start: true });
  await completed;
  await checkPayload(save, swarm);
});

helper_task(async function test_pause_resume_remove() {
  const swarm = await startSwarm({ name: "m6e-svc-pause", stall: true });
  const save = await newFolder("save");
  const { tid } = await M6ETorrentService.addFile(swarm.torrent, null);
  await M6ETorrentService.confirmAdd(tid, { savePath: save, start: true });
  await waitForState(tid, ["downloading"]);
  Assert.ok(await M6ETorrentService.hasActiveTorrents());
  Assert.equal(M6ETorrentService.activeTorrentCount, 1);
  Assert.ok(!M6ETorrentService.testing.hasIdleTimer(), "busy: no idle stop");

  await M6ETorrentService.pause(tid);
  Assert.equal((await statusOf(tid)).state, "paused");
  Assert.equal(await M6ETorrentService.hasActiveTorrents(), false);
  Assert.ok(M6ETorrentService.testing.hasIdleTimer(), "idle stop is scheduled");

  await M6ETorrentService.resume(tid);
  await waitForState(tid, ["downloading", "checking", "queued"]);
  Assert.ok(!M6ETorrentService.testing.hasIdleTimer());

  await M6ETorrentService.pauseAll();
  Assert.equal((await statusOf(tid)).state, "paused");
  await M6ETorrentService.resumeAll();
  await waitForState(tid, ["downloading", "checking", "queued"]);

  const completed = notification("m6e-torrent-completed", d => d.tid == tid);
  await swarm.ask("serve");
  await completed;
  await checkPayload(save, swarm);

  await M6ETorrentService.remove(tid, false);
  Assert.equal(await statusOf(tid), null);
  Assert.ok(
    await IOUtils.exists(payloadPath(save, swarm, swarm.files[0])),
    "removing without deleting keeps the files"
  );
  await Assert.rejects(
    M6ETorrentService.pause(tid),
    e => e.code == "not-found",
    "the removed torrent is gone"
  );
});

helper_task(async function test_cancel_pending() {
  const swarm = await startSwarm({ name: "m6e-svc-cancel" });
  const save = await newFolder("save");
  const { tid } = await M6ETorrentService.addFile(swarm.torrent, null);
  await M6ETorrentService.cancelAdd(tid);
  await rejectsWithCode(
    M6ETorrentService.confirmAdd(tid, {
      files: [{ index: 0, priority: 2 }],
      savePath: save,
      start: true,
    }),
    ["not-found"],
    "confirming a cancelled torrent"
  );
  Assert.deepEqual(await IOUtils.getChildren(save), []);
  Assert.equal((await swarm.ask("stats")).requests, 0);
});

helper_task(async function test_helper_crash_restart_restore() {
  const swarm = await startSwarm({ name: "m6e-svc-crash", stall: true });
  const save = await newFolder("save");
  const { tid } = await M6ETorrentService.addFile(swarm.torrent, null);
  await M6ETorrentService.confirmAdd(tid, { savePath: save, start: true });
  await waitForState(tid, ["downloading"]);
  const oldPid = M6ETorrentService.testing.helperPid();
  Assert.ok(oldPid > 0, "the helper pid is known");
  Assert.ok(await isProcessAlive(oldPid));

  const exited = notification("m6e-torrent-error", d => d.code == "helper-exited");
  await killHard(oldPid);
  await exited;
  Assert.ok(!(await isProcessAlive(oldPid)), "the old helper is gone");

  await waitFor(
    () =>
      M6ETorrentService.testing.isHelperRunning() &&
      M6ETorrentService.testing.helperPid() !== oldPid,
    "the helper to restart"
  );
  const status = await waitForState(tid, ACTIVE);
  Assert.equal(status.tid, tid, "the torrent is restored and active");

  const completed = notification("m6e-torrent-completed", d => d.tid == tid);
  await swarm.ask("serve");
  await completed;
  await checkPayload(save, swarm);
});

helper_task(async function test_resume_at_startup() {
  const swarm = await startSwarm({ name: "m6e-svc-startup", stall: true });
  const save = await newFolder("save");
  const { tid } = await M6ETorrentService.addFile(swarm.torrent, null);
  await M6ETorrentService.confirmAdd(tid, { savePath: save, start: true });
  await waitForState(tid, ["downloading"]);
  await M6ETorrentService.shutdown();
  Assert.ok(!M6ETorrentService.testing.isHelperRunning());

  await M6ETorrentService.testing.reset();
  M6ETorrentService.testing.setTimings({ notifyIntervalMs: 200 });
  Services.prefs.setBoolPref(PREFIX + "resumeAtStartup", false);
  await M6ETorrentService.init();
  Assert.ok(
    M6ETorrentService.testing.isHelperRunning(),
    "a saved torrent starts the helper at startup"
  );
  const { torrents } = await M6ETorrentService.list();
  Assert.equal(torrents.length, 1);
  Assert.equal(torrents[0].state, "paused", "resumeAtStartup is off");

  const completed = notification("m6e-torrent-completed", d => d.tid == tid);
  await swarm.ask("serve");
  await M6ETorrentService.resume(tid);
  await completed;
  await checkPayload(save, swarm);
});

helper_task(async function test_idle_stop_after_timeout() {
  M6ETorrentService.testing.setTimings({ idleStopMs: 1500 });
  const swarm = await startSwarm({ name: "m6e-svc-idle" });
  const { tid } = await M6ETorrentService.addFile(swarm.torrent, null);
  const pid = M6ETorrentService.testing.helperPid();
  Assert.ok(M6ETorrentService.testing.isHelperRunning());
  await sleep(2500);
  Assert.ok(
    M6ETorrentService.testing.isHelperRunning(),
    "a pending torrent keeps the helper"
  );
  await M6ETorrentService.cancelAdd(tid);
  await waitFor(
    () => !M6ETorrentService.testing.isHelperRunning(),
    "the idle stop",
    20_000
  );
  await waitFor(async () => !(await isProcessAlive(pid)), "the process to end");
  Assert.deepEqual(
    errors.filter(e => e.code == "helper-exited"),
    [],
    "an idle stop is not an error"
  );
  Assert.equal(M6ETorrentService.getNetworkState().listening, null);

  await M6ETorrentService.addFile(swarm.torrent, null);
  Assert.ok(M6ETorrentService.testing.isHelperRunning(), "the next need restarts it");
  Assert.notEqual(M6ETorrentService.testing.helperPid(), pid);
});

helper_task(async function test_helper_idle_exit_is_normal() {
  Services.prefs.setIntPref(PREFIX + "helperIdleSeconds", 2);
  const swarm = await startSwarm({ name: "m6e-svc-helperidle" });
  const { tid } = await M6ETorrentService.addFile(swarm.torrent, null);
  const pid = M6ETorrentService.testing.helperPid();
  await M6ETorrentService.cancelAdd(tid);
  await waitFor(
    () => !M6ETorrentService.testing.isHelperRunning(),
    "the helper's own idle exit",
    30_000
  );
  Assert.ok(!(await isProcessAlive(pid)));
  Assert.deepEqual(
    errors.filter(e => e.code == "helper-exited"),
    [],
    "the helper's idle exit with nothing active is not an error"
  );
  const again = await M6ETorrentService.addFile(swarm.torrent, null);
  Assert.equal(again.tid, swarm.tid, "the service starts it again");
});

helper_task(async function test_hostile_torrents_are_rejected() {
  const swarm = await startSwarm({ name: "m6e-svc-hostile", hostile: true });
  const save = await newFolder("save");
  const expected = name => {
    if (name.startsWith("hostile-")) {
      return ["unsafe-path"];
    }
    return name == "oversized" ? ["too-large"] : ["invalid-torrent"];
  };
  const entries = Object.entries(swarm.hostile);
  Assert.greaterOrEqual(entries.length, 18, "all hostile files exist");
  // Only the file with bad piece data passes the service's own checks; the
  // helper refuses it, so it goes last.
  const lastName = "malformed-bad-pieces";
  const lastPath = swarm.hostile[lastName];
  for (const [name, path] of entries) {
    if (name == lastName) {
      continue;
    }
    await rejectsWithCode(
      M6ETorrentService.addFile(path, null),
      expected(name),
      `service: ${name}`
    );
    Assert.ok(
      !M6ETorrentService.testing.isHelperRunning(),
      `${name} is refused before the helper is involved`
    );
  }
  await rejectsWithCode(
    M6ETorrentService.addFile(lastPath, null),
    expected(lastName),
    `helper: ${lastName}`
  );
  Assert.deepEqual(await inboxFiles(), [], "nothing was staged");

  for (const [name, path] of entries) {
    await rejectsWithCode(
      M6ETorrentService.testing.callHelper("add", { file: path }),
      name == "oversized" || name == "too-deep"
        ? ["too-large", "invalid-torrent"]
        : expected(name),
      `helper: ${name}`
    );
  }
  const { torrents } = await M6ETorrentService.list();
  Assert.deepEqual(torrents, [], "no torrent was added");
  Assert.deepEqual(await IOUtils.getChildren(save), [], "nothing was written");
});

helper_task(async function test_bad_arguments_are_rejected() {
  await rejectsWithCode(
    M6ETorrentService.addFile("relative.torrent", null),
    ["bad-request"],
    "relative path"
  );
  await rejectsWithCode(
    M6ETorrentService.addFile(PathUtils.join(torrentsDir(), "missing.torrent"), null),
    ["io-error"],
    "missing file"
  );
  await rejectsWithCode(
    M6ETorrentService.addMagnet("magnet:?dn=nothing", null),
    ["invalid-torrent", "bad-request"],
    "magnet without a topic"
  );
  await rejectsWithCode(
    M6ETorrentService.pause("not-a-tid"),
    ["bad-request"],
    "bad tid"
  );
  Assert.ok(!M6ETorrentService.testing.isHelperRunning());
});

// The tests below use fake_helper.py, which answers like the real helper but
// moves no data, so that the service logic that depends on what the helper
// says (unsupported settings, events, command line) is deterministic.

async function useFakeHelper() {
  const fake = do_get_file("fake_helper.py").path;
  await IOUtils.setPermissions(fake, 0o755);
  Services.prefs.setStringPref(PREFIX + "helperPath", fake);
  await IOUtils.makeDirectory(torrentsDir(), {
    createAncestors: true,
    ignoreExisting: true,
  });
}

function fakeSettings(settings) {
  return IOUtils.writeJSON(PathUtils.join(torrentsDir(), "fake.json"), settings);
}

async function fakeLog(name) {
  const text = await IOUtils.readUTF8(PathUtils.join(torrentsDir(), name)).catch(
    () => ""
  );
  return text
    .split("\n")
    .filter(Boolean)
    .map(line => JSON.parse(line));
}

function fakeEmit(...events) {
  return IOUtils.writeUTF8(
    PathUtils.join(torrentsDir(), "emit.jsonl"),
    events.map(e => JSON.stringify(e)).join("\n") + "\n"
  );
}

const SYNTHETIC_MAGNET =
  "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567";

helper_task(async function test_unsupported_settings_fail_closed() {
  await useFakeHelper();
  await fakeSettings({ unsupported: ["bindInterface"] });
  const failure = await rejectsWithCode(
    M6ETorrentService.addMagnet(SYNTHETIC_MAGNET, null),
    ["config-failed"],
    "start with a bind interface the helper cannot apply"
  );
  Assert.ok(failure.message.includes("bindInterface"), failure.message);
  Assert.ok(
    !M6ETorrentService.testing.isHelperRunning(),
    "the helper is not left running without the setting"
  );

  await fakeSettings({ unsupported: ["ipFilterPath", "proxy"] });
  const filter = PathUtils.join(await newFolder("filter"), "block.p2p");
  await IOUtils.writeUTF8(filter, "# synthetic\n");
  Services.prefs.setStringPref(PREFIX + "bindInterface", "");
  Services.prefs.setStringPref(PREFIX + "ipFilterPath", filter);
  await rejectsWithCode(
    M6ETorrentService.addMagnet(SYNTHETIC_MAGNET, null),
    ["config-failed"],
    "start with an IP filter the helper cannot apply"
  );
  Services.prefs.setStringPref(PREFIX + "ipFilterPath", "");
  Services.prefs.setStringPref(PREFIX + "proxy.type", "socks5");
  Services.prefs.setStringPref(PREFIX + "proxy.host", "127.0.0.1");
  await rejectsWithCode(
    M6ETorrentService.addMagnet(SYNTHETIC_MAGNET, null),
    ["config-failed"],
    "start with a proxy the helper cannot apply"
  );

  Services.prefs.setStringPref(PREFIX + "proxy.type", "none");
  await fakeSettings({});
  await M6ETorrentService.addMagnet(SYNTHETIC_MAGNET, null);
  Assert.ok(M6ETorrentService.testing.isHelperRunning());
  await fakeSettings({ unsupported: ["bindInterface"] });
  await rejectsWithCode(
    M6ETorrentService.setConfig({ bindInterface: "en0" }),
    ["config-failed"],
    "a setting changed while the helper runs"
  );
  Assert.ok(
    !M6ETorrentService.testing.isHelperRunning(),
    "the helper is stopped when it cannot apply a setting"
  );
});

helper_task(async function test_unsupported_goal_is_reported_not_silent() {
  await useFakeHelper();
  await fakeSettings({ unsupported: ["seedGoal"] });
  Services.prefs.setIntPref(PREFIX + "seedRatioPercent", 150);
  await M6ETorrentService.addMagnet(SYNTHETIC_MAGNET, null);
  Assert.ok(M6ETorrentService.testing.isHelperRunning(), "not fatal");
  const reported = errors.filter(e => e.code == "unsupported-setting");
  Assert.equal(reported.length, 1, "the unsupported seed goal is reported once");
  Assert.ok(reported[0].message.includes("seedGoal"));
});

helper_task(async function test_ip_filter_is_staged_in_the_inbox() {
  await useFakeHelper();
  const filter = PathUtils.join(await newFolder("filter"), "block.p2p");
  await IOUtils.writeUTF8(filter, "# synthetic\n");
  Services.prefs.setStringPref(PREFIX + "ipFilterPath", filter);
  await M6ETorrentService.addMagnet(SYNTHETIC_MAGNET, null);
  const configs = (await fakeLog("requests.jsonl")).filter(
    r => r.op == "setConfig" && "ipFilterPath" in r.args
  );
  Assert.equal(configs.length, 1);
  const staged = configs[0].args.ipFilterPath;
  Assert.notEqual(staged, filter);
  Assert.ok(staged.startsWith(inboxDir() + "/"), `staged in the inbox: ${staged}`);
  Assert.ok(staged.endsWith("block.p2p"), "the file name is kept");
  Assert.deepEqual(await inboxFiles(), [], "deleted after the reply");
});

helper_task(async function test_helper_events() {
  await useFakeHelper();
  const swarm = await startSwarm({ name: "m6e-svc-events" });
  const save = await newFolder("save");
  const { tid } = await M6ETorrentService.addFile(swarm.torrent, null);
  const staged = (await fakeLog("requests.jsonl")).find(r => r.op == "add");
  Assert.ok(staged.args.file.startsWith(inboxDir() + "/"), "staged for the helper");
  Assert.notEqual(staged.args.file, swarm.torrent);
  await M6ETorrentService.confirmAdd(tid, { savePath: save, start: true });

  const trackers = notification("m6e-torrent-trackers", d => d.tid == tid);
  await fakeEmit({
    event: "trackers",
    tid,
    trackers: [
      { url: swarm.tracker, status: "working", seeds: 1, peers: 0, message: "" },
    ],
  });
  Assert.equal((await trackers).trackers[0].url, swarm.tracker);

  await fakeEmit({ event: "peers", tid, seeds: 3, peers: 5 });
  await waitFor(
    () =>
      M6ETorrentService.testing
        .cachedTorrents()
        .some(t => t.tid == tid && t.seeds == 3 && t.peers == 5),
    "the peers event to update the status"
  );

  await fakeEmit({ event: "listening", port: 4242 });
  await waitFor(
    () => M6ETorrentService.getNetworkState().listening === 4242,
    "the listening event"
  );
  await fakeEmit({ event: "closed" });
  await waitFor(
    () => M6ETorrentService.getNetworkState().listening === null,
    "the closed event"
  );

  const completed = notification("m6e-torrent-completed", d => d.tid == tid);
  await fakeEmit({
    event: "completed",
    tid,
    warning: "could not set the quarantine attribute",
  });
  const done = await completed;
  Assert.equal(done.warning, "could not set the quarantine attribute");
});

helper_task(async function test_sandbox_arguments_and_folder_restart() {
  await useFakeHelper();
  Services.prefs.setBoolPref(PREFIX + "sandbox", true);
  const swarm = await startSwarm({ name: "m6e-svc-sandbox" });
  const save = await newFolder("outside");
  const { tid } = await M6ETorrentService.addFile(swarm.torrent, null);

  let starts = await fakeLog("starts.jsonl");
  Assert.equal(starts.length, 1);
  const args = starts[0].args;
  Assert.ok(args.includes("--sandbox"));
  Assert.equal(args[args.indexOf("--inbox-dir") + 1], inboxDir());
  const folders = args.flatMap((a, i) => (a == "--download-dir" ? [args[i + 1]] : []));
  Assert.deepEqual(
    folders,
    await M6ETorrentService.testing.allowedFolders(),
    "the default download folder is passed"
  );
  Assert.ok(!folders.includes(save));

  await M6ETorrentService.confirmAdd(tid, { savePath: save, start: true });
  starts = await fakeLog("starts.jsonl");
  Assert.equal(starts.length, 2, "a folder outside the set restarts the helper");
  const second = starts[1].args;
  Assert.ok(
    second.some((a, i) => a == "--download-dir" && second[i + 1] == save),
    "the new folder is passed at the restart"
  );
  const status = await waitForState(tid, ["downloading"]);
  Assert.equal(status.savePath, save);
  Assert.deepEqual(
    JSON.parse(Services.prefs.getStringPref(PREFIX + "sandboxFolders")),
    [save],
    "the folder is remembered"
  );
});

helper_task(async function test_no_sandbox_arguments_by_default() {
  await useFakeHelper();
  await M6ETorrentService.addMagnet(SYNTHETIC_MAGNET, null);
  const [start] = await fakeLog("starts.jsonl");
  Assert.deepEqual(start.args.slice(0, 2), ["--protocol", "1"]);
  Assert.ok(!start.args.includes("--sandbox"));
  Assert.ok(!start.args.includes("--download-dir"));
});

helper_task(async function test_sandbox_restart_keeps_a_magnet() {
  await useFakeHelper();
  Services.prefs.setBoolPref(PREFIX + "sandbox", true);
  const save = await newFolder("outside");
  const metadata = notification("m6e-torrent-metadata");
  const { tid } = await M6ETorrentService.addMagnet(SYNTHETIC_MAGNET, null);
  await metadata;
  const pid = M6ETorrentService.testing.helperPid();
  await M6ETorrentService.confirmAdd(tid, { savePath: save, start: true });
  Assert.notEqual(
    M6ETorrentService.testing.helperPid(),
    pid,
    "the helper was restarted for the new folder"
  );
  Assert.equal((await statusOf(tid)).savePath, save, "the magnet was added again");
});
