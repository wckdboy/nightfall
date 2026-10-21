#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Real process-lifecycle regressions for the private acceptance driver."""
import os
import signal
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

from eclipse_reference import interrupted, run, stop


class RunnerTests(unittest.TestCase):
    def test_success_json_is_returned(self):
        result = run([sys.executable, '-c', 'print(\'{"status":"passed"}\')'],
                     Path.cwd(), time.monotonic()+30)
        self.assertEqual(result, {'status': 'passed'})

    def test_failed_worker_cannot_publish_success(self):
        with self.assertRaisesRegex(RuntimeError, 'acceptance stage failed'):
            run([sys.executable, '-c', 'print(\'{"status":"passed"}\'); raise SystemExit(3)'],
                Path.cwd(), time.monotonic()+30)

    @unittest.skipUnless(os.name == 'posix', 'POSIX worker-group lifetime check')
    def test_timeout_stops_nested_worker(self):
        self.check_nested_worker(False)

    @unittest.skipUnless(os.name == 'posix', 'POSIX worker-group lifetime check')
    def test_sigterm_stops_nested_worker(self):
        self.check_nested_worker(True)

    def check_nested_worker(self, cancel):
        launch = subprocess.Popen
        owned = []
        with tempfile.TemporaryDirectory(prefix='nightfall-runner-') as temporary:
            pidfile = Path(temporary)/'worker.pid'
            child = ('import os,sys,time; '
                     'open(sys.argv[1]+".tmp","w").write(str(os.getpid())); '
                     'os.replace(sys.argv[1]+".tmp",sys.argv[1]); time.sleep(30)')
            parent = ('import subprocess,sys,time; '
                      'subprocess.Popen([sys.executable,"-c",sys.argv[1],sys.argv[2]]); '
                      'time.sleep(30)')

            def ready(*args, **kwargs):
                process = launch(*args, **kwargs)
                owned.append(process)
                limit = time.monotonic()+10
                while not pidfile.exists() and time.monotonic() < limit:
                    time.sleep(.01)
                self.assertTrue(pidfile.exists(), 'nested worker never became ready')
                if cancel:
                    communicate = process.communicate

                    def terminate(*args, **kwargs):
                        process.communicate = communicate
                        os.kill(os.getpid(), signal.SIGTERM)
                        return communicate(*args, **kwargs)

                    process.communicate = terminate
                return process

            previous = signal.signal(signal.SIGTERM, interrupted)
            try:
                # Readiness occurs after this deadline, so cleanup is tested with a real
                # live grandchild without depending on a fast interpreter launch.
                with patch('eclipse_reference.subprocess.Popen', side_effect=ready):
                    with self.assertRaises(InterruptedError if cancel else subprocess.TimeoutExpired):
                        run([sys.executable, '-c', parent, child, str(pidfile)],
                            Path.cwd(), time.monotonic()+(30 if cancel else .001))
                pid = int(pidfile.read_text())
                status = subprocess.run(['ps', '-p', str(pid), '-o', 'stat='],
                                        capture_output=True, text=True, check=False).stdout.strip()
                self.assertTrue(not status or status.startswith('Z'),
                                'nested worker remains live: '+status)
            finally:
                for process in owned:
                    stop(process)
                signal.signal(signal.SIGTERM, previous)


if __name__ == '__main__':
    unittest.main()
