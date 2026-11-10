/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "engine.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdexcept>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#include <IOKit/pwr_mgt/IOPMLib.h>

#include <libtorrent/alert_types.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/bdecode.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/write_resume_data.hpp>

#include "protocol.hpp"
#include "safety.hpp"

namespace m6e {

namespace {

using namespace std::chrono_literals;
using sp = lt::settings_pack;

constexpr char kNeutralPeerId[] = "-LT0000-";
constexpr char kNeutralUserAgent[] = "libtorrent";
constexpr std::size_t kMaxMagnetBytes = 16 * 1024;
constexpr std::size_t kMaxResumeBytes = 2 * kMaxTorrentBytes + 1024 * 1024;
constexpr std::size_t kMaxMetadataJson = kMaxLineBytes - 64 * 1024;
constexpr auto kTick = 1s;
constexpr auto kResumeSaveInterval = 30s;
constexpr auto kProgressMinGap = 900ms;
constexpr auto kDeleteWait = 3s;

std::int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string hex(char const* data, std::size_t size) {
  static char const digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (std::size_t i = 0; i < size; i++) {
    auto b = static_cast<unsigned char>(data[i]);
    out.push_back(digits[b >> 4]);
    out.push_back(digits[b & 15]);
  }
  return out;
}

std::string tidOf(lt::info_hash_t const& ih) {
  if (ih.has_v1()) {
    return hex(ih.v1.data(), ih.v1.size());
  }
  return hex(ih.v2.data(), ih.v2.size()).substr(0, 40);
}

lt::load_torrent_limits loadLimits() {
  lt::load_torrent_limits limits;
  limits.max_buffer_size = kMaxTorrentBytes;
  limits.max_decode_depth = kMaxBencodeDepth;
  limits.max_decode_tokens = kMaxBencodeItems;
  limits.max_directory_depth = kMaxDirectoryDepth;
  return limits;
}

lt::download_priority_t toEnginePriority(std::int64_t p) {
  switch (p) {
    case 0:
      return lt::dont_download;
    case 1:
      return lt::low_priority;
    case 3:
      return lt::top_priority;
    default:
      return lt::default_priority;
  }
}

int fromEnginePriority(lt::download_priority_t p) {
  auto v = static_cast<int>(static_cast<std::uint8_t>(p));
  if (v == 0) {
    return 0;
  }
  if (v < 4) {
    return 1;
  }
  return v < 7 ? 2 : 3;
}

std::vector<std::string> splitPath(std::string const& path) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= path.size()) {
    auto end = path.find('/', start);
    if (end == std::string::npos) {
      end = path.size();
    }
    parts.push_back(path.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

std::string relativePath(lt::file_storage const& fs, lt::file_index_t i) {
  std::string full = fs.file_path(i);
  std::string const prefix = fs.name() + "/";
  if (full.compare(0, prefix.size(), prefix) == 0) {
    return full.substr(prefix.size());
  }
  return full;
}

std::optional<SafetyError> checkLayoutComponents(lt::torrent_info const& ti) {
  auto const& fs = ti.layout();
  for (auto i : fs.file_range()) {
    for (auto const& part : splitPath(fs.file_path(i))) {
      if (!componentIsSafe(part)) {
        return SafetyError{"unsafe-path", "a file path is not safe"};
      }
    }
  }
  return std::nullopt;
}

std::string errorCode(lt::error_code const& ec) {
  if (ec == boost::system::errc::no_space_on_device ||
      ec == boost::system::errc::file_too_large ||
      (ec.category() == boost::system::system_category() &&
       ec.value() == EDQUOT)) {
    return "disk-full";
  }
  if (ec.category() == boost::system::system_category() ||
      ec.category() == boost::system::generic_category()) {
    return "io-error";
  }
  return "engine-error";
}

std::string errnoCode(int err) {
  if (err == ENOSPC || err == EDQUOT) {
    return "disk-full";
  }
  return "io-error";
}

double round1(double v) { return std::round(v * 10.0) / 10.0; }

bool validInterfaceName(std::string const& name) {
  if (name.empty() || name.size() > IFNAMSIZ * 2) {
    return false;
  }
  return std::all_of(name.begin(), name.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
  });
}

bool printableAscii(std::string const& s, std::size_t maxLen) {
  if (s.empty() || s.size() > maxLen) {
    return false;
  }
  return std::all_of(s.begin(), s.end(),
                     [](char c) { return c >= 0x20 && c < 0x7f; });
}

enum class ReadResult { ok, notFound, tooLarge, notRegular, error };

ReadResult readFileLimited(std::string const& path, std::size_t limit,
                           std::vector<char>& out, int& err) {
  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    err = errno;
    return err == ENOENT ? ReadResult::notFound : ReadResult::error;
  }
  struct stat st;
  if (fstat(fd, &st) != 0) {
    err = errno;
    close(fd);
    return ReadResult::error;
  }
  if (!S_ISREG(st.st_mode)) {
    close(fd);
    return ReadResult::notRegular;
  }
  if (static_cast<std::uint64_t>(st.st_size) > limit) {
    close(fd);
    return ReadResult::tooLarge;
  }
  out.clear();
  out.reserve(static_cast<std::size_t>(st.st_size));
  char buf[65536];
  for (;;) {
    ssize_t n = read(fd, buf, sizeof buf);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      err = errno;
      close(fd);
      return ReadResult::error;
    }
    if (n == 0) {
      break;
    }
    out.insert(out.end(), buf, buf + n);
    if (out.size() > limit) {
      close(fd);
      return ReadResult::tooLarge;
    }
  }
  close(fd);
  return ReadResult::ok;
}

bool mkdirs(std::string const& path, mode_t mode) {
  if (path.empty()) {
    return false;
  }
  std::string current;
  for (auto const& part : splitPath(path)) {
    if (part.empty()) {
      if (current.empty()) {
        current = "/";
      }
      continue;
    }
    if (current.size() > 1 || (current.size() == 1 && current != "/")) {
      current += "/";
    }
    current += part;
    if (mkdir(current.c_str(), mode) != 0 && errno != EEXIST) {
      return false;
    }
  }
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void appendUnique(json::array& list, std::set<std::string>& seen,
                  std::string const& value) {
  if (!value.empty() && seen.insert(value).second) {
    list.emplace_back(value);
  }
}

json::object buildMetadata(std::string const& tid, lt::torrent_info const& ti,
                           lt::add_torrent_params const& atp) {
  json::object md;
  md["infoHash"] = tid;
  md["name"] = ti.name();
  md["totalSize"] = ti.total_size();
  md["pieceLength"] = ti.piece_length();
  auto const& fs = ti.layout();
  auto const& selection = atp.file_priorities;
  json::array files;
  for (auto i : fs.file_range()) {
    json::object f;
    auto index = static_cast<int>(i);
    f["index"] = index;
    f["path"] = relativePath(fs, i);
    f["length"] = fs.file_size(i);
    if (fs.pad_file_at(i)) {
      f["pad"] = true;
    }
    if (!selection.empty()) {
      f["selected"] = static_cast<std::size_t>(index) < selection.size() &&
                      selection[static_cast<std::size_t>(index)] !=
                          lt::dont_download;
    }
    files.push_back(std::move(f));
  }
  md["files"] = std::move(files);
  json::array trackers;
  std::set<std::string> seen;
  for (auto const& url : atp.trackers) {
    appendUnique(trackers, seen, url);
  }
  md["trackers"] = std::move(trackers);
  json::array seeds;
  seen.clear();
  for (auto const& url : atp.url_seeds) {
    appendUnique(seeds, seen, url);
  }
  md["webSeeds"] = std::move(seeds);
  md["isPrivate"] = ti.priv();
  return md;
}

void checkMetadataSize(json::object const& md) {
  if (json::serialize(md).size() > kMaxMetadataJson) {
    fail("too-large", "the torrent lists too many files to describe");
  }
}

std::string endpointString(lt::tcp::endpoint const& ep) {
  auto addr = ep.address();
  std::string host = addr.to_string();
  if (addr.is_v6()) {
    host = "[" + host + "]";
  }
  return host + ":" + std::to_string(ep.port());
}

}  // namespace

