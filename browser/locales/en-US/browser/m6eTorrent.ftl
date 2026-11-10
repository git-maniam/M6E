# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## M6E: the BitTorrent button, panel and Add dialog (Phase 8, D33 to D37).

## The BT button

m6e-torrent-button =
    .tooltiptext = BitTorrent
    .aria-label = BitTorrent
# Variables:
#   $count (Number) - the number of torrents that are downloading or seeding
m6e-torrent-button-active =
    .tooltiptext = BitTorrent
    .aria-label =
        { $count ->
            [one] BitTorrent, 1 torrent active
           *[other] BitTorrent, { $count } torrents active
        }
m6e-torrent-button-attention =
    .tooltiptext = BitTorrent
    .aria-label = BitTorrent, a torrent has an error

## The panel

m6e-torrent-title = BitTorrent
# Variables:
#   $down (String) - the total download speed, for example "1.2 MB/s"
#   $up (String) - the total upload speed, for example "300 KB/s"
m6e-torrent-speeds = Down { $down }, up { $up }
m6e-torrent-add-file = Add torrent file…
m6e-torrent-add-magnet = Add magnet link…
m6e-torrent-pause-all = Pause all
m6e-torrent-resume-all = Resume all
m6e-torrent-magnet-input =
    .aria-label = Magnet link
    .placeholder = Paste a magnet link
m6e-torrent-magnet-submit = Add
m6e-torrent-magnet-cancel = Cancel
m6e-torrent-magnet-invalid = This is not a magnet link. It should start with magnet:?xt=urn:btih:
m6e-torrent-list =
    .aria-label = Torrents
m6e-torrent-empty = No torrents yet. Add a torrent file or paste a magnet link. Nothing runs until you add one.
m6e-torrent-error-banner = Some torrents have errors. Open the list to see which.

## Torrent rows

m6e-torrent-state-checking = Checking
m6e-torrent-state-queued = Queued
m6e-torrent-state-downloading = Downloading
m6e-torrent-state-seeding = Seeding
m6e-torrent-state-paused = Paused
m6e-torrent-state-completed = Completed
m6e-torrent-state-error = Error
# Variables:
#   $name (String) - the name of the torrent
m6e-torrent-row-open =
    .title = Show details of { $name }
    .aria-label = Show details of { $name }
m6e-torrent-row-pause = Pause
    .title = Pause { $name }
    .aria-label = Pause { $name }
m6e-torrent-row-resume = Resume
    .title = Resume { $name }
    .aria-label = Resume { $name }
m6e-torrent-row-reveal = Show in Finder
    .title = Show { $name } in Finder
    .aria-label = Show { $name } in Finder
m6e-torrent-row-remove = Remove…
    .title = Remove { $name }
    .aria-label = Remove { $name }
# Variables:
#   $name (String) - the name of the torrent
m6e-torrent-remove-question = Remove “{ $name }”?
m6e-torrent-remove-keep = Remove torrent
m6e-torrent-remove-delete = Remove and delete files
m6e-torrent-remove-cancel = Cancel
# Variables:
#   $percent (Number) - progress from 0 to 100
m6e-torrent-progress =
    .aria-label = Progress
    .title = { NUMBER($percent, maximumFractionDigits: 1) }%
# Variables:
#   $speed (String) - a speed, for example "1.2 MB/s"
m6e-torrent-row-down = Down { $speed }
m6e-torrent-row-up = Up { $speed }
# Variables:
#   $eta (String) - the time left, for example "5 min"
m6e-torrent-row-eta = { $eta } left
# Variables:
#   $message (String) - the error text from the engine
m6e-torrent-row-error = { $message }

## Quick settings in the panel

m6e-torrent-turtle =
    .label = Turtle mode
    .description = Use the slower alternative speed limits.
m6e-torrent-limit-down = Download limit (KB/s, 0 is unlimited)
m6e-torrent-limit-up = Upload limit (KB/s, 0 is unlimited)
m6e-torrent-encryption = Encryption
m6e-torrent-encryption-disabled = Disabled
m6e-torrent-encryption-enabled = Enabled
m6e-torrent-encryption-forced = Forced
m6e-torrent-seed-goal = Seeding goal
m6e-torrent-seed-goal-ratio = Stop at a ratio
m6e-torrent-seed-goal-hours = Stop after some hours
m6e-torrent-seed-goal-never = Never stop
m6e-torrent-seed-ratio =
    .aria-label = Ratio
m6e-torrent-seed-hours =
    .aria-label = Hours
m6e-torrent-seed-ratio-unit = ratio
m6e-torrent-seed-hours-unit = hours
m6e-torrent-settings = BitTorrent settings…

## Errors shown to the user

