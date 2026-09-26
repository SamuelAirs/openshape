# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Reads OpenShape's GitHub Actions results without a login (public API).

Failed CI steps put their error lines into annotations
(scripts/ci/run-logged.sh); successful ones add notices (test counts,
screenshot numbers, iPad app size, QML modules). This prints them.

  python scripts/dev/ci_status.py status [sha]         runs and jobs of a commit (default HEAD)
  python scripts/dev/ci_status.py watch [sha] [secs]   wait until a run of it finishes, then report it
  python scripts/dev/ci_status.py report <run id>      jobs, steps and annotations of a run
  python scripts/dev/ci_status.py job <job id>         one job

Anonymous API calls are limited to 60 per hour per IP address; `watch`
polls one call per interval (default 150 s) and sleeps out a rate limit.
Run `watch` in the background and act when it exits.
"""

import datetime
import json
import subprocess
import sys
import time
import urllib.error
import urllib.request

API = "https://api.github.com/repos/SamuelAirs/openshape"


def get(url):
    request = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json",
                                                   "User-Agent": "openshape-ci-status"})
    for _ in range(6):
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                return json.load(response), response.headers.get("X-RateLimit-Remaining")
        except urllib.error.HTTPError as e:
            body = e.read().decode(errors="replace").lower()
            if e.code in (403, 429) and "rate limit" in body:
                print("rate limited; sleeping 10 min", flush=True)
                time.sleep(600)
                continue
            raise
        except OSError as e:  # network hiccup
            print("request failed:", e, flush=True)
            time.sleep(30)
    raise SystemExit("giving up on " + url)


def minutes(start, end):
    if not start or not end:
        return ""
    parse = lambda s: datetime.datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ")
    return f"{(parse(end) - parse(start)).total_seconds() / 60:.1f} min"


def print_job(job):
    print(f"  JOB {job['name']} [{job['id']}]: {job['status']} {job['conclusion']} "
          f"{minutes(job.get('started_at'), job.get('completed_at'))}")
    marks = {"success": "ok", "failure": "FAIL", "skipped": "skip", "cancelled": "cancel", None: "..."}
    for step in job.get("steps", []):
        print(f"     {marks.get(step['conclusion'], step['conclusion']):6} {step['name']}  "
              f"{minutes(step.get('started_at'), step.get('completed_at'))}")
    if job["status"] == "completed":
        annotations, _ = get(f"{API}/check-runs/{job['id']}/annotations?per_page=100")
        for a in annotations:
            if "Node.js 20 is deprecated" in a["message"]:
                continue
            print(f"   [{a['annotation_level']}] {a['title']}")
            for line in a["message"].splitlines():
                print("      " + line)


def report(run_id):
    data, remaining = get(f"{API}/actions/runs/{run_id}/jobs")
    for job in data["jobs"]:
        print_job(job)
    print(f"(API calls left this hour: {remaining})")


def runs_of(sha):
    data, remaining = get(f"{API}/actions/runs?head_sha={sha}&per_page=20")
    return data["workflow_runs"], remaining


def head_sha():
    return subprocess.run(["git", "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()


def main(argv):
    command = argv[1] if len(argv) > 1 else "status"
    if command == "status":
        sha = argv[2] if len(argv) > 2 else head_sha()
        runs, remaining = runs_of(sha)
        for run in runs:
            print(f"RUN {run['name']} #{run['run_number']} [{run['id']}]: {run['status']} {run['conclusion']}")
            data, remaining = get(f"{API}/actions/runs/{run['id']}/jobs")
            for job in data["jobs"]:
                print(f"  JOB {job['name']} [{job['id']}]: {job['status']} {job['conclusion']}")
        print(f"(API calls left this hour: {remaining})")
    elif command == "watch":
        sha = argv[2] if len(argv) > 2 else head_sha()
        interval = int(argv[3]) if len(argv) > 3 else 150
        runs, _ = runs_of(sha)
        done_before = {r["id"] for r in runs if r["status"] == "completed"}
        while True:
            runs, _ = runs_of(sha)
            finished = [r for r in runs if r["status"] == "completed" and r["id"] not in done_before]
            if finished:
                for run in finished:
                    print(f"=== {run['name']} #{run['run_number']} [{run['id']}]: {run['conclusion']}", flush=True)
                    report(run["id"])
                for run in runs:
                    if run["status"] != "completed":
                        print(f"still running: {run['name']} #{run['run_number']} [{run['id']}]")
                return
            time.sleep(interval)
    elif command == "report":
        report(argv[2])
    elif command == "job":
        data, _ = get(f"{API}/actions/jobs/{argv[2]}")
        print_job(data)
    else:
        print(__doc__)


if __name__ == "__main__":
    main(sys.argv)
