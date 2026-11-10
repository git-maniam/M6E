# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## M6E: the BitTorrent section of Settings (Phase 8, P8.5, D33 to D37).

pane-bittorrent-title = BitTorrent
    .title = BitTorrent

pane-bittorrent-section =
    .heading = BitTorrent

## Privacy defaults

bittorrent-privacy-group =
    .label = Private by default

bittorrent-privacy-note =
    .heading = Nothing runs until you add a torrent
    .message = No torrent engine runs, no port is open and no network traffic is sent until you add a torrent and click Add. Port mapping and local network discovery are off. Peer discovery (DHT and peer exchange) is used only for public torrents, never for private ones. Finished files are marked as downloaded from the internet, so macOS checks them before you open them.

bittorrent-privacy-visibility =
    .heading = Peers can see your IP address
    .message = The Privacy Shield and the Tracker Blocker do not apply to torrent traffic. To hide your address, use a proxy or bind torrents to your VPN below. Encryption does not make you anonymous.

## General

bittorrent-general-group =
    .label = General

bittorrent-enabled =
    .label = Show the BT button
    .description = Turn this off to hide the button and keep the BitTorrent engine from ever starting.

bittorrent-resume-at-startup =
    .label = Resume torrents when { -brand-short-name } starts
    .description = Torrents that were active when you quit continue. Nothing starts if you have no torrents.

bittorrent-confirm-quit =
    .label = Ask before quitting while torrents are active

bittorrent-default-app =
    .label = Make { -brand-short-name } the default app for .torrent files and magnet links
    .description = { -brand-short-name } never does this on its own. macOS may ask you to confirm each change.

bittorrent-default-app-done =
    .message = { -brand-short-name } is the default app for .torrent files and magnet links.

bittorrent-default-app-failed =
    .message = macOS did not make { -brand-short-name } the default app. You can still open a torrent with Open With in Finder.

## Downloads

bittorrent-downloads-group =
    .label = Adding torrents

bittorrent-download-folder =
    .label = Default download folder
    .description = The Add torrent window starts with this folder. Until you choose one, it uses the download folder of { -brand-short-name }.
    .dialogtitle = Choose the default folder for torrents

bittorrent-download-folder-reset =
    .label = Use the { -brand-short-name } download folder

bittorrent-ask-where =
    .label = Ask where to save every torrent
    .description = The Add torrent window opens the folder chooser first.

bittorrent-sequential-default =
    .label = Download in order by default
    .description = Pieces arrive from the start of each file, which helps with playing while downloading but is slower overall.

bittorrent-start-now =
    .label = Start downloading when I click Add
    .description = When this is off, the torrent is added paused.

## Seeding

bittorrent-seeding-group =
    .label = Seeding
    .description = What happens after a download finishes.

bittorrent-seed-mode =
    .label = Stop seeding

bittorrent-seed-ratio-option =
    .label = At a share ratio

bittorrent-seed-ratio =
    .label = Share ratio
    .description = Uploaded divided by downloaded. 1.0 means you upload as much as you downloaded.

bittorrent-seed-hours-option =
    .label = After a number of hours

bittorrent-seed-hours =
    .label = Hours

bittorrent-seed-never-option =
    .label = Never stop

## Speed and queue

bittorrent-speed-group =
    .label = Speed limits and queue
    .description = Limits are in kilobytes per second. Zero means no limit.

bittorrent-down-limit =
    .label = Download limit (KB/s)

bittorrent-up-limit =
    .label = Upload limit (KB/s)

bittorrent-turtle =
    .label = Turtle mode
    .description = Use the lower limits below instead. There is no schedule: Turtle mode stays on or off until you change it.

bittorrent-turtle-down-limit =
    .label = Turtle download limit (KB/s)

bittorrent-turtle-up-limit =
    .label = Turtle upload limit (KB/s)

bittorrent-max-active-downloads =
    .label = Active downloads at a time
    .description = The rest wait in the queue. Zero means no limit.