m6e-torrent-error-title = BitTorrent
# Variables:
#   $message (String) - the reason the torrent could not be added
m6e-torrent-error-add = This torrent could not be added. { $message }

## Quitting with active torrents

m6e-torrent-quit-title = Quit M6E?
m6e-torrent-quit-message = You have torrents that are downloading or seeding. If you quit now they stop until you start M6E again.
m6e-torrent-quit-button = Quit
m6e-torrent-dont-quit-button = Don’t quit

## File picker

m6e-torrent-pick-file-title = Choose a torrent file
m6e-torrent-pick-file-filter = Torrent files
m6e-torrent-pick-folder-title = Choose where to save the files

## The Add torrent dialog

m6e-torrent-add-window-title = Add torrent
m6e-torrent-add-heading = Add torrent
m6e-torrent-add-fetching = Fetching torrent information…
m6e-torrent-add-fetching-note = Only the file list is requested. No file data is downloaded until you click Add.
m6e-torrent-add-name = Name
m6e-torrent-add-size = Size
# Variables:
#   $size (String) - the total size, for example "1.4 GB"
#   $count (Number) - the number of files, not counting padding files
m6e-torrent-add-size-value =
    { $count ->
        [one] { $size } in 1 file
       *[other] { $size } in { $count } files
    }
m6e-torrent-add-files = Files
m6e-torrent-add-file-tree =
    .aria-label = Files in this torrent
m6e-torrent-add-priority = Priority
m6e-torrent-priority-high = High
m6e-torrent-priority-normal = Normal
m6e-torrent-priority-low = Low
m6e-torrent-priority-skip = Do not download
# Variables:
#   $name (String) - a file name
m6e-torrent-add-file-checkbox =
    .aria-label = Download { $name }
# Variables:
#   $name (String) - a file name
m6e-torrent-add-file-priority =
    .aria-label = Priority of { $name }
# Variables:
#   $name (String) - a folder name
m6e-torrent-add-folder-checkbox =
    .aria-label = Download everything in { $name }
# Variables:
#   $selected (Number) - the number of files chosen
#   $total (Number) - the number of files in the torrent
#   $size (String) - the size of the chosen files, for example "1.4 GB"
m6e-torrent-add-selection = { $selected } of { $total } files chosen, { $size }
m6e-torrent-add-save-to = Save to
m6e-torrent-add-save-path =
    .aria-label = Save location
m6e-torrent-add-browse = Choose…
# Variables:
#   $needed (String) - the size of the chosen files, for example "40 GB"
#   $available (String) - the free space, for example "12 GB"
m6e-torrent-add-no-space = The chosen files need { $needed }, but only { $available } is free in this folder. Choose other files or another folder.
m6e-torrent-add-sequential = Sequential download
m6e-torrent-add-start-now = Start now
# Variables:
#   $site (String) - the site the torrent came from, for example "https://example.org"
m6e-torrent-add-trust-site = Always trust torrents from this site ({ $site })
m6e-torrent-add-confirm = Add
m6e-torrent-add-cancel = Cancel
# Variables:
#   $message (String) - the reason the torrent could not be added
m6e-torrent-add-failed = The torrent could not be added. { $message }

## The Shady Torrent banner

m6e-torrent-shady-title = Shady Torrent
m6e-torrent-shady-description = This torrent has traits that are common in malicious or misleading downloads.
m6e-torrent-shady-cancel = Cancel
m6e-torrent-shady-add-anyway = Add anyway
# Variables:
#   $files (String) - the file names concerned, for example "setup.exe, crack.exe"
m6e-torrent-shady-windows-program = It contains Windows programs or scripts, which do not run on a Mac but are often used to spread malware: { $files }
m6e-torrent-shady-double-extension = Some file names end in two extensions to hide that they are programs: { $files }
m6e-torrent-shady-bidi-override = Some file names contain hidden text-direction characters that can make a program look like a harmless file: { $files }
m6e-torrent-shady-piracy-tool-name = Some file names advertise cracks, key generators or similar tools, which are often infected: { $files }
m6e-torrent-shady-program-in-media = A program or archive is mixed in with files that look like video or audio: { $files }
m6e-torrent-shady-macos-installer = It contains Mac installers or programs, which can change your system: { $files }
m6e-torrent-shady-shortcut-file = It contains link shortcuts that can open web pages: { $files }
m6e-torrent-shady-password-file = A file that looks like a password or readme note sits next to an archive, a common trick to get you to open it: { $files }
# Variables:
#   $files (String) - the file names concerned
m6e-torrent-shady-other = A trait that is common in unsafe downloads was found: { $files }
# Variables:
#   $count (Number) - the number of files not listed
m6e-torrent-shady-more-files = and { $count } more