Engine::Engine(EngineOptions options, Emit emit)
    : options_(std::move(options)), emit_(std::move(emit)) {
  pendingDir_ = options_.profileDir + "/pending";
  int fds[2];
  if (pipe(fds) != 0) {
    throw std::runtime_error("cannot create the wake pipe");
  }
  wakeRead_ = fds[0];
  wakeWrite_ = fds[1];
  for (int fd : fds) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    fcntl(fd, F_SETFD, FD_CLOEXEC);
  }
  lt::session_params params(baseSettings());
  session_ = std::make_unique<lt::session>(std::move(params));
  int wake = wakeWrite_;
  session_->set_alert_notify([wake] {
    char c = 1;
    (void)!::write(wake, &c, 1);
  });
  auto now = Clock::now();
  nextTick_ = now + kTick;
  nextResumeSave_ = now + kResumeSaveInterval;
  idleSince_ = now;
}

Engine::~Engine() {
  if (session_) {
    session_->set_alert_notify({});
  }
  session_.reset();
  updatePower(false);
}

std::string Engine::engineName() {
  return "libtorrent-rasterbar " + std::to_string(LIBTORRENT_VERSION_MAJOR) +
         "." + std::to_string(LIBTORRENT_VERSION_MINOR) + "." +
         std::to_string(LIBTORRENT_VERSION_TINY);
}

lt::settings_pack Engine::baseSettings() const {
  lt::settings_pack s;
  s.set_str(sp::listen_interfaces, "");
  s.set_str(sp::outgoing_interfaces, "");
  s.set_str(sp::dht_bootstrap_nodes, "");
  s.set_bool(sp::enable_dht, false);
  s.set_bool(sp::enable_lsd, false);
  s.set_bool(sp::enable_upnp, false);
  s.set_bool(sp::enable_natpmp, false);
  s.set_bool(sp::enable_ip_notifier, false);
  s.set_int(sp::stop_tracker_timeout, 1);
  s.set_int(sp::max_queued_disk_bytes, 4 * 1024 * 1024);
  s.set_int(sp::aio_threads, 2);
  s.set_int(sp::checking_mem_usage, 64);
  s.set_int(sp::max_metadata_size, kMaxTorrentBytes);
  s.set_int(sp::metadata_token_limit, kMaxBencodeItems);
  s.set_int(sp::max_retry_port_bind, 10);
  s.set_int(sp::min_reconnect_time, 10);
  s.set_int(sp::alert_mask, lt::alert_category::status |
                                lt::alert_category::error |
                                lt::alert_category::storage |
                                lt::alert_category::tracker);
  applyIdentity(s);
  applyEncryption(s);
  s.set_int(sp::connections_limit, 200);
  return s;
}

void Engine::applyIdentity(lt::settings_pack& s) const {
  s.set_str(sp::peer_fingerprint, config_.peerIdPrefix.value_or(kNeutralPeerId));
  auto ua = config_.userAgent.value_or(kNeutralUserAgent);
  s.set_str(sp::user_agent, ua);
  s.set_str(sp::handshake_client_version, ua);
}

void Engine::applyEncryption(lt::settings_pack& s) const {
  int policy = sp::pe_enabled;
  int level = sp::pe_both;
  if (config_.encryption == "disabled") {
    policy = sp::pe_disabled;
  } else if (config_.encryption == "forced") {
    policy = sp::pe_forced;
    level = sp::pe_rc4;
  }
  s.set_int(sp::out_enc_policy, policy);
  s.set_int(sp::in_enc_policy, policy);
  s.set_int(sp::allowed_enc_level, level);
  s.set_bool(sp::prefer_rc4, config_.encryption == "forced");
}

void Engine::applyLimits() {
  lt::settings_pack s;
  int down = limits_.turtle ? limits_.turtleDownKBps : limits_.downKBps;
  int up = limits_.turtle ? limits_.turtleUpKBps : limits_.upKBps;
  s.set_int(sp::download_rate_limit, down * 1024);
  s.set_int(sp::upload_rate_limit, up * 1024);
  s.set_int(sp::active_downloads, limits_.maxActiveDownloads);
  s.set_int(sp::active_limit, limits_.maxActiveTorrents);
  session_->apply_settings(std::move(s));
}

json::object Engine::handle(std::string const& op, json::object const& args) {
  if (op == "add") {
    return opAdd(args);
  }
  if (op == "confirm") {
    return opConfirm(args);
  }
  if (op == "cancel") {
    return opCancel(args);
  }
  if (op == "pause" || op == "resume") {
    return opPause(args, op == "pause");
  }
  if (op == "pauseAll" || op == "resumeAll") {
    return opPauseAll(op == "pauseAll");
  }
  if (op == "remove") {
    return opRemove(args);
  }
  if (op == "list") {
    return opList();
  }
  if (op == "detail") {
    return opDetail(args);
  }
  if (op == "setLimits") {
    return opSetLimits(args);
  }
  if (op == "setConfig") {
    return opSetConfig(args);
  }
  if (op == "restore") {
    return opRestore(args);
  }
  fail("bad-request", "unknown operation " + op);
}

json::object Engine::opAdd(json::object const& args) {
  auto const* file = findArg(args, "file");
  auto const* magnet = findArg(args, "magnet");
  if ((file != nullptr) == (magnet != nullptr)) {
    fail("bad-request", "add needs exactly one of file and magnet");
  }
  if (file) {
    return addFile(stringArg(args, "file"));
  }
  return addMagnet(stringArg(args, "magnet"));
}

json::object Engine::addFile(std::string const& path) {
  if (!isAbsolutePath(path)) {
    fail("bad-request", "the torrent file path must be absolute");
  }
  std::vector<char> buf;
  int err = 0;
  switch (readFileLimited(path, kMaxTorrentBytes, buf, err)) {
    case ReadResult::ok:
      break;
    case ReadResult::notFound:
      fail("not-found", "no file at " + path);
    case ReadResult::tooLarge:
      fail("too-large", "the torrent file is larger than 16 MiB");
    case ReadResult::notRegular:
      fail("invalid-torrent", "the torrent path is not a regular file");
    case ReadResult::error:
      fail("io-error", "cannot read the torrent file: " +
                           std::string(std::strerror(err)));
  }
  lt::error_code ec;
  int errorPos = 0;
  lt::bdecode_node root = lt::bdecode(buf, ec, &errorPos, kMaxBencodeDepth,
                                      kMaxBencodeItems);
  if (ec) {
    fail("invalid-torrent", "the torrent file is not valid bencoding (" +
                                ec.message() + ")");
  }
  if (root.type() != lt::bdecode_node::dict_t) {
    fail("invalid-torrent", "the torrent file is not a dictionary");
  }
  auto info = root.dict_find_dict("info");
  if (!info) {
    fail("invalid-torrent", "the torrent has no info dictionary");
  }
  if (auto e = checkInfoDict(info)) {
    fail(e->code, e->message);
  }
  lt::add_torrent_params atp = lt::load_torrent_parsed(root, ec, loadLimits());
  if (ec || !atp.ti || !atp.ti->is_valid()) {
    fail("invalid-torrent",
         "the torrent is malformed" + (ec ? " (" + ec.message() + ")" : ""));
  }
  if (auto e = checkLayoutComponents(*atp.ti)) {
    fail(e->code, e->message);
  }
  std::string tid = tidOf(atp.ti->info_hashes());
  if (torrents_.count(tid)) {
    fail("duplicate", "the torrent is already in the list", tid);
  }
  auto it = pending_.find(tid);
  if (it != pending_.end() && !it->second.fetching) {
    json::object r;
    r["tid"] = tid;
    r["state"] = "pending-confirm";
    r["metadata"] = it->second.metadata;
    return r;
  }
  json::object md = buildMetadata(tid, *atp.ti, atp);
  checkMetadataSize(md);
  if (it != pending_.end()) {
    dropPending(tid);
  }
  Pending p;
  p.tid = tid;
  p.atp = std::move(atp);
  p.metadata = md;
  p.addedAtMs = nowMs();
  pending_.emplace(tid, std::move(p));
  json::object r;
  r["tid"] = tid;
  r["state"] = "pending-confirm";
  r["metadata"] = std::move(md);
  return r;
}

