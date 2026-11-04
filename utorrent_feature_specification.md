# Product Specification: Ultra-Lightweight BitTorrent Client ("MicroTorrent Clone")

> **Target Audience:** AI Coding Agents (Claude Code, Google Antigravity, Cursor, Codex)  
> **Purpose:** Blueprint for vibe coding a clean, high-performance, resource-efficient BitTorrent client inspired by classic uTorrent (v2.2.1 simplicity with modern protocol standards).

---

## 1. Project Vision & Architecture Principles

- **Footprint:** Ultra-low CPU and RAM footprint (<30MB idle RAM target).
- **Architecture:** Clean decoupling between:
  1. **Core Engine:** Asynchronous peer-to-peer daemon/service.
  2. **Storage Layer:** High-speed cached disk I/O with sparse allocation.
  3. **Management API:** IPC/RPC/REST interface.
  4. **Frontend UI:** Lightweight native or embedded UI (e.g., Tauri/Rust, Qt, or modern WebUI).
- **Zero Bloatware:** Strictly no embedded advertisements, sponsored installers, or bundled toolbars.

---

## 2. BitTorrent Protocol Specifications (BEP Support)

The client must implement modern BitTorrent Enhancement Proposals (BEPs):

### 2.1 Core Protocols
- **BEP 03 (BitTorrent Protocol):** Core peer wire protocol, tracker HTTP scraping, handshake, piece verification via SHA-1/SHA-256.
- **BEP 09 & BEP 10 (Extension Protocol):** Metadata transfer (`ut_metadata`), custom peer messages, capability negotiation.
- **BEP 53 (Magnet URI Format):** Seamless resolution of `magnet:?xt=urn:btih:...` links including tracker (`tr=`) and display name (`dn=`) parameters.
- **BEP 15 (UDP Tracker Protocol):** Compact tracker announcements over UDP to reduce server load.
- **BEP 19 (WebSeed / HTTP-FTP Seeding):** GetRight-style HTTP seeding for hybrid torrent distribution.
- **BEP 52 (BitTorrent v2):** Dual/hybrid swarm support utilizing Merkle trees and SHA-256 file hashing.

### 2.2 Decentralization & Peer Discovery
- **BEP 05 (Mainline DHT):** Kademlia distributed hash table for trackerless swarm discovery.
- **BEP 11 (Peer Exchange / PEX):** Decentralized peer-to-peer discovery (`ut_pex`) within active swarms.
- **BEP 26 (Local Peer Discovery / LPD):** Multicast discovery of peers on the local subnet (LAN) for zero-ISP-bandwidth LAN transfers.

### 2.3 Congestion Control
- **BEP 29 (uTP - Micro Transport Protocol):** LEDBAT-based congestion control running over UDP to automatically throttle BitTorrent traffic when background latency rises (prevents home router choking).

---

## 3. Comprehensive Security & Privacy Features

This section defines the security baseline required to prevent ISP throttling, mitigate peer surveillance, prevent leaks, and secure remote interfaces.

### 3.1 Network Traffic Obfuscation & Encryption
- **Message Stream Encryption (PE/MSE):**
  - **Modes:**
    - *Disabled:* Plaintext connections only.
    - *Enabled:* Negotiate encryption with peers that support it; accept legacy unencrypted peers.
    - *Forced:* Reject any peer unable or unwilling to negotiate encrypted connections.
  - **DPI Mitigation:** RC4 stream encryption with random padding to thwart ISP Deep Packet Inspection (DPI) and protocol fingerprinting.
  - **Header/Payload Encryption:** Encrypt headers only or full stream (headers + payload).

### 3.2 Proxy & VPN Integration
- **SOCKS5 Support:** Full SOCKS5 proxy support with authentication (username/password).
- **SOCKS4 / HTTP Proxy Support:** Fallback proxy handling for tracker-only lookups.
- **Proxy Routing Modes:**
  - Route tracker communications only.
  - Route peer-to-peer traffic and tracker communications.
  - Route DHT/PEX routing table lookups through proxy.
- **DNS Leak Protection:** Force all hostname resolution to execute on the remote proxy server (preventing local ISP DNS logging).
- **Kill-Switch / Network Interface Binding:**
  - Capability to bind the client socket engine strictly to a designated network interface (e.g., `tun0`, `wg0`, or specific VPN adapter GUID).
  - Immediate packet dropping and connection termination if the bound interface drops.

