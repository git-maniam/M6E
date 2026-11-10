/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include "json.hpp"

namespace m6e {

using Clock = std::chrono::steady_clock;

struct EngineOptions {
  std::string profileDir;
  std::chrono::seconds idleExit{60};
};

// The libtorrent session and every torrent in it. Single-threaded: all calls
// come from the main loop; libtorrent only wakes it through wakeFd().
class Engine {
 public:
  using Emit = std::function<void(json::object const&)>;

  Engine(EngineOptions options, Emit emit);
  ~Engine();
  Engine(Engine const&) = delete;
  Engine& operator=(Engine const&) = delete;

  static std::string engineName();

  // Readable when libtorrent has alerts; then call processAlerts().
  int wakeFd() const { return wakeRead_; }

  // Runs one protocol operation (not "shutdown"); throws RequestError.
  json::object handle(std::string const& op, json::object const& args);

  void processAlerts();
  void tick(Clock::time_point now);
  Clock::time_point nextTick() const { return nextTick_; }
  void noteRequest(Clock::time_point now);
  bool idleExpired(Clock::time_point now) const;

  // Saves resume data (until saveBy at the latest) and closes the session.
  void shutdown(Clock::time_point saveBy);

 private:
  struct Config {
    std::string encryption = "enabled";
    bool dht = false;
    bool pex = false;
    bool lpd = false;
    bool portMapping = false;
    int listenPort = 0;
    std::optional<std::string> bindInterface;
    std::optional<std::string> peerIdPrefix;
    std::optional<std::string> userAgent;
    int maxConnections = 0;
  };

  struct Limits {
    int downKBps = 0;
    int upKBps = 0;
    bool turtle = false;
    int turtleDownKBps = 50;
    int turtleUpKBps = 10;
    int maxActiveDownloads = 3;
    int maxActiveTorrents = 5;
  };

  // Added but not confirmed: a parsed .torrent, or a magnet link whose
  // metadata is being fetched (then `handle` is valid) or has arrived.
  struct Pending {
    std::string tid;
    lt::add_torrent_params atp;
    lt::torrent_handle handle;
    bool fetching = false;
    json::object metadata;
    std::int64_t addedAtMs = 0;
  };

  struct Torrent {
    std::string tid;
    lt::torrent_handle handle;
    std::shared_ptr<lt::torrent_info const> ti;
    json::object metadata;
    lt::torrent_status status;
    std::int64_t addedAtMs = 0;
    bool userPaused = false;
    bool completed = false;
    bool finishedSeen = false;
    bool flushing = false;
    bool trackersDirty = false;
    int lastSeeds = -2;
    int lastPeers = -2;
  };

  json::object opAdd(json::object const& args);
  json::object addFile(std::string const& path);
  json::object addMagnet(std::string const& uri);
  json::object opConfirm(json::object const& args);
  json::object opCancel(json::object const& args);
  json::object opPause(json::object const& args, bool pause);
  json::object opPauseAll(bool pause);
  json::object opRemove(json::object const& args);
  json::object opList();
  json::object opDetail(json::object const& args);
  json::object opSetLimits(json::object const& args);
  json::object opSetConfig(json::object const& args);
  json::object opRestore(json::object const& args);

  lt::settings_pack baseSettings() const;
  void applyLimits();
  void applyIdentity(lt::settings_pack& sp) const;
  void applyEncryption(lt::settings_pack& sp) const;
  void ensureListening();
  void refreshNetwork();
  std::string listenSpec() const;
  void updatePower(bool active);
  void setPaused(Torrent& t, bool pause);
  void dropPending(std::string const& tid);
  void pumpUntil(std::function<bool()> const& done, Clock::duration timeout);

  void handleAlert(lt::alert* a);
  void onMetadata(lt::torrent_handle const& h);
  void onFinished(Torrent& t);
  void onFlushed(Torrent& t);
  void emitProgress();
  void emitTrackerAndPeerChanges();
  void emitError(std::string const& tid, std::string const& code,
                 std::string const& message);
  void log(std::string const& level, std::string const& message);

  bool writeResume(Torrent const& t, lt::add_torrent_params const& atp);
  void requestResume(Torrent& t);
  void deleteResume(std::string const& tid);
  std::string resumePath(std::string const& tid) const;

  // Extension points for P8.5 (seeding goals, queue, kill switch); called
  // once a second.
  void applySeedGoals();
  void watchBoundInterface();

  bool anyActive() const;
  bool anyFetching() const;
  bool anyPublicActive() const;
  bool isActive(Torrent const& t) const;
  std::string stateOf(Torrent const& t) const;
  json::object statusJson(Torrent const& t) const;
  json::object pendingStatusJson(Pending const& p) const;
  json::array trackersJson(lt::torrent_handle const& h) const;
  Torrent* byHandle(lt::torrent_handle const& h);
  Torrent& confirmed(std::string const& tid);
  void refreshStatus(Torrent& t);
  lt::torrent_flags_t privacyFlags(bool isPrivate) const;

  EngineOptions options_;
  Emit emit_;
  std::string pendingDir_;
  int wakeRead_ = -1;
  int wakeWrite_ = -1;
  std::unique_ptr<lt::session> session_;
  Config config_;
  Limits limits_;
  std::map<std::string, Pending> pending_;
  std::map<std::string, Torrent> torrents_;
  std::map<lt::torrent_handle, std::string> handles_;
  std::set<std::string> deleting_;
  int outstandingSaves_ = 0;

  bool listening_ = false;
  bool listenAnnounced_ = false;
  int port_ = 0;
  bool dhtOn_ = false;
  bool lsdOn_ = false;
  bool mappingOn_ = false;
  std::optional<std::uint32_t> powerAssertion_;

  Clock::time_point nextTick_;
  Clock::time_point nextResumeSave_;
  Clock::time_point lastProgress_;
  bool lastProgressActive_ = false;
  std::optional<Clock::time_point> idleSince_;
};

}  // namespace m6e