json::object Engine::addMagnet(std::string const& uri) {
  if (uri.size() > kMaxMagnetBytes || uri.rfind("magnet:?", 0) != 0) {
    fail("bad-request", "not a magnet link");
  }
  for (char c : uri) {
    if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f) {
      fail("bad-request", "the magnet link has spaces or control characters");
    }
  }
  lt::error_code ec;
  lt::add_torrent_params atp = lt::parse_magnet_uri(uri, ec);
  if (ec) {
    fail("invalid-torrent", "the magnet link names no torrent (" +
                                ec.message() + ")");
  }
  std::string tid = tidOf(atp.info_hashes);
  if (torrents_.count(tid)) {
    fail("duplicate", "the torrent is already in the list", tid);
  }
  auto it = pending_.find(tid);
  if (it != pending_.end()) {
    json::object r;
    r["tid"] = tid;
    r["state"] = it->second.fetching ? "fetching-metadata" : "pending-confirm";
    if (!it->second.fetching) {
      r["metadata"] = it->second.metadata;
    }
    return r;
  }
  if (!mkdirs(pendingDir_, 0700)) {
    fail("io-error", "cannot create " + pendingDir_);
  }
  lt::add_torrent_params fetch = atp;
  fetch.name.clear();
  fetch.save_path = pendingDir_;
  fetch.file_priorities.clear();
  fetch.flags = lt::torrent_flags::update_subscribe |
                lt::torrent_flags::default_dont_download |
                lt::torrent_flags::disable_pex | lt::torrent_flags::disable_lsd;
  if (!config_.dht) {
    fetch.flags |= lt::torrent_flags::disable_dht;
  }
  ensureListening();
  if (config_.dht && !dhtOn_) {
    lt::settings_pack s;
    s.set_bool(sp::enable_dht, true);
    session_->apply_settings(std::move(s));
    dhtOn_ = true;
  }
  lt::torrent_handle h = session_->add_torrent(std::move(fetch), ec);
  if (ec) {
    refreshNetwork();
    fail("engine-error", "cannot fetch the magnet link: " + ec.message());
  }
  Pending p;
  p.tid = tid;
  p.atp = std::move(atp);
  p.handle = h;
  p.fetching = true;
  p.addedAtMs = nowMs();
  pending_.emplace(tid, std::move(p));
  handles_[h] = tid;
  refreshNetwork();
  json::object r;
  r["tid"] = tid;
  r["state"] = "fetching-metadata";
  return r;
}

void Engine::onMetadata(lt::torrent_handle const& h) {
  auto hit = handles_.find(h);
  if (hit == handles_.end()) {
    return;
  }
  std::string tid = hit->second;
  auto it = pending_.find(tid);
  if (it == pending_.end() || !it->second.fetching) {
    return;
  }
  Pending& p = it->second;
  auto ti = h.torrent_file();
  std::optional<SafetyError> problem;
  if (!ti || !ti->is_valid()) {
    problem = SafetyError{"invalid-torrent", "the metadata is not usable"};
  } else {
    auto section = ti->info_section();
    lt::error_code ec;
    lt::bdecode_node info = lt::bdecode(section, ec, nullptr, kMaxBencodeDepth,
                                        kMaxBencodeItems);
    if (ec) {
      problem = SafetyError{"invalid-torrent",
                            "the metadata is not valid bencoding"};
    } else {
      problem = checkInfoDict(info);
    }
    if (!problem) {
      problem = checkLayoutComponents(*ti);
    }
  }
  json::object md;
  if (!problem) {
    md = buildMetadata(tid, *ti, p.atp);
    if (json::serialize(md).size() > kMaxMetadataJson) {
      problem = SafetyError{"too-large",
                            "the torrent lists too many files to describe"};
    }
  }
  session_->remove_torrent(h, lt::session_handle::delete_partfile);
  handles_.erase(hit);
  if (problem) {
    pending_.erase(it);
    refreshNetwork();
    emitError(tid, problem->code, problem->message);
    return;
  }
  p.atp.ti = std::make_shared<lt::torrent_info>(*ti);
  p.handle = {};
  p.fetching = false;
  p.metadata = md;
  refreshNetwork();
  json::object e = event("metadata");
  e["tid"] = tid;
  e["metadata"] = std::move(md);
  emit_(e);
}

