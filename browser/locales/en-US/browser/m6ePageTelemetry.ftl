# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## M6E: the page telemetry button and panel (D29).

m6e-page-telemetry-button =
    .tooltiptext = Telemetry
    .aria-label = Telemetry
m6e-page-telemetry-title = Telemetry
m6e-page-telemetry-description = Measured on this device for the current page. Nothing is sent anywhere.
m6e-page-telemetry-ttfb = Time to first byte (TTFB)
m6e-page-telemetry-fcp = First contentful paint (FCP)
m6e-page-telemetry-tti = Time to interactive (TTI)
m6e-page-telemetry-page-size = Page size
m6e-page-telemetry-script-parse = Script parse time
m6e-page-telemetry-script-execution = Script execution time
m6e-page-telemetry-tls = TLS/SSL handshake time

# Variables:
#   $value (Number) - a duration in milliseconds
m6e-page-telemetry-milliseconds = { NUMBER($value, maximumFractionDigits: 0) } ms
# Variables:
#   $transferred (String) - bytes received over the network, for example "1.2 MB"
#   $requests (Number) - the number of requests the page made
m6e-page-telemetry-size =
    { $requests ->
        [one] { $transferred } (1 request)
       *[other] { $transferred } ({ $requests } requests)
    }
# Variables:
#   $transferred (String) - bytes received over the network, for example "1.2 MB"
#   $requests (Number) - the number of requests the page made
#   $cached (Number) - how many of them were served from the browser's cache
m6e-page-telemetry-size-cached =
    { $requests ->
        [one] { $transferred } (1 request, from cache)
       *[other] { $transferred } ({ $requests } requests, { $cached } from cache)
    }
m6e-page-telemetry-measuring = Measuring…
m6e-page-telemetry-not-available = Not available
m6e-page-telemetry-connection-reused = Reused connection, no handshake
m6e-page-telemetry-not-encrypted = Not encrypted (HTTP)
m6e-page-telemetry-unsupported = Telemetry is only available for web pages.
m6e-page-telemetry-loading = The page is still loading. Script times so far.