bittorrent-max-active-torrents =
    .label = Active torrents at a time
    .description = Downloads and seeds together. Zero means no limit.

## Connections

bittorrent-connections-group =
    .label = Connections and discovery

bittorrent-encryption =
    .label = Encryption
    .description = Encryption hides BitTorrent from simple traffic shaping. It does not make you anonymous.

bittorrent-encryption-disabled =
    .label = Disabled

bittorrent-encryption-enabled =
    .label = Enabled (use it when the peer does too)

bittorrent-encryption-forced =
    .label = Forced (refuse peers that do not encrypt)

bittorrent-dht =
    .label = Find peers with DHT
    .description = Public torrents only. Private torrents never use it.

bittorrent-pex =
    .label = Find peers through other peers (peer exchange)
    .description = Public torrents only. Private torrents never use it.

bittorrent-lpd =
    .label = Find peers on the local network
    .description = This announces you to the local network, and macOS may ask for Local Network permission. Off by default.

bittorrent-port-mapping =
    .label = Open a port on my router automatically (UPnP and NAT-PMP)
    .description = This opens your home router to the internet. Off by default. Without it, other peers can reach you only if you forward the port yourself, and your own downloads still work.

bittorrent-listen-port =
    .label = Listening port
    .description = Zero picks a random port every time the engine starts.

bittorrent-randomize-peer-id =
    .label = Use a new random peer ID in every session
    .description = The peer ID never names { -brand-short-name } or its version, whatever you choose here.

## Proxy

bittorrent-proxy-group =
    .label = Proxy
    .description = A separate setting from the network settings of { -brand-short-name }.

bittorrent-proxy-enabled =
    .label = Connect through a SOCKS5 proxy

bittorrent-proxy-host =
    .label = Host

bittorrent-proxy-port =
    .label = Port

bittorrent-proxy-username =
    .label = User name

bittorrent-proxy-password =
    .label = Password
    .description = Kept in the { -brand-short-name } login manager, never in a setting.

bittorrent-proxy-password-saved =
    .label = Password
    .description = A password is saved in the { -brand-short-name } login manager. Type here to replace it.

bittorrent-proxy-password-clear =
    .label = Remove the saved password

bittorrent-proxy-scope =
    .label = Send through the proxy

bittorrent-proxy-scope-all =
    .label = Trackers and peers
    .description = Your address stays hidden from the swarm.

bittorrent-proxy-scope-trackers =
    .label = Trackers only
    .description = Peers connect to you directly and see your IP address.

bittorrent-proxy-failclosed =
    .heading = Fails closed
    .message = The proxy resolves host names, so no DNS request leaves your Mac for a torrent. Nothing bypasses it: if the proxy cannot be used, or { -brand-short-name } cannot apply these settings, torrents stop instead of connecting directly.

## Network interface

bittorrent-interface-group =
    .label = Network interface
    .description = Keep torrents off your normal connection by tying them to your own VPN.

bittorrent-interface-enabled =
    .label = Only use one network interface (kill switch)
    .description = If the interface goes away, every torrent connection closes and none opens until it returns.

bittorrent-interface-name =
    .label = Interface name
    .placeholder = utun3
    .description = The device of your VPN, as shown by ifconfig in Terminal (for example utun3 or a WireGuard device).

bittorrent-interface-empty =
    .heading = No interface chosen
    .message = Until you enter an interface name, torrent traffic is not restricted.

# Variables:
#   $interface (String) - the name of the network interface
bittorrent-interface-down =
    .heading = The interface { $interface } is down
    .message = All torrent connections are closed and none will open until it returns.

## IP filter

bittorrent-ipfilter-group =
    .label = IP filter

bittorrent-ipfilter-path =
    .label = Block list file
    .description = A PeerGuardian .p2p or eMule ipfilter.dat file. { -brand-short-name } does not include or download any list.
    .placeholder = No file chosen

bittorrent-ipfilter-choose =
    .label = Choose file…

bittorrent-ipfilter-picker-title = Choose an IP block list

bittorrent-ipfilter-clear =
    .label = Remove the filter