json::object Engine::opConfirm(json::object const& args) {
  std::string tid = tidArg(args);
  if (torrents_.count(tid)) {
    fail("duplicate", "the torrent is already confirmed", tid);
  }
  auto it = pending_.find(tid);
  if (it == pending_.end()) {
    fail("not-found", "no pending torrent " + tid);
  }
  Pending& p = it->second;
  if (p.fetching) {
    fail("bad-request", "the torrent's metadata has not arrived yet");
  }
  auto const* filesArg = findArg(args, "files");
  if (!filesArg || !filesArg->is_array()) {
    fail("bad-request", "files must be an array");
  }
  std::string savePath = stringArg(args, "savePath");
  bool sequential = boolArg(args, "sequential", false);
  bool start = boolArg(args, "start", false);
  if (!isAbsolutePath(savePath)) {
    fail("bad-request", "savePath must be an absolute path");
  }

  auto const& ti = *p.atp.ti;
  auto const& fs = ti.layout();
  int const count = fs.num_files();
  std::vector<lt::download_priority_t> priorities(
      static_cast<std::size_t>(count), lt::default_priority);
  std::vector<bool> given(static_cast<std::size_t>(count), false);
  for (auto const& entry : filesArg->as_array()) {
    if (!entry.is_object()) {
      fail("bad-request", "each file must be an object");
    }
    auto const& f = entry.as_object();
    auto index = intArg(f, "index", 0, count - 1);
    auto priority = intArg(f, "priority", 0, 3);
    auto i = static_cast<std::size_t>(index);
    if (given[i]) {
      fail("bad-request", "a file index is listed twice");
    }
    given[i] = true;
    priorities[i] = toEnginePriority(priority);
  }
  for (auto i : fs.file_range()) {
    if (fs.pad_file_at(i)) {
      priorities[static_cast<std::size_t>(static_cast<int>(i))] =
          lt::dont_download;
    }
  }

  char resolved[PATH_MAX];
  if (!realpath(savePath.c_str(), resolved)) {
    fail(errnoCode(errno), "the save folder is not usable: " +
                               std::string(std::strerror(errno)));
  }
  std::string root = resolved;
  struct stat st;
  if (stat(root.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
    fail("io-error", "the save folder is not a folder");
  }
  if (access(root.c_str(), W_OK) != 0) {
    fail("io-error", "the save folder is not writable");
  }
  std::int64_t wanted = 0;
  for (auto i : fs.file_range()) {
    if (fs.pad_file_at(i)) {
      continue;
    }
    auto parts = splitPath(fs.file_path(i));
    if (auto e = checkNoSymlinkEscape(root, parts)) {
      fail(e->code, e->message);
    }
    if (priorities[static_cast<std::size_t>(static_cast<int>(i))] ==
        lt::dont_download) {
      continue;
    }
    std::int64_t size = fs.file_size(i);
    struct stat existing;
    std::string full = root + "/" + fs.file_path(i);
    if (lstat(full.c_str(), &existing) == 0 && S_ISREG(existing.st_mode)) {
      size -= std::min<std::int64_t>(size, existing.st_size);
    }
    wanted += size;
  }
  struct statfs fsinfo;
  if (statfs(root.c_str(), &fsinfo) == 0) {
    auto available = static_cast<std::int64_t>(fsinfo.f_bavail) *
                     static_cast<std::int64_t>(fsinfo.f_bsize);
    if (wanted > available) {
      fail("disk-full", "the selected files do not fit in the save folder");
    }
  }

  lt::add_torrent_params atp = p.atp;
  atp.name.clear();
  atp.save_path = root;
  atp.file_priorities = priorities;
  atp.added_time = std::time(nullptr);
  atp.flags = lt::torrent_flags::update_subscribe |
              lt::torrent_flags::apply_ip_filter |
              lt::torrent_flags::need_save_resume |
              lt::torrent_flags::duplicate_is_error | privacyFlags(ti.priv());
  if (sequential) {
    atp.flags |= lt::torrent_flags::sequential_download;
  }
  if (!start) {
    atp.flags |= lt::torrent_flags::paused;
  }
  if (start) {
    ensureListening();
  }
  lt::error_code ec;
  lt::torrent_handle h = session_->add_torrent(atp, ec);
  if (ec) {
    refreshNetwork();
    if (ec == lt::errors::duplicate_torrent) {
      fail("duplicate", "the torrent is already in the session", tid);
    }
    fail("engine-error", "cannot add the torrent: " + ec.message());
  }
  Torrent t;
  t.tid = tid;
  t.handle = h;
  t.ti = p.atp.ti;
  t.metadata = p.metadata;
  t.addedAtMs = nowMs();
  t.userPaused = !start;
  pending_.erase(it);
  auto& stored = torrents_.emplace(tid, std::move(t)).first->second;
  handles_[h] = tid;
  if (!writeResume(stored, atp)) {
    log("warn", "the torrent could not be saved for the next start");
  }
  refreshStatus(stored);
  refreshNetwork();
  updatePower(anyActive());
  json::object r;
  r["status"] = statusJson(stored);
  return r;
}

json::object Engine::opCancel(json::object const& args) {
  std::string tid = tidArg(args);
  if (!pending_.count(tid)) {
    fail("not-found", "no pending torrent " + tid);
  }
  dropPending(tid);
  return {};
}

void Engine::dropPending(std::string const& tid) {
  auto it = pending_.find(tid);
  if (it == pending_.end()) {
    return;
  }
  if (it->second.fetching && it->second.handle.is_valid()) {
    session_->remove_torrent(it->second.handle,
                             lt::session_handle::delete_partfile);
    handles_.erase(it->second.handle);
  }
  pending_.erase(it);
  refreshNetwork();
}

Engine::Torrent& Engine::confirmed(std::string const& tid) {
  auto it = torrents_.find(tid);
  if (it == torrents_.end()) {
    fail("not-found", "no torrent " + tid);
  }
  return it->second;
}

void Engine::setPaused(Torrent& t, bool pause) {
  t.handle.unset_flags(lt::torrent_flags::auto_managed);
  t.userPaused = pause;
  t.completed = false;
  if (pause) {
    t.handle.pause();
  } else {
    if (t.status.errc) {
      t.handle.clear_error();
    }
    ensureListening();
    t.handle.resume();
  }
  requestResume(t);
}

json::object Engine::opPause(json::object const& args, bool pause) {
  Torrent& t = confirmed(tidArg(args));
  setPaused(t, pause);
  refreshStatus(t);
  refreshNetwork();
  updatePower(anyActive());
  json::object r;
  r["status"] = statusJson(t);
  return r;
}

json::object Engine::opPauseAll(bool pause) {
  for (auto& [tid, t] : torrents_) {
    setPaused(t, pause);
  }
  for (auto& [tid, t] : torrents_) {
    refreshStatus(t);
  }
  refreshNetwork();
  updatePower(anyActive());
  return {};
}

json::object Engine::opRemove(json::object const& args) {
  std::string tid = tidArg(args);
  bool deleteFiles = boolArg(args, "deleteFiles", false);
  if (pending_.count(tid)) {
    dropPending(tid);
    return {};
  }
  Torrent& t = confirmed(tid);
  lt::torrent_handle h = t.handle;
  session_->remove_torrent(h, deleteFiles ? lt::session_handle::delete_files
                                          : lt::session_handle::delete_partfile);
  handles_.erase(h);
  torrents_.erase(tid);
  deleteResume(tid);
  if (deleteFiles) {
    deleting_.insert(tid);
    pumpUntil([&] { return !deleting_.count(tid); }, kDeleteWait);
    deleting_.erase(tid);
  }
  refreshNetwork();
  updatePower(anyActive());
  return {};
}

json::object Engine::opList() {
  json::array list;
  for (auto& [tid, t] : torrents_) {
    refreshStatus(t);
    list.push_back(statusJson(t));
  }
  json::object r;
  r["torrents"] = std::move(list);
  return r;
}

json::object Engine::opDetail(json::object const& args) {
  std::string tid = tidArg(args);
  json::object r;
  auto pit = pending_.find(tid);
  if (pit != pending_.end()) {
    Pending const& p = pit->second;
    r["status"] = pendingStatusJson(p);
    r["metadata"] = p.metadata;
    r["trackers"] = json::array();
    r["peers"] = json::array();
    json::object pieces;
    pieces["count"] = p.fetching ? 0 : p.atp.ti->num_pieces();
    pieces["have"] = base64(std::vector<std::uint8_t>(
        p.fetching ? 0 : static_cast<std::size_t>((p.atp.ti->num_pieces() + 7) / 8)));
    r["pieces"] = std::move(pieces);
    json::array files;
    if (!p.fetching) {
      for (auto i : p.atp.ti->layout().file_range()) {
        json::object f;
        f["index"] = static_cast<int>(i);
        f["priority"] = 2;
        f["progress"] = 0;
        files.push_back(std::move(f));
      }
    }
    r["files"] = std::move(files);
    return r;
  }
  Torrent& t = confirmed(tid);
  t.status = t.handle.status(lt::torrent_handle::query_pieces |
                             lt::torrent_handle::query_accurate_download_counters |
                             lt::torrent_handle::query_save_path);
  r["status"] = statusJson(t);
  r["metadata"] = t.metadata;
  r["trackers"] = trackersJson(t.handle);
  std::vector<lt::peer_info> peers;
  t.handle.get_peer_info(peers);
  json::array peerList;
  for (auto const& pi : peers) {
    json::object o;
    o["address"] = endpointString(pi.remote_endpoint());
    o["client"] = pi.client;
    o["encrypted"] =
        bool(pi.flags & (lt::peer_info::rc4_encrypted |
                         lt::peer_info::plaintext_encrypted));
    o["downKBps"] = round1(pi.payload_down_speed / 1024.0);
    o["upKBps"] = round1(pi.payload_up_speed / 1024.0);
    o["progress"] = std::clamp<double>(pi.progress, 0.0, 1.0);
    peerList.push_back(std::move(o));
  }
  r["peers"] = std::move(peerList);
  auto const& bits = t.status.pieces;
  int count = t.ti ? t.ti->num_pieces() : bits.size();
  std::vector<std::uint8_t> have(static_cast<std::size_t>((count + 7) / 8));
  for (int i = 0; i < count && i < bits.size(); i++) {
    if (bits.get_bit(lt::piece_index_t{i})) {
      have[static_cast<std::size_t>(i / 8)] |=
          static_cast<std::uint8_t>(0x80 >> (i % 8));
    }
  }
  json::object pieces;
  pieces["count"] = count;
  pieces["have"] = base64(have);
  r["pieces"] = std::move(pieces);
  auto priorities = t.handle.get_file_priorities();
  auto progress = t.handle.file_progress(lt::torrent_handle::piece_granularity);
  json::array files;
  if (t.ti) {
    auto const& fs = t.ti->layout();
    for (auto i : fs.file_range()) {
      auto k = static_cast<std::size_t>(static_cast<int>(i));
      json::object f;
      f["index"] = static_cast<int>(i);
      f["priority"] = k < priorities.size() ? fromEnginePriority(priorities[k]) : 2;
      std::int64_t size = fs.file_size(i);
      double done = k < progress.size() ? static_cast<double>(progress[k]) : 0.0;
      f["progress"] = size > 0 ? std::clamp(done / static_cast<double>(size), 0.0, 1.0) : 1.0;
      files.push_back(std::move(f));
    }
  }
  r["files"] = std::move(files);
  return r;
}

json::array Engine::trackersJson(lt::torrent_handle const& h) const {
  json::array list;
  for (auto const& ae : h.trackers()) {
    bool working = false, updating = false, failed = false, enabled = false;
    int seeds = -1, peers = -1;
    std::string message;
    for (auto const& ep : ae.endpoints) {
      enabled = enabled || ep.enabled;
      for (auto const& ih : ep.info_hashes) {
        updating = updating || ih.updating;
        if (ih.last_error) {
          failed = true;
          message = ih.message.empty() ? ih.last_error.message() : ih.message;
        } else if (ih.start_sent && ih.fails == 0) {
          working = true;
          if (!ih.message.empty()) {
            message = ih.message;
          }
        }
        seeds = std::max(seeds, ih.scrape_complete);
        peers = std::max(peers, ih.scrape_incomplete);
      }
    }
    json::object o;
    o["url"] = ae.url;
    o["status"] = working    ? "working"
                  : updating ? "updating"
                  : failed   ? "error"
                  : enabled  ? "updating"
                             : "disabled";
    o["seeds"] = seeds;
    o["peers"] = peers;
    o["message"] = message;
    list.push_back(std::move(o));
  }
  return list;
}

json::object Engine::opSetLimits(json::object const& args) {
  Limits next = limits_;
  constexpr std::int64_t kMaxKBps = 4 * 1024 * 1024;
  next.downKBps = static_cast<int>(intArg(args, "downKBps", 0, kMaxKBps, next.downKBps));
  next.upKBps = static_cast<int>(intArg(args, "upKBps", 0, kMaxKBps, next.upKBps));
  next.maxActiveDownloads = static_cast<int>(
      intArg(args, "maxActiveDownloads", -1, 10000, next.maxActiveDownloads));
  next.maxActiveTorrents = static_cast<int>(
      intArg(args, "maxActiveTorrents", -1, 10000, next.maxActiveTorrents));
  if (auto const* turtle = findArg(args, "turtle")) {
    if (!turtle->is_object()) {
      fail("bad-request", "turtle must be an object");
    }
    auto const& o = turtle->as_object();
    next.turtle = boolArg(o, "enabled", next.turtle);
    next.turtleDownKBps =
        static_cast<int>(intArg(o, "downKBps", 0, kMaxKBps, next.turtleDownKBps));
    next.turtleUpKBps =
        static_cast<int>(intArg(o, "upKBps", 0, kMaxKBps, next.turtleUpKBps));
  }
  limits_ = next;
  applyLimits();
  return {};
}

json::object Engine::opSetConfig(json::object const& args) {
  Config next = config_;
  json::array applied, unsupported;
  for (auto const& [key, value] : args) {
    std::string k(key);
    if (k == "encryption") {
      std::string v = stringArg(args, k);
      if (v != "disabled" && v != "enabled" && v != "forced") {
        fail("bad-request", "encryption must be disabled, enabled or forced");
      }
      next.encryption = v;
    } else if (k == "dht") {
      next.dht = boolArg(args, k);
    } else if (k == "pex") {
      next.pex = boolArg(args, k);
    } else if (k == "lpd") {
      next.lpd = boolArg(args, k);
    } else if (k == "portMapping") {
      next.portMapping = boolArg(args, k);
    } else if (k == "listenPort") {
      next.listenPort = static_cast<int>(intArg(args, k, 0, 65535));
    } else if (k == "maxConnections") {
      next.maxConnections = static_cast<int>(intArg(args, k, 0, 65535));
    } else if (k == "bindInterface") {
      if (value.is_null()) {
        next.bindInterface.reset();
      } else {
        std::string v = stringArg(args, k);
        if (!validInterfaceName(v)) {
          fail("bad-request", "bindInterface is not an interface name");
        }
        next.bindInterface = v.empty() ? std::nullopt : std::optional(v);
      }
    } else if (k == "peerIdPrefix") {
      if (value.is_null()) {
        next.peerIdPrefix.reset();
      } else {
        std::string v = stringArg(args, k);
        if (!printableAscii(v, 20)) {
          fail("bad-request", "peerIdPrefix must be 1 to 20 printable characters");
        }
        next.peerIdPrefix = v;
      }
    } else if (k == "userAgent") {
      if (value.is_null()) {
        next.userAgent.reset();
      } else {
        std::string v = stringArg(args, k);
        if (!printableAscii(v, 128)) {
          fail("bad-request", "userAgent must be 1 to 128 printable characters");
        }
        next.userAgent = v;
      }
    } else if (k == "proxy") {
      if (!value.is_object()) {
        fail("bad-request", "proxy must be an object");
      }
      std::string type = stringArg(value.as_object(), "type");
      if (type != "none" && type != "socks5") {
        fail("bad-request", "proxy.type must be none or socks5");
      }
      (type == "none" ? applied : unsupported).emplace_back(k);
      continue;
    } else if (k == "ipFilterPath") {
      if (!value.is_null() && !value.is_string()) {
        fail("bad-request", "ipFilterPath must be a path or null");
      }
      (value.is_null() ? applied : unsupported).emplace_back(k);
      continue;
    } else if (k == "seedGoal") {
      if (!value.is_object()) {
        fail("bad-request", "seedGoal must be an object");
      }
      auto const& o = value.as_object();
      bool never = !findArg(o, "ratio") && !findArg(o, "hours");
      (never ? applied : unsupported).emplace_back(k);
      continue;
    } else {
      unsupported.emplace_back(k);
      continue;
    }
    applied.emplace_back(k);
  }
  bool reopen = listening_ && (next.bindInterface != config_.bindInterface ||
                               (next.listenPort && next.listenPort != port_));
  config_ = next;
  if (config_.listenPort) {
    port_ = config_.listenPort;
  }
  lt::settings_pack s;
  applyIdentity(s);
  applyEncryption(s);
  s.set_int(sp::connections_limit,
            config_.maxConnections > 0 ? config_.maxConnections : 200);
  s.set_str(sp::outgoing_interfaces, config_.bindInterface.value_or(""));
  if (reopen) {
    s.set_str(sp::listen_interfaces, listenSpec());
    listenAnnounced_ = false;
  }
  session_->apply_settings(std::move(s));
  for (auto& [tid, t] : torrents_) {
    auto flags = privacyFlags(t.ti && t.ti->priv());
    t.handle.set_flags(flags, lt::torrent_flags::disable_pex |
                                  lt::torrent_flags::disable_dht |
                                  lt::torrent_flags::disable_lsd);
  }
  refreshNetwork();
  json::object r;
  r["applied"] = std::move(applied);
  r["unsupported"] = std::move(unsupported);
  return r;
}

lt::torrent_flags_t Engine::privacyFlags(bool isPrivate) const {
  if (isPrivate) {
    return lt::torrent_flags::disable_dht | lt::torrent_flags::disable_pex |
           lt::torrent_flags::disable_lsd;
  }
  lt::torrent_flags_t flags{};
  if (!config_.pex) {
    flags |= lt::torrent_flags::disable_pex;
  }
  return flags;
}

json::object Engine::opRestore(json::object const& args) {
  bool start = boolArg(args, "start", false);
  std::vector<std::string> names;
  if (DIR* dir = opendir(options_.profileDir.c_str())) {
    while (dirent* entry = readdir(dir)) {
      std::string name = entry->d_name;
      if (name.size() == 47 && name.compare(40, 7, ".resume") == 0 &&
          std::all_of(name.begin(), name.begin() + 40, [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
          })) {
        names.push_back(name);
      }
    }
    closedir(dir);
  }
  std::sort(names.begin(), names.end());
  for (auto const& name : names) {
    std::string tid = name.substr(0, 40);
    if (torrents_.count(tid) || pending_.count(tid)) {
      continue;
    }
    std::string path = options_.profileDir + "/" + name;
    std::vector<char> buf;
    int err = 0;
    if (readFileLimited(path, kMaxResumeBytes, buf, err) != ReadResult::ok) {
      log("warn", "skipped unreadable saved torrent " + tid);
      continue;
    }
    lt::error_code ec;
    lt::bdecode_node node = lt::bdecode(buf, ec, nullptr, kMaxBencodeDepth,
                                        2 * kMaxBencodeItems);
    lt::bdecode_node info;
    if (!ec && node.type() == lt::bdecode_node::dict_t) {
      info = node.dict_find_dict("info");
    }
    if (!info) {
      log("warn", "skipped damaged saved torrent " + tid);
      continue;
    }
    if (auto e = checkInfoDict(info)) {
      log("warn", "skipped unsafe saved torrent " + tid + ": " + e->message);
      continue;
    }
    lt::add_torrent_params atp = lt::read_resume_data(node, ec);
    if (ec || !atp.ti || !atp.ti->is_valid() ||
        tidOf(atp.ti->info_hashes()) != tid || !isAbsolutePath(atp.save_path) ||
        checkLayoutComponents(*atp.ti)) {
      log("warn", "skipped invalid saved torrent " + tid);
      continue;
    }
    auto m6eState = node.dict_find_dict("m6e");
    Torrent t;
    t.tid = tid;
    t.addedAtMs = m6eState ? m6eState.dict_find_int_value("addedAtMs", 0) : 0;
    if (t.addedAtMs <= 0) {
      t.addedAtMs = static_cast<std::int64_t>(atp.added_time) * 1000;
    }
    t.userPaused = m6eState && m6eState.dict_find_int_value("paused", 0) != 0;
    t.completed = m6eState && m6eState.dict_find_int_value("completed", 0) != 0;
    t.finishedSeen = m6eState && m6eState.dict_find_int_value("finished", 0) != 0;
    atp.flags &= ~(lt::torrent_flags::auto_managed | lt::torrent_flags::paused |
                   lt::torrent_flags::disable_dht | lt::torrent_flags::disable_pex |
                   lt::torrent_flags::disable_lsd | lt::torrent_flags::upload_mode |
                   lt::torrent_flags::default_dont_download);
    atp.flags |= lt::torrent_flags::update_subscribe | privacyFlags(atp.ti->priv());
    if (!start || t.userPaused || t.completed) {
      atp.flags |= lt::torrent_flags::paused;
    } else {
      ensureListening();
    }
    t.ti = atp.ti;
    t.metadata = buildMetadata(tid, *atp.ti, atp);
    lt::torrent_handle h = session_->add_torrent(std::move(atp), ec);
    if (ec) {
      log("warn", "could not restore " + tid + ": " + ec.message());
      continue;
    }
    t.handle = h;
    auto& stored = torrents_.emplace(tid, std::move(t)).first->second;
    handles_[h] = tid;
    refreshStatus(stored);
  }
  refreshNetwork();
  updatePower(anyActive());
  return opList();
}

void Engine::refreshStatus(Torrent& t) {
  t.status = t.handle.status(lt::torrent_handle::query_save_path);
}

bool Engine::isActive(Torrent const& t) const {
  auto s = stateOf(t);
  return s == "checking" || s == "queued" || s == "downloading" ||
         s == "seeding";
}

bool Engine::anyActive() const {
  return std::any_of(torrents_.begin(), torrents_.end(),
                     [&](auto const& kv) { return isActive(kv.second); });
}

bool Engine::anyPublicActive() const {
  return std::any_of(torrents_.begin(), torrents_.end(), [&](auto const& kv) {
    return isActive(kv.second) && !(kv.second.ti && kv.second.ti->priv());
  });
}

bool Engine::anyFetching() const {
  return std::any_of(pending_.begin(), pending_.end(),
                     [](auto const& kv) { return kv.second.fetching; });
}

std::string Engine::stateOf(Torrent const& t) const {
  auto const& st = t.status;
  if (st.errc) {
    return "error";
  }
  if (st.flags & lt::torrent_flags::paused) {
    if (st.flags & lt::torrent_flags::auto_managed) {
      return "queued";
    }
    return t.completed ? "completed" : "paused";
  }
  switch (st.state) {
    case lt::torrent_status::checking_files:
    case lt::torrent_status::checking_resume_data:
      return "checking";
    case lt::torrent_status::finished:
    case lt::torrent_status::seeding:
      return t.flushing ? "downloading" : "seeding";
    default:
      return "downloading";
  }
}

json::object Engine::statusJson(Torrent const& t) const {
  auto const& st = t.status;
  json::object o;
  std::string state = stateOf(t);
  o["tid"] = t.tid;
  o["name"] = t.ti ? t.ti->name() : st.name;
  o["state"] = state;
  o["progress"] = std::clamp<double>(st.progress, 0.0, 1.0);
  o["size"] = st.total_wanted;
  o["downloaded"] = st.total_wanted_done;
  o["uploaded"] = st.all_time_upload;
  o["downKBps"] = round1(st.download_payload_rate / 1024.0);
  o["upKBps"] = round1(st.upload_payload_rate / 1024.0);
  std::int64_t remaining = st.total_wanted - st.total_wanted_done;
  if (state == "downloading" && st.download_payload_rate > 0 && remaining > 0) {
    o["etaSeconds"] = (remaining + st.download_payload_rate - 1) /
                      st.download_payload_rate;
  } else {
    o["etaSeconds"] = nullptr;
  }
  std::int64_t base = st.total_wanted_done > 0 ? st.total_wanted_done
                                               : st.all_time_download;
  o["ratio"] = base > 0 ? std::round(static_cast<double>(st.all_time_upload) /
                                     static_cast<double>(base) * 1000.0) /
                              1000.0
                        : 0.0;
  o["seeds"] = st.num_seeds;
  o["peers"] = std::max(0, st.num_peers - st.num_seeds);
  o["isPrivate"] = t.ti && t.ti->priv();
  o["savePath"] = st.save_path;
  o["sequential"] = bool(st.flags & lt::torrent_flags::sequential_download);
  o["addedAt"] = t.addedAtMs;
  if (st.errc) {
    json::object e;
    e["code"] = errorCode(st.errc);
    std::string message = st.errc.message();
    if (st.error_file >= lt::file_index_t{0} && t.ti &&
        st.error_file < t.ti->layout().end_file()) {
      message += ": " + t.ti->layout().file_path(st.error_file);
    }
    e["message"] = message;
    o["error"] = std::move(e);
  } else {
    o["error"] = nullptr;
  }
  return o;
}

json::object Engine::pendingStatusJson(Pending const& p) const {
  json::object o;
  bool known = !p.fetching && p.atp.ti;
  o["tid"] = p.tid;
  o["name"] = known ? p.atp.ti->name() : p.atp.name;
  o["state"] = p.fetching ? "fetching-metadata" : "pending-confirm";
  o["progress"] = 0;
  o["size"] = known ? p.atp.ti->total_size() : 0;
  o["downloaded"] = 0;
  o["uploaded"] = 0;
  o["downKBps"] = 0;
  o["upKBps"] = 0;
  o["etaSeconds"] = nullptr;
  o["ratio"] = 0;
  o["seeds"] = -1;
  o["peers"] = -1;
  o["isPrivate"] = known && p.atp.ti->priv();
  o["savePath"] = "";
  o["sequential"] = false;
  o["addedAt"] = p.addedAtMs;
  o["error"] = nullptr;
  return o;
}

std::string Engine::listenSpec() const {
  std::string port = std::to_string(port_);
  if (config_.bindInterface) {
    return *config_.bindInterface + ":" + port;
  }
  return "0.0.0.0:" + port + ",[::]:" + port;
}

void Engine::ensureListening() {
  if (listening_) {
    return;
  }
  if (port_ == 0) {
    port_ = config_.listenPort
                ? config_.listenPort
                : 20000 + static_cast<int>(arc4random_uniform(40000));
  }
  lt::settings_pack s;
  s.set_str(sp::listen_interfaces, listenSpec());
  session_->apply_settings(std::move(s));
  listening_ = true;
  listenAnnounced_ = false;
}

void Engine::refreshNetwork() {
  bool fetching = anyFetching();
  bool active = anyActive();
  bool publicActive = anyPublicActive();
  bool wantListen = fetching || active;
  bool wantDht = config_.dht && (fetching || publicActive);
  bool wantLsd = config_.lpd && publicActive;
  bool wantMapping = config_.portMapping && wantListen;
  if (wantListen && !listening_) {
    ensureListening();
  }
  lt::settings_pack s;
  bool changed = false;
  if (!wantListen && listening_) {
    s.set_str(sp::listen_interfaces, "");
    if (listenAnnounced_) {
      emit_(event("closed"));
    }
    listenAnnounced_ = false;
    listening_ = false;
    changed = true;
  }
  if (wantDht != dhtOn_) {
    s.set_bool(sp::enable_dht, wantDht);
    dhtOn_ = wantDht;
    changed = true;
  }
  if (wantLsd != lsdOn_) {
    s.set_bool(sp::enable_lsd, wantLsd);
    lsdOn_ = wantLsd;
    changed = true;
  }
  if (wantMapping != mappingOn_) {
    s.set_bool(sp::enable_upnp, wantMapping);
    s.set_bool(sp::enable_natpmp, wantMapping);
    mappingOn_ = wantMapping;
    changed = true;
  }
  if (changed) {
    session_->apply_settings(std::move(s));
  }
}

void Engine::updatePower(bool active) {
  if (active && !powerAssertion_) {
    IOPMAssertionID id = 0;
    if (IOPMAssertionCreateWithName(kIOPMAssertPreventUserIdleSystemSleep,
                                    kIOPMAssertionLevelOn,
                                    CFSTR("M6E BitTorrent transfer"),
                                    &id) == kIOReturnSuccess) {
      powerAssertion_ = id;
    } else {
      std::fprintf(stderr, "m6e-torrent: no power assertion\n");
    }
  } else if (!active && powerAssertion_) {
    IOPMAssertionRelease(*powerAssertion_);
    powerAssertion_.reset();
  }
}

void Engine::processAlerts() {
  char drain[256];
  while (::read(wakeRead_, drain, sizeof drain) > 0) {
  }
  std::vector<lt::alert*> alerts;
  session_->pop_alerts(&alerts);
  for (auto* a : alerts) {
    handleAlert(a);
  }
}

void Engine::pumpUntil(std::function<bool()> const& done,
                       Clock::duration timeout) {
  auto deadline = Clock::now() + timeout;
  while (!done()) {
    auto now = Clock::now();
    if (now >= deadline) {
      return;
    }
    session_->wait_for_alert(std::chrono::duration_cast<lt::time_duration>(
        std::min<Clock::duration>(deadline - now, 100ms)));
    processAlerts();
  }
}

Engine::Torrent* Engine::byHandle(lt::torrent_handle const& h) {
  auto hit = handles_.find(h);
  if (hit == handles_.end()) {
    return nullptr;
  }
  auto it = torrents_.find(hit->second);
  return it == torrents_.end() ? nullptr : &it->second;
}

void Engine::handleAlert(lt::alert* a) {
  if (auto* su = lt::alert_cast<lt::state_update_alert>(a)) {
    for (auto const& st : su->status) {
      if (Torrent* t = byHandle(st.handle)) {
        t->status = st;
      }
    }
    bool active = anyActive();
    if ((active || lastProgressActive_) &&
        Clock::now() - lastProgress_ >= kProgressMinGap) {
      emitProgress();
      lastProgressActive_ = active;
    }
    return;
  }
  if (auto* mr = lt::alert_cast<lt::metadata_received_alert>(a)) {
    onMetadata(mr->handle);
    return;
  }
  if (auto* tf = lt::alert_cast<lt::torrent_finished_alert>(a)) {
    if (Torrent* t = byHandle(tf->handle)) {
      onFinished(*t);
    }
    return;
  }
  if (auto* cf = lt::alert_cast<lt::cache_flushed_alert>(a)) {
    if (Torrent* t = byHandle(cf->handle)) {
      onFlushed(*t);
    }
    return;
  }
  if (auto* sr = lt::alert_cast<lt::save_resume_data_alert>(a)) {
    outstandingSaves_ = std::max(0, outstandingSaves_ - 1);
    if (Torrent* t = byHandle(sr->handle)) {
      writeResume(*t, sr->params);
    }
    return;
  }
  if (lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
    outstandingSaves_ = std::max(0, outstandingSaves_ - 1);
    return;
  }
  if (auto* td = lt::alert_cast<lt::torrent_deleted_alert>(a)) {
    deleting_.erase(tidOf(td->info_hashes));
    return;
  }
  if (auto* tdf = lt::alert_cast<lt::torrent_delete_failed_alert>(a)) {
    std::string tid = tidOf(tdf->info_hashes);
    deleting_.erase(tid);
    emitError(tid, "io-error", "could not delete the files: " + tdf->error.message());
    return;
  }
  if (auto* fe = lt::alert_cast<lt::file_error_alert>(a)) {
    if (Torrent* t = byHandle(fe->handle)) {
      emitError(t->tid, errorCode(fe->error),
                fe->error.message() + ": " + fe->filename());
    }
    return;
  }
  if (auto* te = lt::alert_cast<lt::torrent_error_alert>(a)) {
    if (Torrent* t = byHandle(te->handle)) {
      emitError(t->tid, errorCode(te->error), te->error.message());
    }
    return;
  }
  if (auto* ls = lt::alert_cast<lt::listen_succeeded_alert>(a)) {
    if (listening_ && !listenAnnounced_ && ls->socket_type == lt::socket_type_t::tcp) {
      listenAnnounced_ = true;
      json::object e = event("listening");
      e["port"] = ls->port;
      emit_(e);
    }
    return;
  }
  if (auto* lf = lt::alert_cast<lt::listen_failed_alert>(a)) {
    if (listening_) {
      log("warn", "cannot listen on " + std::string(lf->listen_interface()) +
                      ": " + lf->error.message());
    }
    return;
  }
  if (auto* ta = lt::alert_cast<lt::tracker_reply_alert>(a)) {
    if (Torrent* t = byHandle(ta->handle)) {
      t->trackersDirty = true;
    }
    return;
  }
  if (auto* ta = lt::alert_cast<lt::tracker_error_alert>(a)) {
    if (Torrent* t = byHandle(ta->handle)) {
      t->trackersDirty = true;
    }
    return;
  }
  if (auto* ta = lt::alert_cast<lt::tracker_warning_alert>(a)) {
    if (Torrent* t = byHandle(ta->handle)) {
      t->trackersDirty = true;
    }
    return;
  }
}

void Engine::onFinished(Torrent& t) {
  refreshStatus(t);
  if (t.finishedSeen || t.flushing) {
    return;
  }
  t.flushing = true;
  t.handle.flush_cache();
}

void Engine::onFlushed(Torrent& t) {
  if (!t.flushing) {
    return;
  }
  t.flushing = false;
  t.finishedSeen = true;
  refreshStatus(t);
  if (t.ti) {
    auto const& fs = t.ti->layout();
    auto priorities = t.handle.get_file_priorities();
    std::string root = t.status.save_path;
    for (auto i : fs.file_range()) {
      auto k = static_cast<std::size_t>(static_cast<int>(i));
      if (fs.pad_file_at(i) || k >= priorities.size() ||
          priorities[k] == lt::dont_download) {
        continue;
      }
      auto parts = splitPath(fs.file_path(i));
      if (checkNoSymlinkEscape(root, parts)) {
        continue;
      }
      std::string full = root + "/" + fs.file_path(i);
      struct stat st;
      if (lstat(full.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
          (st.st_mode & 0111)) {
        fchmodat(AT_FDCWD, full.c_str(), st.st_mode & 07666,
                 AT_SYMLINK_NOFOLLOW);
      }
    }
  }
  requestResume(t);
  json::object e = event("completed");
  e["tid"] = t.tid;
  emit_(e);
}

void Engine::emitProgress() {
  json::array list;
  for (auto const& [tid, t] : torrents_) {
    list.push_back(statusJson(t));
  }
  json::object e = event("progress");
  e["torrents"] = std::move(list);
  emit_(e);
  lastProgress_ = Clock::now();
}

void Engine::emitTrackerAndPeerChanges() {
  for (auto& [tid, t] : torrents_) {
    if (t.trackersDirty) {
      t.trackersDirty = false;
      json::object e = event("trackers");
      e["tid"] = tid;
      e["trackers"] = trackersJson(t.handle);
      emit_(e);
    }
    int seeds = t.status.num_seeds;
    int peers = std::max(0, t.status.num_peers - t.status.num_seeds);
    if (seeds != t.lastSeeds || peers != t.lastPeers) {
      bool first = t.lastSeeds == -2;
      t.lastSeeds = seeds;
      t.lastPeers = peers;
      if (!first || seeds || peers) {
        json::object e = event("peers");
        e["tid"] = tid;
        e["seeds"] = seeds;
        e["peers"] = peers;
        emit_(e);
      }
    }
  }
}

void Engine::emitError(std::string const& tid, std::string const& code,
                       std::string const& message) {
  json::object e = event("error");
  if (!tid.empty()) {
    e["tid"] = tid;
  }
  e["code"] = code;
  e["message"] = message;
  emit_(e);
}

void Engine::log(std::string const& level, std::string const& message) {
  std::fprintf(stderr, "m6e-torrent: %s: %s\n", level.c_str(), message.c_str());
  json::object e = event("log");
  e["level"] = level;
  e["message"] = message;
  emit_(e);
}

std::string Engine::resumePath(std::string const& tid) const {
  return options_.profileDir + "/" + tid + ".resume";
}

bool Engine::writeResume(Torrent const& t, lt::add_torrent_params const& atp) {
  if (!atp.ti) {
    return false;
  }
  lt::entry e = lt::write_resume_data(atp);
  auto& state = e["m6e"];
  state["addedAtMs"] = t.addedAtMs;
  state["paused"] = t.userPaused ? 1 : 0;
  state["completed"] = t.completed ? 1 : 0;
  state["finished"] = t.finishedSeen ? 1 : 0;
  std::vector<char> buf;
  lt::bencode(std::back_inserter(buf), e);
  std::string path = resumePath(t.tid);
  std::string tmp = path + ".tmp";
  int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC,
                0600);
  if (fd < 0) {
    return false;
  }
  std::size_t off = 0;
  bool ok = true;
  while (off < buf.size()) {
    ssize_t n = ::write(fd, buf.data() + off, buf.size() - off);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      ok = false;
      break;
    }
    off += static_cast<std::size_t>(n);
  }
  ok = ok && fsync(fd) == 0;
  ok = close(fd) == 0 && ok;
  if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
    unlink(tmp.c_str());
    return false;
  }
  return true;
}

