# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

"""
Regression test for bug 2036593.

On a restart, macOS must not have two processes of the same app bundle running
at the same time. While it does, the second one counts as another instance of
the app and is given its own dock tile, which then sticks around in the dock's
list of recently used applications. Users saw those pile up, one per restart.

We sample the process table across a restart and check that the process being
replaced is gone before its replacement shows up.
"""

import os
import platform
import subprocess
import threading
import time

from marionette_harness import MarionetteTestCase

SAMPLE_INTERVAL_SECONDS = 0.02


class ProcessSampler(threading.Thread):
    """Records which top-level processes of a given binary are alive."""

    def __init__(self, binary):
        super().__init__(daemon=True)
        # argv[0] of the relaunched process is the same binary path, but the
        # caller may have handed us a path through a symlink.
        self._prefixes = {binary, os.path.realpath(binary)}
        self._done = threading.Event()
        self.samples = []
        self.error = None

    def _matches(self, command):
        # Compare against the whole first argument rather than splitting on
        # whitespace, because the bundle path may contain spaces. Child
        # processes run different binaries (plugin-container, gpu-helper) and
        # so do not match.
        return any(
            command == prefix or command.startswith(prefix + " ")
            for prefix in self._prefixes
        )

    def run(self):
        try:
            while not self._done.is_set():
                out = subprocess.run(
                    ["ps", "-Ao", "pid=,command="],
                    capture_output=True,
                    text=True,
                    check=False,
                ).stdout
                alive = set()
                for line in out.splitlines():
                    pid, _, command = line.strip().partition(" ")
                    if pid.isdigit() and self._matches(command.strip()):
                        alive.add(int(pid))
                self.samples.append(alive)
                self._done.wait(SAMPLE_INTERVAL_SECONDS)
        except Exception as exc:  # noqa: BLE001 - reported by the test body
            self.error = exc

    def stop(self):
        self._done.set()
        self.join(timeout=30)


class TestRelaunchDockOverlap(MarionetteTestCase):
    def setUp(self):
        super().setUp()
        if platform.system() != "Darwin":
            self.skipTest("macOS-only regression test (bug 2036593)")

        self.binary = self.marionette.bin
        # .../Foo.app/Contents/MacOS/firefox -> .../Foo.app
        self.bundle_path = os.path.dirname(
            os.path.dirname(os.path.dirname(self.binary))
        )
        if not self.bundle_path.endswith(".app"):
            self.skipTest(f"Not running from an .app bundle: {self.binary}")

    def test_relaunch_does_not_overlap_previous_process(self):
        binary = self.binary
        old_pid = self.marionette.process_id

        sampler = ProcessSampler(binary)
        sampler.start()
        try:
            # Make sure we are sampling before anything moves.
            deadline = time.monotonic() + 10
            while not sampler.samples and time.monotonic() < deadline:
                time.sleep(SAMPLE_INTERVAL_SECONDS)

            self.marionette.restart(in_app=True)
            new_pid = self.marionette.process_id
        finally:
            sampler.stop()

        self.assertIsNone(sampler.error, msg=f"Sampler failed: {sampler.error}")
        self.assertNotEqual(old_pid, new_pid, msg="Application did not restart")

        saw_old = any(old_pid in sample for sample in sampler.samples)
        saw_new = any(new_pid in sample for sample in sampler.samples)
        overlaps = [
            sorted(sample)
            for sample in sampler.samples
            if old_pid in sample and new_pid in sample
        ]

        detail = (
            f"old_pid={old_pid} new_pid={new_pid} "
            f"samples={len(sampler.samples)} overlaps={len(overlaps)}"
        )

        # If we never saw either process the sampling missed the transition and
        # the check below would pass for the wrong reason.
        self.assertTrue(saw_old, msg=f"Never sampled the original process. {detail}")
        self.assertTrue(saw_new, msg=f"Never sampled the relaunched process. {detail}")
        self.assertEqual(
            overlaps,
            [],
            msg=(
                "The relaunched process was running while the process it "
                f"replaced was still alive. {detail}"
            ),
        )
