#!/usr/bin/env python3
"""Privacy and settings tests for the m6e-torrent helper (spec 8.4, 8.5;
PROTOCOL.md, "Additions (privacy)").

    python3 -I tools/m6e/torrent/tests/privacy/privacy_tests.py --helper <binary>

Only synthetic fixtures on 127.0.0.1 and ::1 (spec 8.9, D38): a local
tracker, seed, SOCKS5 server, relay and web seed. No real torrent, magnet
link, tracker or DHT node appears here. Output is TAP; the exit status is 1
when any test fails.
"""

import argparse
import hashlib
import json
import os
import shutil
import socket
import sys
import tempfile
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(HERE)
TORRENT_DIR = os.path.dirname(TESTS)
sys.path.insert(0, TESTS)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(TORRENT_DIR, "fixtures"))

import bencode  # noqa: E402
import make_torrent  # noqa: E402
import peers  # noqa: E402
import run_tests  # noqa: E402
import socks5  # noqa: E402
import v2torrent  # noqa: E402

Helper = run_tests.Helper
Skip = run_tests.Skip
Timeout = run_tests.Timeout
confirm_args = run_tests.confirm_args
add_file = run_tests.add_file
wait_done = run_tests.wait_done
verify_output = run_tests.verify_output
state_of = run_tests.state_of
wait_until = run_tests.wait_until

CMD = []
REGISTRY = []
FORBIDDEN = [b"libtorrent", b"rasterbar", b"2.1.2", b"lt2120", b"m6e"]
ABSENT_INTERFACE = "m6eabsent0"


def test(fn):
    REGISTRY.append(fn)
    return fn


