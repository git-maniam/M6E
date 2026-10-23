# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Run the vendored Speedometer 3 offline against one or more browser builds.

Run with the mach Python environment, which provides marionette_driver:

    ./mach python tools/m6e/speedometer.py --binary <exe> [--binary <exe> ...] --out <file>

Builds are run in alternating rounds so machine noise affects them equally.
Writes the per-iteration scores and their means as JSON to --out.
"""

import argparse
import functools
import http.server
import json
import os
import statistics
import threading
import time

from marionette_driver.marionette import Marionette

SUITE = os.path.join(
    os.path.dirname(__file__),
    "..",
    "..",
    "third_party",
    "webkit",
    "PerformanceTests",
    "Speedometer3",
)

RESULTS_JS = """
const client = window.wrappedJSObject.benchmarkClient;
if (!client || !client._hasResults) {
  return null;
}
return client._measuredValuesList.map(v => v.score);
"""


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve_suite():
    handler = functools.partial(QuietHandler, directory=os.path.abspath(SUITE))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def run_once(binary, url, timeout):
    client = Marionette(bin=binary, startup_timeout=120)
    client.start_session()
    try:
        client.set_context(client.CONTEXT_CONTENT)
        client.navigate(url)
        deadline = time.time() + timeout
        while time.time() < deadline:
            scores = client.execute_script(RESULTS_JS)
            if scores is not None:
                return scores
            time.sleep(5)
        raise TimeoutError(f"Speedometer did not finish within {timeout} s")
    finally:
        client.quit(in_app=True)
        client.cleanup()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", action="append", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--timeout", type=int, default=1200)
    args = parser.parse_args()

    server = serve_suite()
    url = (
        f"http://127.0.0.1:{server.server_address[1]}/index.html"
        f"?startAutomatically&iterationCount={args.iterations}"
    )
    scores = {binary: [] for binary in args.binary}
    try:
        for _ in range(args.rounds):
            for binary in args.binary:
                scores[binary] += run_once(binary, url, args.timeout)
                print(f"{binary}: {statistics.mean(scores[binary]):.2f}", flush=True)
    finally:
        server.shutdown()

    results = {
        binary: {
            "mean": statistics.mean(s),
            "stdev": statistics.stdev(s) if len(s) > 1 else 0,
            "scores": s,
        }
        for binary, s in scores.items()
    }
    with open(args.out, "w") as fh:
        json.dump(results, fh, indent=2)
    for binary, r in results.items():
        print(f"{r['mean']:.2f} ± {r['stdev']:.2f}  {binary}")


if __name__ == "__main__":
    main()
