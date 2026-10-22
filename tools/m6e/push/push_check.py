# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""Check Web Push end to end through Mozilla's push service (D12).

    ./mach python tools/m6e/push/push_check.py --binary <app>/Contents/MacOS/m6e [--rfp]

Serves a test page with a service worker on localhost, subscribes, and sends
real pushes to the subscription endpoint with curl. It checks that a push is
delivered and shown as a notification with the page open, with its tab
closed, and after an in-app restart, and that the app keeps running when its
last window closes. --rfp repeats the run with fingerprinting resistance.
"""

import argparse
import functools
import http.server
import os
import subprocess
import threading
import time

from marionette_driver.marionette import Marionette

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 8766
ORIGIN = f"http://127.0.0.1:{PORT}"


def serve():
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=HERE)
    handler.log_message = lambda *args: None
    server = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


def send(endpoint):
    return subprocess.run(
        ["curl", "-s", "-o", "/dev/null", "-w", "%{http_code}", "-X", "POST"]
        + ["-H", "TTL: 60", "-H", "Content-Length: 0", endpoint],
        capture_output=True,
        text=True,
        check=False,
    ).stdout


def notifications(client):
    client.set_context("content")
    client.switch_to_window(client.window_handles[0])
    client.navigate(ORIGIN + "/index.html")
    time.sleep(2)
    return client.execute_async_script(
        "const done = arguments[0];"
        "window.wrappedJSObject.notifications().then(n => done(Array.from(n)));"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--binary", required=True)
    parser.add_argument("--rfp", action="store_true")
    args = parser.parse_args()

    server = serve()
    # Marionette turns the push connection off; M6E's default is on.
    prefs = {
        "dom.push.connection.enabled": True,
        "privacy.resistFingerprinting": args.rfp,
    }
    client = Marionette(
        bin=os.path.abspath(args.binary), startup_timeout=120, prefs=prefs
    )
    client.start_session()
    results = {}
    try:
        client.set_context("chrome")
        client.execute_script(
            "Services.perms.addFromPrincipal("
            f"Services.scriptSecurityManager.createContentPrincipalFromOrigin('{ORIGIN}'),"
            "'desktop-notification', Services.perms.ALLOW_ACTION);"
        )
        client.set_context("content")
        client.navigate(ORIGIN + "/index.html")
        sub = client.execute_async_script(
            "const done = arguments[0];"
            "window.wrappedJSObject.setup().then(done, e => done({error: String(e)}));",
            script_timeout=60000,
        )
        if "error" in sub:
            raise SystemExit(f"Subscribing failed: {sub['error']}")

        expected = 0
        for case in ("page open", "page closed", "after restart"):
            if case == "page closed":
                client.set_context("chrome")
                client.execute_script(
                    "let win = Services.wm.getMostRecentWindow('navigator:browser');"
                    "let old = win.gBrowser.selectedTab;"
                    "win.gBrowser.selectedTab = win.gBrowser.addTab('about:blank',"
                    " { triggeringPrincipal: Services.scriptSecurityManager.getSystemPrincipal() });"
                    "win.gBrowser.removeTab(old);"
                )
            elif case == "after restart":
                client.restart(in_app=True)
                client.set_context("chrome")
                client.execute_script(
                    "Services.prefs.setBoolPref('dom.push.connection.enabled', true)"
                )
            time.sleep(5)
            status = send(sub["endpoint"])
            time.sleep(8)
            shown = notifications(client)
            expected = expected + 1 if case != "after restart" else 1
            results[case] = status == "201" and len(shown) >= expected
            print(f"{case}: push {status}, {len(shown)} notification(s)")

        client.set_context("chrome")
        pid = client.execute_script("return Services.appinfo.processID")
        client.execute_script(
            "setTimeout(() => { for (let w of Services.wm.getEnumerator("
            "'navigator:browser')) w.close(); }, 100)"
        )
        time.sleep(4)
        results["runs with no windows"] = (
            subprocess.run(["kill", "-0", str(pid)], check=False).returncode == 0
        )
        print(f"runs with no windows: {results['runs with no windows']}")
    finally:
        client.cleanup()
        server.shutdown()

    failed = [case for case, ok in results.items() if not ok]
    print("FAILED: " + ", ".join(failed) if failed else "All push checks passed.")
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
