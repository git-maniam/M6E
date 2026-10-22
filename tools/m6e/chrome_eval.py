# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Run a JavaScript snippet in the chrome context of a fresh browser profile.

    ./mach python tools/m6e/chrome_eval.py --binary <app binary> --script <file.js> [--headless]

The snippet runs as the body of an async function; whatever it returns is
printed as JSON. `marionette` and `sleep(ms)` are available to the snippet.
"""

import argparse
import json

from marionette_driver.marionette import Marionette

WRAPPER = """
const resolve = arguments[arguments.length - 1];
const sleep = ms => new Promise(r => setTimeout(r, ms));
(async () => {
%s
})().then(resolve, e => resolve({ error: String(e), stack: e.stack }));
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", required=True)
    parser.add_argument("--script", required=True)
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--timeout", type=int, default=120)
    args = parser.parse_args()

    with open(args.script) as fh:
        body = fh.read()

    client = Marionette(bin=args.binary, headless=args.headless, startup_timeout=120)
    client.start_session()
    try:
        client.set_context(client.CONTEXT_CHROME)
        client.switch_to_window(client.chrome_window_handles[0])
        result = client.execute_async_script(
            WRAPPER % body, script_timeout=args.timeout * 1000
        )
    finally:
        client.quit(in_app=True)
        client.cleanup()
    print(json.dumps(result, indent=2, default=str))


if __name__ == "__main__":
    main()