class Ctx:
    def __init__(self, root, name):
        self.dir = os.path.join(root, name)
        os.makedirs(self.dir)
        self.helpers = []
        self.closers = []
        self.notes = []

    def folder(self, name):
        path = os.path.join(self.dir, name)
        os.makedirs(path, exist_ok=True)
        return path

    def start(self, name="a", env=None, bind="lo0"):
        h = Helper(
            CMD,
            os.path.join(self.dir, name, "profile"),
            env,
            os.path.join(self.dir, name + "-stderr.log"),
        )
        self.helpers.append(h)
        h.wait_ready()
        if bind:
            h.call("setConfig", {"bindInterface": bind})
        return h

    def close_later(self, *objs):
        for o in objs:
            self.closers.append(o.close)
        return objs[0] if len(objs) == 1 else objs

    def write(self, name, data):
        path = os.path.join(self.folder("in"), name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def note(self, text):
        self.notes.append(text)

    def finish(self):
        for h in self.helpers:
            h.stop()
        for c in self.closers:
            try:
                c()
            except Exception:
                pass
        for h in self.helpers:
            if h.violations:
                raise AssertionError("protocol violation on stdout: %s" % h.violations[0])


def socket_list(pid):
    out = []
    for line in run_tests.sockets(pid):
        parts = line.split()
        if len(parts) < 9:
            continue
        proto = parts[7]
        name = " ".join(parts[8:])
        out.append((proto, name))
    return out


def tcp_peer_ports(pid):
    ports = set()
    for proto, name in socket_list(pid):
        if proto == "TCP" and "->" in name:
            remote = name.split("->", 1)[1].split()[0]
            ports.add(int(remote.rsplit(":", 1)[1]))
    return ports


def fixture(name, size=200000, announce=None):
    return make_torrent.single_file(announce=announce, name=name, size=size)


def swarm_for(ctx, fx, **seed_args):
    tracker = ctx.close_later(peers.Tracker())
    seed = ctx.close_later(peers.RecordingSeed([fx], **seed_args))
    tracker.peers.append(("127.0.0.1", seed.port))
    return tracker, seed


def torrent_with(ctx, fx, announce=None, extra=None, name=None):
    top = {"info": fx.info, "created by": "m6e-test"}
    if announce:
        top["announce"] = announce
    if extra:
        top.update(extra)
    return ctx.write(name or (fx.name + ".torrent"), bencode.encode(top))


def start_download(h, path, save, **kw):
    added = add_file(h, path)
    tid = added["tid"]
    h.call("confirm", dict(confirm_args(added["metadata"], save, **kw), tid=tid))
    return tid


def start_seeder(ctx, fx, name="seeder", config=None, limits=None):
    h = ctx.start(name)
    if config:
        h.call("setConfig", config)
    if limits:
        h.call("setLimits", limits)
    save = ctx.folder(name + "-save")
    fx.write_content(save)
    if isinstance(fx, v2torrent.V2Fixture):
        data = fx.torrent_bytes()
    else:
        data = bencode.encode({"info": fx.info})
    path = ctx.write(name + ".torrent", data)
    since = h.mark()
    tid = start_download(h, path, save)
    port = h.wait_event("listening", timeout=15, since=since)["port"]
    seeding = wait_until(lambda: (state_of(h, tid) or {}).get("state") == "seeding", 20)
    assert seeding, "the seeder is not seeding: %r" % state_of(h, tid)
    return h, tid, port


def downloaded(h, tid):
    st = state_of(h, tid)
    return st["downloaded"] if st else 0


def assert_neutral(label, data):
    low = data.lower()
    for word in FORBIDDEN:
        assert word not in low, "%s contains %r: %r" % (label, word, data[:200])


def ext_handshake(stream):
    pos = 68
    while pos + 5 <= len(stream):
        length = int.from_bytes(stream[pos : pos + 4], "big")
        if length and stream[pos + 4] == 20 and stream[pos + 5] == 0:
            return bencode.decode(stream[pos + 6 : pos + 4 + length])
        pos += 4 + length
    return None


# (8) Identity -------------------------------------------------------------


@test
def identity_is_neutral_on_the_wire(ctx):
    fx = fixture("neutral-identity.bin", 60000)
    tracker, seed = swarm_for(ctx, fx)
    h = ctx.start()
    h.call("setConfig", {"encryption": "disabled"})
    tid = start_download(h, torrent_with(ctx, fx, tracker.url), ctx.folder("save"))
    wait_done(h, tid, 0, 40)
    announces = tracker.snapshot()
    assert announces, "no tracker announce"
    for a in announces:
        assert_neutral("tracker User-Agent", (a["user_agent"] or "").encode())
        assert_neutral("tracker query", a["query"].encode())
        assert a["params"]["peer_id"].startswith(b"-LT0000-"), a["params"]["peer_id"]
    streams = [bytes(r) for r in seed.received if r.startswith(b"\x13BitTorrent protocol")]
    assert streams, "no plaintext handshake reached the seed"
    for s in streams:
        assert_neutral("peer handshake", s)
        assert s[48:56] == b"-LT0000-", s[48:68]
        ext = ext_handshake(s)
        assert ext is not None, "no extension handshake"
        assert b"v" not in ext, "the extension handshake names the client: %r" % ext.get(b"v")
    ctx.note("tracker User-Agent: %r" % announces[0]["user_agent"])


@test
def user_agent_setting_is_sent(ctx):
    fx = fixture("agent-setting.bin", 40000)
    tracker, seed = swarm_for(ctx, fx)
    h = ctx.start()
    r = h.call("setConfig", {"encryption": "disabled", "userAgent": "TestAgent/9"})
    assert "userAgent" in r["applied"]
    tid = start_download(h, torrent_with(ctx, fx, tracker.url), ctx.folder("save"))
    wait_done(h, tid, 0, 40)
    assert {a["user_agent"] for a in tracker.snapshot()} == {"TestAgent/9"}
    streams = [bytes(r) for r in seed.received if r.startswith(b"\x13BitTorrent protocol")]
    assert any((ext_handshake(s) or {}).get(b"v") == b"TestAgent/9" for s in streams)


# (1) Encryption -----------------------------------------------------------


@test
def encryption_forced_refuses_a_plaintext_peer(ctx):
    fx = fixture("enc-forced.bin", 80000)
    tracker, seed = swarm_for(ctx, fx)
    h = ctx.start()
    assert h.call("setConfig", {"encryption": "forced"})["applied"] == ["encryption"]
    tid = start_download(h, torrent_with(ctx, fx, tracker.url), ctx.folder("save"))
    assert wait_until(lambda: seed.accept_count() >= 1, 20), "the helper never tried the seed"
    time.sleep(4)
    assert seed.handshakes == [], "a plaintext handshake was sent in forced mode"
    assert seed.bytes_served == 0 and downloaded(h, tid) == 0
    assert all(not bytes(r).startswith(b"\x13BitTorrent") for r in seed.received)


@test
def encryption_disabled_speaks_plaintext(ctx):
    fx = fixture("enc-disabled.bin", 80000)
    tracker, seed = swarm_for(ctx, fx)
    h = ctx.start()
    h.call("setConfig", {"encryption": "disabled"})
    tid = start_download(h, torrent_with(ctx, fx, tracker.url), ctx.folder("save"))
    wait_done(h, tid, 0, 40)
    verify_output(fx, ctx.folder("save"))
    firsts = [bytes(r[:20]) for r in seed.received if r]
    assert firsts and all(f == b"\x13BitTorrent protocol" for f in firsts), firsts


def relay_pair(ctx, seeder_enc, leecher_enc, size=300000, seeder_up=0):
    fx = fixture("enc-pair-%s-%s.bin" % (seeder_enc, leecher_enc), size)
    seeder, _, port = start_seeder(
        ctx, fx, config={"encryption": seeder_enc}, limits={"upKBps": seeder_up}
    )
    relay = ctx.close_later(peers.Relay(port))
    tracker = ctx.close_later(peers.Tracker([("127.0.0.1", relay.port)]))
    leecher = ctx.start("leecher")
    leecher.call("setConfig", {"encryption": leecher_enc})
    save = ctx.folder("leecher-save")
    tid = start_download(leecher, torrent_with(ctx, fx, tracker.url), save)
    return fx, seeder, leecher, tid, relay, save


def transferring_peers_encrypted(leecher, tid, fx, save):
    seen = []
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        d = leecher.call("detail", {"tid": tid})
        seen += [p["encrypted"] for p in d["peers"] if p["downKBps"] > 0]
        if d["status"]["state"] in ("seeding", "completed"):
            break
        time.sleep(0.3)
    wait_done(leecher, tid, 0, 10)
    verify_output(fx, save)
    assert seen and all(seen), "a transferring peer was not encrypted: %r" % seen


@test
def encryption_enabled_reaches_a_forced_peer_encrypted(ctx):
    fx, seeder, leecher, tid, relay, save = relay_pair(ctx, "forced", "enabled", seeder_up=100)
    transferring_peers_encrypted(leecher, tid, fx, save)


@test
def encryption_forced_is_encrypted_on_the_wire(ctx):
    fx, seeder, leecher, tid, relay, save = relay_pair(ctx, "forced", "forced", seeder_up=100)
    seen = []
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        d = leecher.call("detail", {"tid": tid})
        seen += [p["encrypted"] for p in d["peers"] if p["downKBps"] > 0]
        if d["status"]["state"] in ("seeding", "completed"):
            break
        time.sleep(0.3)
    wait_done(leecher, tid, 0, 10)
    verify_output(fx, save)
    assert seen and all(seen), "a transferring peer was not encrypted: %r" % seen
    streams = relay.captured()
    assert any(len(back) > 100000 for _, back in streams), "no payload through the relay"
    for out, back in streams:
        for label, data in (("leecher to seeder", out), ("seeder to leecher", back)):
            assert b"BitTorrent protocol" not in data, "plaintext handshake %s: %r (%d bytes)" % (
                label,
                data[:40],
                len(data),
            )
        assert fx.payload[:4096] not in back, "payload in clear on the wire"


@test
def encryption_disabled_cannot_reach_a_forced_peer(ctx):
    fx, seeder, leecher, tid, relay, save = relay_pair(ctx, "forced", "disabled", 100000)
    assert wait_until(lambda: relay.captured(), 20), "the leecher never connected"
    time.sleep(4)
    assert downloaded(leecher, tid) == 0
    for out, back in relay.captured():
        assert out.startswith(b"\x13BitTorrent protocol"), out[:20]
        assert len(back) == 0, "the forced peer answered a plaintext handshake"


# (2) Rate limits ----------------------------------------------------------


def timed_download(ctx, h, fx, name):
    tracker, seed = swarm_for(ctx, fx)
    save = ctx.folder(name)
    tid = start_download(h, torrent_with(ctx, fx, tracker.url, name=name + ".torrent"), save)
    assert wait_until(lambda: seed.bytes_served > 0, 30), "no data was served"
    t0 = time.monotonic()
    wait_done(h, tid, 0, 90)
    verify_output(fx, save)
    return tid, time.monotonic() - t0


@test
def rate_limit_global_download_covers_loopback(ctx):
    h = ctx.start()
    _, fast = timed_download(ctx, h, fixture("rate-free.bin", 320000), "free")
    assert h.call("setLimits", {"downKBps": 40}) == {}
    _, slow = timed_download(ctx, h, fixture("rate-down.bin", 320000), "limited")
    ctx.note("320 KB unlimited %.1fs, at 40 KB/s %.1fs" % (fast, slow))
    assert fast < 3, "the unlimited control took %.1fs" % fast
    assert slow >= 5, "the download limit was not applied (%.1fs)" % slow


@test
def rate_limit_global_upload(ctx):
    fx = fixture("rate-up.bin", 320000)
    seeder, _, port = start_seeder(ctx, fx, limits={"upKBps": 40})
    tracker = ctx.close_later(peers.Tracker([("127.0.0.1", port)]))
    leecher = ctx.start("leecher")
    save = ctx.folder("leecher-save")
    tid = start_download(leecher, torrent_with(ctx, fx, tracker.url), save)
    assert wait_until(lambda: downloaded(leecher, tid) > 0, 30), "no data arrived"
    t0 = time.monotonic()
    wait_done(leecher, tid, 0, 90)
    elapsed = time.monotonic() - t0
    verify_output(fx, save)
    ctx.note("320 KB at a 40 KB/s upload limit: %.1fs" % elapsed)
    assert elapsed >= 5, "the upload limit was not applied (%.1fs)" % elapsed


@test
def rate_limit_turtle_mode(ctx):
    fx = fixture("turtle.bin", 400000)
    tracker, seed = swarm_for(ctx, fx)
    h = ctx.start()
    h.call("setLimits", {"downKBps": 0, "turtle": {"enabled": True, "downKBps": 25, "upKBps": 10}})
    save = ctx.folder("save")
    tid = start_download(h, torrent_with(ctx, fx, tracker.url), save)
    assert wait_until(lambda: seed.bytes_served > 0, 30)
    time.sleep(4)
    part = downloaded(h, tid)
    assert part < 0.5 * len(fx.payload), "Turtle mode did not slow the download (%d bytes)" % part
    h.call("setLimits", {"turtle": {"enabled": False}})
    t0 = time.monotonic()
    wait_done(h, tid, 0, 30)
    ctx.note("after Turtle mode off the rest took %.1fs" % (time.monotonic() - t0))
    assert time.monotonic() - t0 < 4
    verify_output(fx, save)


@test
def rate_limit_per_torrent(ctx):
    fx = fixture("per-torrent.bin", 320000)
    tracker, seed = swarm_for(ctx, fx)
    h = ctx.start()
    save = ctx.folder("save")
    tid = start_download(h, torrent_with(ctx, fx, tracker.url), save)
    st = h.call("setTorrentLimits", {"tid": tid, "downKBps": 40})["status"]
    assert st["downLimitKBps"] == 40 and st["upLimitKBps"] == 0, st
    assert wait_until(lambda: seed.bytes_served > 0, 30)
    t0 = time.monotonic()
    wait_done(h, tid, 0, 90)
    elapsed = time.monotonic() - t0
    ctx.note("320 KB at a 40 KB/s torrent limit: %.1fs" % elapsed)
    assert elapsed >= 5, "the torrent limit was not applied (%.1fs)" % elapsed
    h.err("setTorrentLimits", {"tid": "0" * 40, "downKBps": 1}, {"not-found"})
    h.err("setTorrentLimits", {"tid": tid, "downKBps": -1}, {"bad-request"})
    h.call("shutdown")
    h2 = ctx.start("a")
    restored = h2.call("restore", {"start": False})["torrents"]
    assert restored[0]["downLimitKBps"] == 40, restored[0]


# (3) Queue and seeding goals ----------------------------------------------


@test
def queue_max_active_downloads(ctx):
    h = ctx.start()
    h.call("setLimits", {"maxActiveDownloads": 1, "maxActiveTorrents": 5})
    swarms = []
    tids = []
    for n in range(2):
        fx = fixture("queue-%d.bin" % n, 900000)
        tracker, seed = swarm_for(ctx, fx, block_delay=0.3)
        swarms.append((fx, seed))
        path = torrent_with(ctx, fx, tracker.url)
        tids.append(start_download(h, path, ctx.folder("save%d" % n)))
    assert wait_until(lambda: any(s.request_count() for _, s in swarms), 30), "nothing started"
    time.sleep(3)
    states = [state_of(h, t)["state"] for t in tids]
    active = [i for i, s in enumerate(states) if s == "downloading"]
    assert len(active) == 1 and states[1 - active[0]] == "queued", states
    waiting = 1 - active[0]
    assert swarms[waiting][1].request_count() == 0, "a queued torrent requested data"
    h.call("pause", {"tid": tids[active[0]]})
    started = wait_until(lambda: state_of(h, tids[waiting])["state"] == "downloading", 15)
    assert started, state_of(h, tids[waiting])
    assert wait_until(lambda: swarms[waiting][1].request_count() > 0, 20)
    h.call("setLimits", {"maxActiveDownloads": 0})
    h.call("resume", {"tid": tids[active[0]]})
    assert wait_until(
        lambda: all(state_of(h, t)["state"] == "downloading" for t in tids), 15
    ), "0 did not mean unlimited: %r" % [state_of(h, t)["state"] for t in tids]


def goal_pair(ctx, goal, limits, size=600000):
    fx = fixture("goal-%s.bin" % goal.get("action", "stop"), size)
    seeder, stid, port = start_seeder(ctx, fx, config={"seedGoal": goal}, limits=limits)
    tracker = ctx.close_later(peers.Tracker([("127.0.0.1", port)]))
    leecher = ctx.start("leecher")
    ltid = start_download(leecher, torrent_with(ctx, fx, tracker.url), ctx.folder("leecher-save"))
    return fx, seeder, stid, leecher, ltid


@test
def seed_goal_ratio_stops_seeding(ctx):
    fx, seeder, stid, leecher, ltid = goal_pair(ctx, {"ratio": 0.3}, {"upKBps": 60})
    e = seeder.wait_event("goalReached", lambda e: e["tid"] == stid, 40)
    assert e["action"] == "stop"
    st = state_of(seeder, stid)
    assert st["state"] == "completed" and st["goalReached"], st
    assert 0.3 <= st["ratio"] < 0.8, st["ratio"]
    time.sleep(1)
    before = downloaded(leecher, ltid)
    time.sleep(3)
    assert downloaded(leecher, ltid) == before < len(fx.payload), "the seeder kept uploading"
    seeder.call("pauseAll")
    seeder.call("resumeAll")
    assert state_of(seeder, stid)["state"] == "completed", "resumeAll restarted a finished torrent"
    seeder.call("shutdown")
    again = ctx.start("seeder")
    restored = again.call("restore", {"start": True})["torrents"]
    assert restored[0]["state"] == "completed", restored[0]["state"]


@test
def seed_goal_ratio_switches_to_the_alternative_upload_limit(ctx):
    fx, seeder, stid, leecher, ltid = goal_pair(
        ctx,
        {"ratio": 0.2, "action": "slow"},
        {"upKBps": 100, "turtle": {"enabled": False, "upKBps": 12}},
        1200000,
    )
    e = seeder.wait_event("goalReached", lambda e: e["tid"] == stid, 40)
    assert e["action"] == "slow"
    time.sleep(2)
    a = downloaded(leecher, ltid)
    time.sleep(5)
    b = downloaded(leecher, ltid)
    rate = (b - a) / 5 / 1024
    st = state_of(seeder, stid)
    ctx.note("upload after the goal: %.1f KB/s" % rate)
    assert st["state"] == "seeding" and st["goalReached"], st
    assert 0 < rate < 20, "the alternative upload limit was not applied (%.1f KB/s)" % rate


@test
def seed_goal_hours_stops_seeding(ctx):
    fx = fixture("goal-hours.bin", 50000)
    seeder, stid, _ = start_seeder(ctx, fx, config={"seedGoal": {"ratio": None, "hours": 0.0008}})
    e = seeder.wait_event("goalReached", lambda e: e["tid"] == stid, 15)
    st = state_of(seeder, stid)
    assert st["state"] == "completed", st
    seeder.err("setConfig", {"seedGoal": {"ratio": "high"}}, {"bad-request"})
    seeder.err("setConfig", {"seedGoal": {"hours": -1}}, {"bad-request"})
    seeder.err("setConfig", {"seedGoal": {"action": "explode"}}, {"bad-request"})


# (4) SOCKS5 proxy ---------------------------------------------------------


def proxied_swarm(ctx, name):
    fx = fixture(name, 200000)
    tracker = ctx.close_later(peers.Tracker())
    seed = ctx.close_later(peers.RecordingSeed([fx]))
    tracker.peers.append(("127.0.0.1", seed.port))
    utp = ctx.close_later(peers.Listener(udp=True, port=seed.port))
    udp_tracker = ctx.close_later(peers.Listener(udp=True))
    extra = {
        "announce-list": [
            [tracker.name_url],
            ["udp://127.0.0.1:%d/announce" % udp_tracker.port],
        ]
    }
    path = torrent_with(ctx, fx, tracker.name_url, extra)
    return fx, tracker, seed, utp, udp_tracker, path


def proxy_config(proxy, user="m6e-user", password="m6e-pass", peers_too=True):
    return {
        "type": "socks5",
        "host": "127.0.0.1",
        "port": proxy.port if proxy else 9,
        "username": user,
        "password": password,
        "peers": peers_too,
    }


@test
def proxy_carries_all_traffic(ctx):
    proxy = ctx.close_later(socks5.Socks5Server("m6e-user", "m6e-pass"))
    fx, tracker, seed, utp, udp_tracker, path = proxied_swarm(ctx, "proxied.bin")
    h = ctx.start()
    pid = h.pid
    r = h.call(
        "setConfig",
        {"dht": True, "lpd": True, "portMapping": True, "proxy": proxy_config(proxy)},
    )
    assert r["unsupported"] == [], r
    for k in ("dht", "lpd", "portMapping", "utp", "udpTrackers", "incoming"):
        assert k in r["forcedOff"], r["forcedOff"]
    time.sleep(0.5)
    assert socket_list(pid) == [], "sockets at idle with a proxy: %r" % socket_list(pid)
    assert proxy.requests == [], "the proxy was contacted at idle"
    since = h.mark()
    save = ctx.folder("save")
    tid = start_download(h, path, save)
    samples = []
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        samples.append(socket_list(pid))
        st = state_of(h, tid)
        if st and st["state"] in ("seeding", "completed"):
            break
        time.sleep(0.3)
    wait_done(h, tid, since, 30)
    verify_output(fx, save)
    via = proxy.upstream_ports
    assert proxy.auth and all(good for _, _, good in proxy.auth), proxy.auth
    names = [(c["atyp"], c["host"]) for c in proxy.connects()]
    assert (3, "localhost") in names, "the tracker name was not sent to the proxy: %r" % names
    assert tracker.count() > 0
    assert all(a["client_port"] in via for a in tracker.snapshot()), "a tracker bypassed the proxy"
    assert seed.accepted and all(p in via for p in seed.accepted), "a peer bypassed the proxy"
    assert utp.count() == 0, "uTP packets went to the peer directly"
    assert udp_tracker.count() == 0, "the UDP tracker was contacted"
    assert not h.events_of("listening", since), "a listening socket was opened"
    for sample in samples:
        for proto, name in sample:
            assert "LISTEN" not in name, "listening socket with a proxy: %s" % name
            if proto == "TCP" and "->" in name:
                assert name.split("->")[1].startswith("127.0.0.1:%d" % proxy.port), name
            if proto == "UDP":
                assert "->" not in name, "a UDP socket is connected: %s" % name
    trackers = {t["url"]: t for t in h.call("detail", {"tid": tid})["trackers"]}
    udp = [t for u, t in trackers.items() if u.startswith("udp://")]
    assert udp and udp[0]["status"] == "disabled", trackers
    udp_kinds = sorted({n for s in samples for p, n in s if p == "UDP"})
    ctx.note("UDP sockets seen: %r" % udp_kinds)
    ports = {int(n.rsplit(":", 1)[1]) for p, n in socket_list(pid) if p == "UDP"}
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.settimeout(1.5)
    ping = bencode.encode({"t": "aa", "y": "q", "q": "ping", "a": {"id": b"x" * 20}})
    syn = b"\x41\x00" + b"\x00" * 18
    for port in ports:
        for datagram in (ping, syn):
            probe.sendto(datagram, ("127.0.0.1", port))
            try:
                reply = probe.recvfrom(2048)
                raise AssertionError("the UDP socket answered a direct datagram: %r" % (reply,))
            except socket.timeout:
                pass
    probe.close()
    assert proxy.connects() and all(c["ok"] for c in proxy.connects())


@test
def proxy_carries_web_seeds(ctx):
    proxy = ctx.close_later(socks5.Socks5Server("m6e-user", "m6e-pass"))
    fx = fixture("proxied-web.bin", 120000)
    web = ctx.close_later(peers.WebSeed({"/" + fx.name: fx.payload}))
    h = ctx.start()
    h.call("setConfig", {"proxy": proxy_config(proxy)})
    save = ctx.folder("save")
    tid = start_download(h, torrent_with(ctx, fx, None, {"url-list": [web.name_base]}), save)
    wait_done(h, tid, 0, 40)
    verify_output(fx, save)
    requests = web.snapshot()
    via = proxy.upstream_ports
    assert requests, "the web seed was not used"
    assert all(r["client_port"] in via for r in requests), "a web seed bypassed the proxy"
    targets = [(c["atyp"], c["host"], c["port"]) for c in proxy.connects()]
    assert (3, "localhost", web.port) in targets, targets
    assert all(r["user_agent"] is None for r in requests), "web seed requests name the client"


@test
def proxy_fails_closed(ctx):
    proxy = ctx.close_later(socks5.Socks5Server("m6e-user", "m6e-pass"))
    fx, tracker, seed, utp, udp_tracker, path = proxied_swarm(ctx, "fail-closed.bin")
    h = ctx.start()
    h.call("setConfig", {"dht": True, "proxy": proxy_config(proxy, password="wrong")})
    tid = start_download(h, path, ctx.folder("save"))
    magnet = fixture("fail-closed-magnet.bin", 30000)
    seed.add(magnet)
    h.call("add", {"magnet": magnet.magnet(peers=["127.0.0.1:%d" % seed.port])})
    assert wait_until(lambda: proxy.auth, 15), "the proxy was not tried"
    time.sleep(5)
    assert all(not good for _, _, good in proxy.auth)
    assert tracker.count() == 0 and seed.accept_count() == 0, "a direct connection was made"
    assert utp.count() == 0 and udp_tracker.count() == 0
    dead = socket.socket()
    dead.bind(("127.0.0.1", 0))
    dead_port = dead.getsockname()[1]
    dead.close()
    h.call("setConfig", {"proxy": dict(proxy_config(None), port=dead_port)})
    h.call("pause", {"tid": tid})
    h.call("resume", {"tid": tid})
    time.sleep(6)
    assert tracker.count() == 0 and seed.accept_count() == 0, "a direct connection was made"
    assert downloaded(h, tid) == 0


@test
def proxy_for_trackers_only(ctx):
    proxy = ctx.close_later(socks5.Socks5Server("m6e-user", "m6e-pass"))
    fx, tracker, seed, utp, udp_tracker, path = proxied_swarm(ctx, "trackers-only.bin")
    h = ctx.start()
    r = h.call("setConfig", {"proxy": proxy_config(proxy, peers_too=False)})
    assert "incoming" not in r["forcedOff"] and "udpTrackers" in r["forcedOff"], r
    save = ctx.folder("save")
    tid = start_download(h, path, save)
    wait_done(h, tid, 0, 60)
    verify_output(fx, save)
    via = proxy.upstream_ports
    assert tracker.count() and all(a["client_port"] in via for a in tracker.snapshot())
    assert seed.accepted and not any(p in via for p in seed.accepted), "peers used the proxy"
    assert udp_tracker.count() == 0


# (5) Interface binding and kill switch -----------------------------------


@test
def kill_switch_closes_everything_while_the_interface_is_gone(ctx):
    gate = os.path.join(ctx.dir, "interface-present")
    open(gate, "w").close()
    fx = make_torrent.single_file(name="kill-switch.bin", size=600000)
    tracker, seed = swarm_for(ctx, fx, block_delay=0.2)
    h = ctx.start(env={"M6E_TORRENT_TEST_INTERFACE_GATE": gate})
    pid = h.pid
    save = ctx.folder("save")
    tid = start_download(h, torrent_with(ctx, fx, tracker.url), save)
    assert wait_until(lambda: seed.request_count() >= 3, 30), "the download did not start"
    since = h.mark()
    os.unlink(gate)
    down = h.wait_event("interfaceDown", timeout=3, since=since)
    assert down["interface"] == "lo0"
    closed = wait_until(lambda: socket_list(pid) == [], 3)
    assert closed, "sockets after the kill switch: %r" % socket_list(pid)
    announces, accepts = tracker.count(), seed.accept_count()
    time.sleep(1.5)
    requests = seed.request_count()
    h.call("pause", {"tid": tid})
    h.call("resume", {"tid": tid})
    magnet = make_torrent.single_file(name="kill-magnet.bin", size=30000)
    seed.add(magnet)
    h.call("add", {"magnet": magnet.magnet(peers=["127.0.0.1:%d" % seed.port])})
    for _ in range(10):
        time.sleep(0.5)
        open_now = socket_list(pid)
        assert open_now == [], "a socket opened while the interface was gone: %r" % open_now
    after = (tracker.count(), seed.accept_count(), seed.request_count())
    before = (announces, accepts, requests)
    assert after == before, "traffic while the interface was gone: %r then %r" % (before, after)
    assert not h.events_of("metadata", since)
    open(gate, "w").close()
    h.wait_event("interfaceUp", timeout=3, since=since)
    wait_done(h, tid, since, 60)
    verify_output(fx, save)
    h.wait_event("metadata", timeout=20, since=since)


@test
def kill_switch_with_a_missing_interface_from_the_start(ctx):
    try:
        socket.if_nametoindex(ABSENT_INTERFACE)
        raise Skip("an interface named %s exists" % ABSENT_INTERFACE)
    except OSError:
        pass
    fx = fixture("absent-interface.bin", 60000)
    tracker, seed = swarm_for(ctx, fx)
    h = ctx.start(bind=None)
    pid = h.pid
    r = h.call("setConfig", {"bindInterface": ABSENT_INTERFACE})
    assert r["applied"] == ["bindInterface"] and "ipv6" in r["forcedOff"], r
    assert h.wait_event("interfaceDown", timeout=2)["interface"] == ABSENT_INTERFACE
    save = ctx.folder("save")
    tid = start_download(h, torrent_with(ctx, fx, tracker.url), save)
    time.sleep(5)
    assert socket_list(pid) == [], socket_list(pid)
    assert tracker.count() == 0 and seed.accept_count() == 0, "a connection was made"
    since = h.mark()
    h.call("setConfig", {"bindInterface": "lo0"})
    h.wait_event("interfaceUp", timeout=3, since=since)
    wait_done(h, tid, since, 40)
    verify_output(fx, save)


@test
def platform_ip_bound_if_refuses_ipv6_sockets(ctx):
    ip_bound_if = 25
    index = socket.if_nametoindex("lo0")
    v4 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    v4.setsockopt(socket.IPPROTO_IP, ip_bound_if, index.to_bytes(4, sys.byteorder))
    v4.close()
    v6 = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    try:
        v6.setsockopt(socket.IPPROTO_IP, ip_bound_if, index.to_bytes(4, sys.byteorder))
        raise AssertionError(
            "IP_BOUND_IF now works on IPv6 sockets: revisit the IPv6 block (Engine::applyIpFilter)"
        )
    except OSError as e:
        assert e.errno == 22, e
    finally:
        v6.close()


@test
def bound_interface_keeps_ipv6_off(ctx):
    if not socket.has_ipv6:
        raise Skip("no IPv6")
    fx = fixture("bound-v6.bin", 30000)
    seed = ctx.close_later(peers.RecordingSeed([fx]))
    six = ctx.close_later(peers.Listener("::1"))
    h = ctx.start()
    uri = fx.magnet(peers=["[::1]:%d" % six.port, "127.0.0.1:%d" % seed.port])
    tid = h.call("add", {"magnet": uri})["tid"]
    h.wait_event("metadata", lambda e: e["tid"] == tid, 30)
    time.sleep(3)
    assert seed.accept_count() >= 1
    assert six.count() == 0, "an IPv6 peer was contacted on a bound interface"


# (6) IP filter -------------------------------------------------------------


def inbox(h):
    path = os.path.join(h.profile, "inbox")
    os.makedirs(path, exist_ok=True)
    return path


P2P = (
    b"# PeerGuardian text list\r\n"
    b"\r\n"
    b"Some Org:10.0.0.0-10.255.255.255\r\n"
    b"Org: with colon:192.168.001.000-192.168.001.255\r\n"
    b"Loopback test:127.0.0.1-127.0.0.1\r\n"
    b"this line is broken\r\n"
)
DAT = (
    b"// eMule list\n"
    b"010.000.000.000 - 010.255.255.255 , 000 , private\n"
    b"127.000.000.001 - 127.000.000.001 , 200 , allowed by level\n"
    b"192.168.000.000 - 192.168.255.255 , 100 , lan\n"
    b"2001:db8::1 - 2001:db8::ffff , 0 , documentation range\n"
    b"999.1.1.1 - 1.1.1.1 , 0 , broken\n"
)


@test
def ip_filter_reads_both_formats_from_the_inbox(ctx):
    h = ctx.start()
    box = inbox(h)
    with open(os.path.join(box, "list.p2p"), "wb") as f:
        f.write(P2P)
    with open(os.path.join(box, "ipfilter.dat"), "wb") as f:
        f.write(DAT)
    r = h.call("setConfig", {"ipFilterPath": os.path.join(box, "list.p2p")})
    assert r["applied"] == ["ipFilterPath"] and r["ipFilter"] == {"ranges": 3, "skipped": 1}, r
    r = h.call("setConfig", {"ipFilterPath": os.path.join(box, "ipfilter.dat")})
    assert r["ipFilter"] == {"ranges": 3, "skipped": 1}, r
    assert h.call("setConfig", {"ipFilterPath": None})["applied"] == ["ipFilterPath"]
    outside = ctx.write("outside.p2p", P2P)
    h.err("setConfig", {"ipFilterPath": outside}, {"bad-request"})
    h.err("setConfig", {"ipFilterPath": os.path.join(box, "missing.p2p")}, {"not-found"})
    os.symlink(outside, os.path.join(box, "link.p2p"))
    h.err("setConfig", {"ipFilterPath": os.path.join(box, "link.p2p")}, {"unsafe-path"})
    with open(os.path.join(box, "garbage.p2p"), "wb") as f:
        f.write(b"nothing to see\n")
    h.err("setConfig", {"ipFilterPath": os.path.join(box, "garbage.p2p")}, {"bad-request"})
    h.err("setConfig", {"ipFilterPath": 7}, {"bad-request"})
    h.err("setConfig", {"ipFilterPath": "relative.p2p"}, {"bad-request"})


@test
def ip_filter_blocks_peers_and_trackers(ctx):
    fx = fixture("filtered.bin", 100000)
    tracker, seed = swarm_for(ctx, fx)
    h = ctx.start()
    box = inbox(h)
    with open(os.path.join(box, "block.p2p"), "wb") as f:
        f.write(P2P)
    with open(os.path.join(box, "allow.dat"), "wb") as f:
        f.write(DAT)
    h.call("setConfig", {"ipFilterPath": os.path.join(box, "block.p2p")})
    save = ctx.folder("save")
    tid = start_download(h, torrent_with(ctx, fx, tracker.url), save)
    magnet = fixture("filtered-magnet.bin", 30000)
    seed.add(magnet)
    mtid = h.call("add", {"magnet": magnet.magnet(peers=["127.0.0.1:%d" % seed.port])})["tid"]
    time.sleep(6)
    assert tracker.count() == 0, "a blocked tracker was contacted"
    assert seed.accept_count() == 0, "a blocked peer was contacted"
    assert not h.events_of("metadata")
    since = h.mark()
    h.call("setConfig", {"ipFilterPath": os.path.join(box, "allow.dat")})
    h.call("cancel", {"tid": mtid})
    h.call("add", {"magnet": magnet.magnet(peers=["127.0.0.1:%d" % seed.port])})
    h.wait_event("metadata", lambda e: e["tid"] == mtid, 30, since)
    h.call("pause", {"tid": tid})
    h.call("resume", {"tid": tid})
    wait_done(h, tid, since, 40)
    verify_output(fx, save)


# (7) Wanted protocols ------------------------------------------------------


@test
def wanted_utp_is_on_by_default(ctx):
    fx = fixture("utp-default.bin", 60000)
    tracker, seed = swarm_for(ctx, fx)
    utp = ctx.close_later(peers.Listener(udp=True, port=seed.port))
    h = ctx.start()
    save = ctx.folder("save")
    tid = start_download(h, torrent_with(ctx, fx, tracker.url), save)
    wait_done(h, tid, 0, 40)
    syns = [d for d in utp.datagrams() if d[:1] == b"\x41"]
    assert syns, "no uTP SYN reached the peer (%d datagrams)" % utp.count()


@test
def wanted_web_seed(ctx):
    fx = fixture("web-seeded.bin", 150000)
    web = ctx.close_later(peers.WebSeed({"/" + fx.name: fx.payload}))
    h = ctx.start()
    path = torrent_with(ctx, fx, None, {"url-list": [web.base]})
    added = add_file(h, path)
    assert added["metadata"]["webSeeds"] == [web.base], added["metadata"]["webSeeds"]
    save = ctx.folder("save")
    h.call("confirm", dict(confirm_args(added["metadata"], save), tid=added["tid"]))
    wait_done(h, added["tid"], 0, 40)
    verify_output(fx, save)
    assert web.count() > 0


@test
def wanted_bittorrent_v2(ctx):
    data = make_torrent.deterministic_bytes("v2", 200000)
    fx = v2torrent.V2Fixture("v2-only.bin", data)
    seeder, stid, port = start_seeder(ctx, fx)
    assert stid == fx.tid, (stid, fx.tid)
    tracker = ctx.close_later(peers.Tracker([("127.0.0.1", port)]))
    leecher = ctx.start("leecher")
    path = ctx.write("v2-leech.torrent", fx.torrent_bytes(tracker.url))
    save = ctx.folder("leecher-save")
    tid = start_download(leecher, path, save)
    wait_done(leecher, tid, 0, 60)
    with open(os.path.join(save, fx.name), "rb") as f:
        assert hashlib.sha256(f.read()).digest() == hashlib.sha256(data).digest()


@test
def wanted_local_discovery_is_off_by_default(ctx):
    fx = fixture("lsd-default.bin", 600000)
    tracker, seed = swarm_for(ctx, fx, block_delay=0.2)
    h = ctx.start()
    pid = h.pid
    since = h.mark()
    tid = start_download(h, torrent_with(ctx, fx, tracker.url), ctx.folder("save"))
    port = h.wait_event("listening", timeout=15, since=since)["port"]
    assert wait_until(lambda: seed.request_count() > 0, 30)
    udp = [name for proto, name in socket_list(pid) if proto == "UDP"]
    assert udp, "no UDP socket at all (uTP should be on)"
    for name in udp:
        assert name.endswith(":%d" % port), "a UDP socket other than the listen port: %s" % name
        assert ":6771" not in name


def run_one(fn, root):
    ctx = Ctx(root, fn.__name__)
    status, reason, detail = "ok", "", ""
    try:
        fn(ctx)
    except Skip as e:
        status, reason = "skip", str(e)
    except Exception as e:
        status, reason = "fail", "%s: %s" % (type(e).__name__, e)
        detail = traceback.format_exc()
    try:
        ctx.finish()
    except Exception as e:
        if status != "fail":
            status, reason = "fail", "%s: %s" % (type(e).__name__, e)
    return status, reason, detail, ctx


def main():
    global CMD
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--helper", default=os.path.join(TORRENT_DIR, "build", "m6e-torrent"))
    ap.add_argument("--only", action="append", default=[])
    ap.add_argument("--keep", action="store_true")
    opts = ap.parse_args()
    CMD = [opts.helper]
    if not os.path.exists(opts.helper):
        print("Bail out! helper not found: %s" % opts.helper)
        return 2
    root = os.path.realpath(tempfile.mkdtemp(prefix="m6e-torrent-privacy-"))
    selected = [f for f in REGISTRY if not opts.only or any(o in f.__name__ for o in opts.only)]
    print("TAP version 13")
    print("1..%d" % len(selected))
    counts = {"ok": 0, "fail": 0, "skip": 0}
    for n, fn in enumerate(selected, 1):
        t0 = time.monotonic()
        status, reason, detail, ctx = run_one(fn, root)
        counts[status] += 1
        if status == "ok":
            print("ok %d - %s" % (n, fn.__name__))
        elif status == "skip":
            print("ok %d - %s # SKIP %s" % (n, fn.__name__, reason))
        else:
            print("not ok %d - %s" % (n, fn.__name__))
            print("# %s" % reason)
            for line in detail.splitlines()[-8:]:
                print("# %s" % line)
        for note in ctx.notes:
            print("# %s: %s" % (fn.__name__, note))
        print("# %s took %.1fs" % (fn.__name__, time.monotonic() - t0))
        sys.stdout.flush()
    print("# pass %d fail %d skip %d" % (counts["ok"], counts["fail"], counts["skip"]))
    if opts.keep:
        print("# kept %s" % root)
    else:
        shutil.rmtree(root, ignore_errors=True)
    return 1 if counts["fail"] else 0


if __name__ == "__main__":
    sys.exit(main())
