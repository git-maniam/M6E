# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## M6E: the about:torrents page (Phase 8, D33). The state names, priority
## names and "Show in Finder" come from m6eTorrent.ftl.

m6e-torrents-page-title = Torrents
m6e-torrents-heading = Torrents

## Sidebar

m6e-torrents-filters =
    .aria-label = Filter torrents
m6e-torrents-filter-all = All
m6e-torrents-filter-downloading = Downloading
m6e-torrents-filter-seeding = Seeding
m6e-torrents-filter-completed = Completed
m6e-torrents-filter-paused = Paused
m6e-torrents-filter-active = Active
m6e-torrents-filter-inactive = Inactive
m6e-torrents-filter-errored = Errored

## Toolbar

m6e-torrents-toolbar =
    .aria-label = Actions for the selected torrent
m6e-torrents-pause = Pause
m6e-torrents-resume = Resume
m6e-torrents-remove = Remove…
m6e-torrents-open-folder = Open folder
m6e-torrents-copy-magnet = Copy magnet link
m6e-torrents-magnet-copied = Magnet link copied.
m6e-torrents-action-failed = That did not work.
# Variables:
#   $count (Number) - the number of torrents shown in the table
m6e-torrents-count =
    { $count ->
        [one] 1 torrent shown
       *[other] { $count } torrents shown
    }

## Table

m6e-torrents-table =
    .aria-label = Torrents
m6e-torrents-column-name = Name
m6e-torrents-column-size = Size
m6e-torrents-column-done = Done
m6e-torrents-column-status = Status
m6e-torrents-column-down = Down
m6e-torrents-column-up = Up
m6e-torrents-column-eta = ETA
m6e-torrents-column-ratio = Ratio
m6e-torrents-column-seeds = Seeds
m6e-torrents-column-peers = Peers
m6e-torrents-column-added = Added
m6e-torrents-empty = No torrents yet. Use the BT button next to the address bar to add a torrent file or a magnet link. Nothing runs until you add one.
m6e-torrents-no-match = No torrents match this filter.
m6e-torrents-disabled = BitTorrent is turned off in Settings.

## Details

m6e-torrents-detail-hint = Select a torrent to see its details.
m6e-torrents-detail-unavailable = The details of this torrent are not available now.
m6e-torrents-tabs =
    .aria-label = Torrent details
m6e-torrents-tab-general = General
m6e-torrents-tab-files = Files
m6e-torrents-tab-trackers = Trackers
m6e-torrents-tab-peers = Peers

m6e-torrents-general-name = Name
m6e-torrents-general-status = Status
m6e-torrents-general-progress = Progress
m6e-torrents-general-size = Size
m6e-torrents-general-downloaded = Downloaded
m6e-torrents-general-uploaded = Uploaded
m6e-torrents-general-ratio = Ratio
m6e-torrents-general-down = Download speed
m6e-torrents-general-up = Upload speed
m6e-torrents-general-eta = Time left
m6e-torrents-general-seeds = Seeds
m6e-torrents-general-peers = Peers
m6e-torrents-general-added = Added
m6e-torrents-general-path = Save location
m6e-torrents-general-hash = Hash
m6e-torrents-general-pieces = Pieces
m6e-torrents-general-private = Private torrent
m6e-torrents-general-sequential = Sequential download
m6e-torrents-general-error = Error
m6e-torrents-yes = Yes
m6e-torrents-no = No
# Variables:
#   $count (Number) - the number of pieces
#   $size (String) - the size of one piece, for example "1 MB"
m6e-torrents-pieces-value =
    { $count ->
        [one] 1 piece of { $size }
       *[other] { $count } pieces of { $size }
    }

m6e-torrents-files =
    .aria-label = Files in this torrent
m6e-torrents-files-column-name = Name
m6e-torrents-files-column-size = Size
m6e-torrents-files-column-progress = Progress
m6e-torrents-files-column-priority = Priority
m6e-torrents-files-empty = This torrent has no files to show.

m6e-torrents-trackers =
    .aria-label = Trackers of this torrent
m6e-torrents-trackers-column-url = Address
m6e-torrents-trackers-column-status = Status
m6e-torrents-trackers-column-seeds = Seeds
m6e-torrents-trackers-column-peers = Peers
m6e-torrents-trackers-column-message = Message
m6e-torrents-trackers-empty = This torrent has no trackers.
m6e-torrents-tracker-working = Working
m6e-torrents-tracker-updating = Updating
m6e-torrents-tracker-error = Error
m6e-torrents-tracker-disabled = Disabled

m6e-torrents-peers =
    .aria-label = Peers of this torrent
m6e-torrents-peers-column-address = Address
m6e-torrents-peers-column-client = Client
m6e-torrents-peers-column-encrypted = Encryption
m6e-torrents-peers-column-down = Down
m6e-torrents-peers-column-up = Up
m6e-torrents-peers-column-progress = Progress
m6e-torrents-peers-empty = No peers are connected.
m6e-torrents-peer-encrypted = Encrypted
m6e-torrents-peer-plain = Not encrypted

## Removing a torrent

m6e-torrents-remove-question = Remove this torrent?
m6e-torrents-remove-note = “Remove and delete files” also deletes the downloaded files from your disk.
m6e-torrents-remove-keep = Remove torrent
m6e-torrents-remove-delete = Remove and delete files
m6e-torrents-remove-cancel = Cancel