### 3.3 Peer Filtering & Privacy Controls
- **IP Filter Engine (`ipfilter.dat`):**
  - Parse standard IP range blocking files (e.g., PeerGuardian, eMule format).
  - Memory-efficient interval tree or trie-based IP lookup.
  - Auto-block known anti-piracy crawlers, malicious ranges, and spam nodes.
- **Private Swarm Flag Enforcement (BEP 27):**
  - Strict inspection of the `private: 1` info-dict flag.
  - Automatically and permanently disable DHT, PEX, and LPD for any torrent marked private to prevent swarm leakages.
- **Client Identity Anonymization (Anti-Fingerprinting):**
  - Configurable Peer-ID prefix (option to randomize Peer ID strings per session).
  - Sanitize HTTP/HTTPS Tracker User-Agent headers (prevent tracker-side OS fingerprinting).

### 3.4 WebUI & Remote Access Hardening
- **Authentication & Sessions:**
  - Mandatory password protection with Argon2id / bcrypt password hashing.
  - Session tokens with strict expiry and rotation.
- **Cross-Site Protection:**
  - CSRF token validation on every mutating POST/PUT/DELETE request.
  - Strict CORS origin whitelisting.
- **Brute Force Protection:**
  - Automatic IP ban after $N$ failed authentication attempts (e.g., 5 attempts in 3 minutes $\rightarrow$ 60-minute ban).
- **Encrypted Transmission:**
  - Integrated HTTPS/TLS listener with automatic self-signed certificate generation or user-supplied certificates.
- **Localhost & Subnet Whitelisting:**
  - Option to reject any WebUI connection originating outside `127.0.0.1` or designated private subnets.

### 3.5 File Integrity & Host System Safeguards
- **Block-level Cryptographic Validation:** Constant verification of downloaded blocks against the `.torrent` SHA-1/SHA-256 hashes before writing to disk.
- **Anti-Zip-Bomb & Malformed File Protections:** Inspect `.torrent` metadata structure to reject path traversal attempts (e.g., `../../etc/passwd` or `C:\Windows\System32` directory traversal).
- **Post-Download Antivirus Integration:** Configurable trigger to automatically run the local OS anti-malware command (e.g., Windows Defender CLI or ClamAV) on downloaded files upon completion.

---

## 4. Bandwidth, Queuing & Network Engine

### 4.1 Traffic Shaping & Scheduling
- **Global & Per-Torrent Speed Limits:** Distinct, independent limits for upload and download rates ($KB/s$ or $MB/s$).
- **Dynamic Scheduler Grid:**
  - 24/7 calendar grid (hourly increments) with 4 operational states:
    1. *Full Speed*
    2. *Throttled / Limited Speed*
    3. *Seeding Only*
    4. *Turn Off / Stop All*
- **Alternative Speed Limits:** Quick toggle (Turtle mode) for instant temporary rate limiting.

### 4.2 Port Management & Connectivity
- **Port Forwarding Protocols:**
  - UPnP (Universal Plug and Play) IGD.
  - NAT-PMP / PCP (Port Control Protocol).
- **Random Port on Startup:** Toggle to randomize the incoming listening port across a specified range on every launch.
- **Dual-Stack IP:** Full simultaneous IPv4 and IPv6 routing and tracker exchange.

### 4.3 Queue Management
- **Maximum Active Downloads:** Limit concurrent downloads to optimize disk writes and upload trade efficiency.
- **Maximum Active Torrents:** Global cap on active seeds + downloads.
- **Seed Ratio Enforcement:**
  - Set default seeding goals (e.g., stop seeding when Ratio reaches $1.5$ or time reaches $X$ hours).
  - Alternative upload limit once seeding ratio is reached.

---

## 5. Storage & Disk I/O Management

### 5.1 Disk Allocation & Caching
- **Intelligent Memory Caching:**
  - Configurable RAM cache for disk reads and writes (reduces physical HDD/SSD thrashing).
  - Flush writes periodically and consolidate blocks into sequential writes.
  - Memory cache read-ahead for active uploads.
- **Pre-allocation Algorithms:**
  - *Sparse Files:* Fast start, minimal initial allocation.
  - *Zero-Fill Pre-allocation:* Guarantees disk space reservation upfront to prevent "Disk Full" errors midway.

### 5.2 Granular Content Selection
- **Selective File Downloading:**
  - Expand multi-file torrents to view file tree.
  - Check/uncheck individual files to download or skip (`.pad` file auto-hiding).
  - Priority levels: *High*, *Normal*, *Low*, *Do Not Download*.
- **Sequential Downloading (Streaming Mode):**
  - Download pieces strictly from head-to-tail for media files to enable previewing video/audio before the download finishes.