void Engine::requestResume(Torrent& t) {
  t.handle.save_resume_data(lt::torrent_handle::save_info_dict);
  outstandingSaves_++;
}

void Engine::deleteResume(std::string const& tid) {
  unlink(resumePath(tid).c_str());
  unlink((resumePath(tid) + ".tmp").c_str());
}

void Engine::applySeedGoals() {}

void Engine::watchBoundInterface() {}

void Engine::tick(Clock::time_point now) {
  nextTick_ = now + kTick;
  bool active = anyActive();
  if (active || lastProgressActive_) {
    session_->post_torrent_updates(lt::torrent_handle::query_save_path);
  }
  emitTrackerAndPeerChanges();
  if (now >= nextResumeSave_) {
    nextResumeSave_ = now + kResumeSaveInterval;
    for (auto& [tid, t] : torrents_) {
      if (t.handle.need_save_resume_data()) {
        requestResume(t);
      }
    }
  }
  applySeedGoals();
  watchBoundInterface();
  refreshNetwork();
  updatePower(active);
  bool busy = active || !pending_.empty();
  if (busy) {
    idleSince_.reset();
  } else if (!idleSince_) {
    idleSince_ = now;
  }
}

void Engine::noteRequest(Clock::time_point now) {
  if (idleSince_) {
    idleSince_ = now;
  }
}

bool Engine::idleExpired(Clock::time_point now) const {
  return options_.idleExit.count() > 0 && idleSince_ &&
         now - *idleSince_ >= options_.idleExit;
}

void Engine::shutdown(Clock::time_point saveBy) {
  for (auto& [tid, t] : torrents_) {
    if (t.handle.is_valid() && t.handle.need_save_resume_data()) {
      requestResume(t);
    }
  }
  pumpUntil([&] { return outstandingSaves_ <= 0; },
            std::max<Clock::duration>(saveBy - Clock::now(), 0ms));
  updatePower(false);
  session_->set_alert_notify({});
  lt::session_proxy proxy = session_->abort();
  session_.reset();
}

}  // namespace m6e
