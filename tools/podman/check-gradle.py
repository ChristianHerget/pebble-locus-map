#!/usr/bin/env python3
"""Run one unchanged Gradle invocation and retain only reports it refreshed."""
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


REPORT_ROOTS = (
    Path("android/app/build/test-results"),
    Path("android/app/build/reports"),
    Path("build/reports"),
)


def snapshot():
    return {
        path: (path.stat().st_mtime_ns, path.stat().st_size)
        for root in REPORT_ROOTS
        for path in root.rglob("*")
        if path.is_file() and not path.is_symlink()
        and path.suffix in {".xml", ".html", ".txt", ".json"}
    }


def totals(paths):
    result = dict.fromkeys(("tests", "failures", "errors", "skipped"), 0)
    for path in paths:
        root = ET.parse(path).getroot()
        if root.tag != "testsuite":
            raise ValueError("expected a testsuite")
        for key in result:
            value = int(root.attrib[key])
            if value < 0:
                raise ValueError("negative test count")
            result[key] += value
    return result


def main():
    before = snapshot()
    process = subprocess.Popen(sys.argv[1:], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    executed = False
    for line in process.stdout:
        sys.stdout.buffer.write(line)
        sys.stdout.buffer.flush()
        if re.fullmatch(rb"> Task :android:app:testDebugUnitTest(?: FAILED)?\r?\n", line):
            executed = True
    status = process.wait()
    status = 128 - status if status < 0 else status
    try:
        after = snapshot()
        fresh = [path for path, stamp in after.items() if before.get(path) != stamp]
        directory = os.environ.get("CHECK_LOG_DIR")
        if directory:
            for path in fresh:
                destination = Path(directory) / "reports" / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(path, destination)
            if fresh:
                print(f"Reports: reports/ ({len(fresh)} refreshed files)")
        junit = [path for path in fresh if path.match("*/testDebugUnitTest/TEST-*.xml")]
        if executed and junit:
            try:
                counts = totals(junit)
                print("Tests: JVM " + " ".join(f"{key}={value}" for key, value in counts.items()))
            except (ET.ParseError, ValueError, KeyError) as error:
                print(f"Report totals unavailable: {error}")
    except OSError as error:
        print(f"Report retention failed: {error}", file=sys.stderr)
        status = status or 74
    return status


if __name__ == "__main__":
    sys.exit(main())