---

## 6. User Interface & User Experience (Desktop & Web)

### 6.1 Layout & Views
- **Main Torrent Table:** Columns for Name, Size, Done %, Status, Down Speed, Up Speed, ETA, Ratio, Seeds/Peers, Added Date.
- **Detail Pane Tabs:**
  - **General:** Transfer metrics, hash, piece size, path, completion estimate.
  - **Trackers:** Tracker URL list, status (Working, Updating, Error), scrape interval, seed/peer count.
  - **Peers:** Connected client IP/Host, flag/country, client software signature, encryption status, down/up speeds, progress %.
  - **Pieces:** Visual piece map/grid displaying downloaded, verified, and in-flight pieces.
  - **Files:** Tree/list of contained files with individual download priorities and progress bars.
  - **Speed Graph:** Real-time visual timeline chart of upload and download bandwidth.
  - **Logger:** Live diagnostics and event logs (Debug, Info, Warn, Error).
- **Sidebar Categories:** Filter by State (All, Downloading, Seeding, Completed, Active, Inactive, Errored) and Custom Labels/Tags.

### 6.2 System Integration
- **Tray Icon & Background Mode:** Minimize to System Tray, display download tooltip, tray menu controls.
- **OS File Association:** Register `.torrent` files and `magnet:` URL handler with the operating system.
- **Power Management:**
  - Prevent system sleep/hibernation while downloads are active.
  - Auto-shutdown, sleep, or hibernate computer when all downloads finish.

---

## 7. Automation, Remote Access & CLI

### 7.1 RSS Feed Downloader
- **Subscription Management:** Add multiple RSS/Atom feeds with custom refresh intervals.
- **Smart Filter Rules:**
  - Regex-based matching on episode names, qualities (e.g., `1080p|2160p`), codecs (e.g., `AV1|x265`).
  - Minimum/maximum file size limits.
  - Episode filtering (e.g., smart filter `S01E01-E12`).

### 7.2 Automation Triggers
- **Watch Folders:** Directory polling for dropped `.torrent` files to auto-load (with option to delete or move `.torrent` after import).
- **Run External Program:** Trigger shell script / executable when a torrent finishes:
  - Supported parameters passed as CLI args: `%N` (Name), `%D` (Directory), `%F` (Filename), `%H` (Hash).

---

## 8. Suggested Tech Stack for Vibe Coding

| Layer | Recommended Options | Rationale |
| :--- | :--- | :--- |
| **Backend Core** | **Rust** (`librqbit` or Tokio custom) or **Go** (`anacrolix/torrent`) | Memory-safe, concurrency-friendly, zero garbage collection pauses (Rust), minimal footprint. |
| **Desktop Wrapper** | **Tauri (v2)** or **PyQt6 / C++ Qt6** | Tauri leverages OS native webview, keeping binary size under 15MB. |
| **Frontend UI** | **React / Solid.js / Svelte** + **Tailwind CSS** | Fast state synchronization, virtualized data tables for 10,000+ torrents. |
| **Storage / State** | **SQLite** or **sled** (embedded key-value) | Resilient session restoration across application restarts. |

---

## 9. Phased Implementation Roadmap for AI Agents

When coding with Claude Code or Antigravity, prompt the agent through these phases sequentially:

### Phase 1: Core Torrent Engine & Ingest
- Implement Bencode parser/encoder.
- Build Magnet link resolver and `.torrent` file parser.
- Establish basic tracker client (HTTP/UDP) and peer wire handshake.

### Phase 2: Piece Exchange, Storage & Verification
- Build piece storage manager with SHA-1/SHA-256 chunk validation.
- Implement Rarest-First piece-picking algorithm and End-Game mode.
- Establish download/upload rate limiter with token bucket algorithm.

### Phase 3: Security & Network Hardening
- Implement Protocol Encryption (PE/MSE) via RC4 handshake.
- Integrate SOCKS5 client proxy tunnel for peer and tracker connections.
- Implement `ipfilter.dat` parser with binary search IP range lookups.
- Add network interface binding (kill-switch logic).

### Phase 4: Extended Protocols (DHT, PEX, uTP)
- Add Kademlia DHT node implementation (BEP 05).
- Add Peer Exchange (PEX - BEP 11).
- Add uTP socket handler (BEP 29).

### Phase 5: UI & Management API
- Build JSON-RPC / REST API for client-daemon communication.
- Implement desktop GUI with virtualized torrent table, detail tabs, and system tray integration.
- Implement RSS automator and watch folder listener.