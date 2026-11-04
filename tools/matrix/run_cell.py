#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the platform tests of one matrix cell against a built tree.

    run_cell.py --cell ubuntu-26.04 --build-dir build/linux-release-gui --out out

It writes ``result-<cell>.json`` and the evidence files next to it. Every test
reports one of four statuses:

    pass      it ran and held
    fail      it ran and did not hold
    skip      it does not apply here, and the detail says why
    not_run   it should have run and did not (a missing program, a crash before it)

A cell is green only when no applicable test failed or was left unrun.
The ids are the ones in docs/platforms.md.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import queue
import re
import subprocess
import sys
import tempfile
import threading
import time
from datetime import datetime, timezone
from pathlib import Path

SCHEMA = 1
POSIX = os.name == "posix"
HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent

TESTS = {
    "T1": "ctest unit and system suites",
    "T2": "a separate process opens two virtual ports and exchanges data through the hub",
    "T3": "split and join topologies from JSON route bytes",
    "T4": "vss_cli selftest",
    "T5": "vss_gui offscreen self-test",
    "T6": "GUI screenshot in the light and dark themes",
    "T7": "what the operating system and common tools see of a virtual port",
    "T8": "stable link lifecycle of vport",
    "T9": "install the package artifact as a user would",
    "T10": "vss_cli bench over a pseudo terminal pair",
    "T11": "kernel-backed serial experiment",
}


class Recorder:
    def __init__(self) -> None:
        self.results: list[dict] = []
        self.observations: dict = {}

    def add(self, test: str, status: str, detail: str, evidence: str | None = None) -> None:
        assert status in ("pass", "fail", "skip", "not_run"), status
        entry = {"id": test, "status": status, "detail": detail}
        if evidence:
            entry["evidence"] = evidence
        self.results.append(entry)
        print(f"{test:4} {status:8} {detail}", flush=True)


def find_program(build_dir: Path, name: str) -> Path | None:
    suffix = ".exe" if os.name == "nt" else ""
    for folder in (build_dir / "bin", build_dir / "bin" / "Release", build_dir):
        candidate = folder / (name + suffix)
        if candidate.is_file():
            return candidate
    return None


class LineReader:
    """Collects the lines a child process prints, without blocking the test."""

    def __init__(self, stream) -> None:
        self.lines: list[str] = []
        self._queue: queue.Queue[str | None] = queue.Queue()
        self._thread = threading.Thread(target=self._pump, args=(stream,), daemon=True)
        self._thread.start()

    def _pump(self, stream) -> None:
        for raw in iter(stream.readline, ""):
            self._queue.put(raw.rstrip("\r\n"))
        self._queue.put(None)

    def wait_for(self, pattern: str, timeout: float) -> re.Match | None:
        expression = re.compile(pattern)
        for line in self.lines:
            found = expression.search(line)
            if found:
                return found
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            try:
                line = self._queue.get(timeout=remaining)
            except queue.Empty:
                return None
            if line is None:
                return None
            self.lines.append(line)
            found = expression.search(line)
            if found:
                return found

    def drain(self) -> None:
        while True:
            try:
                line = self._queue.get_nowait()
            except queue.Empty:
                return
            if line is not None:
                self.lines.append(line)


class Child:
    """A process started for a test, with its output kept as evidence."""

    def __init__(self, command: list[str], env: dict | None = None) -> None:
        self.command = command
        self.process = subprocess.Popen(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            env=env,
        )
        self.reader = LineReader(self.process.stdout)

    def stop(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.reader.drain()

    def text(self) -> str:
        self.reader.drain()
        return "\n".join(self.reader.lines)


def run_to_end(command: list[str], timeout: float, env: dict | None = None) -> tuple[int | None, str]:
    try:
        completed = subprocess.run(
            command, capture_output=True, text=True, timeout=timeout, env=env
        )
    except subprocess.TimeoutExpired as error:
        output = (error.stdout or b"")
        if isinstance(output, bytes):
            output = output.decode("utf-8", "replace")
        return None, output + "\n[timed out]"
    except OSError as error:
        return None, f"[could not start: {error}]"
    return completed.returncode, completed.stdout + completed.stderr


def write_evidence(out: Path, name: str, text: str) -> str:
    (out / name).write_text(text, encoding="utf-8", errors="replace")
    return name


# ---------------------------------------------------------------- pty helpers

def open_raw(path: str) -> int:
    import termios
    import tty

    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    tty.setraw(fd)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def write_all(fd: int, data: bytes, timeout: float = 10.0) -> None:
    import select

    deadline = time.monotonic() + timeout
    view = memoryview(data)
    while view:
        if time.monotonic() > deadline:
            raise TimeoutError("write timed out")
        _, writable, _ = select.select([], [fd], [], 0.2)
        if not writable:
            continue
        try:
            written = os.write(fd, view)
        except BlockingIOError:
            continue
        view = view[written:]


def read_at_least(fd: int, count: int, timeout: float) -> bytes:
    import select

    collected = bytearray()
    deadline = time.monotonic() + timeout
    while len(collected) < count and time.monotonic() < deadline:
        readable, _, _ = select.select([fd], [], [], 0.2)
        if not readable:
            continue
        try:
            chunk = os.read(fd, 65536)
        except (BlockingIOError, InterruptedError):
            continue
        except OSError:
            break
        if not chunk:
            break
        collected.extend(chunk)
    return bytes(collected)


def start_pty_pair() -> tuple[Child, str, str] | None:
    child = Child([sys.executable, str(REPO / "tools" / "pty_loopback.py")])
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        child.reader.wait_for(r"^NEVER-MATCHES$", 0.2)
        paths = [line for line in child.reader.lines if line.startswith("/")]
        if len(paths) >= 2:
            return child, paths[0], paths[1]
        if child.process.poll() is not None:
            break
    child.stop()
    return None


def topology_ports(child: Child, names: list[str], timeout: float = 30.0) -> dict[str, str] | None:
    """Reads the 'ports:' table vss_cli prints and returns name -> path."""
    if not child.reader.wait_for(r"^links: \d+", timeout):
        return None
    paths: dict[str, str] = {}
    row = re.compile(r"^\s*\[\d+\]\s+(\S+)\s+(\S+)\s+(\S+)\s")
    for line in child.reader.lines:
        found = row.match(line)
        if found and found.group(1) in names:
            paths[found.group(1)] = found.group(3)
    if all(name in paths for name in names):
        return paths
    return None


def sequence(alphabet: bytes, length: int) -> bytes:
    return bytes(alphabet[i % len(alphabet)] for i in range(length))


# ------------------------------------------------------------------ the tests

def test_t1(rec: Recorder, args, out: Path) -> None:
    if args.t1_exit is None:
        rec.add("T1", "not_run", "the ctest result was not passed to the runner")
        return
    evidence = None
    if args.t1_log and Path(args.t1_log).is_file():
        evidence = write_evidence(out, "t1.log", Path(args.t1_log).read_text(errors="replace"))
    if args.t1_exit == 0:
        rec.add("T1", "pass", "ctest passed", evidence)
    else:
        rec.add("T1", "fail", "the build or the ctest suite failed; the job log has the output", evidence)


def test_t4(rec: Recorder, cli: Path | None, out: Path) -> None:
    if cli is None:
        rec.add("T4", "not_run", "vss_cli was not built")
        return
    code, text = run_to_end([str(cli), "selftest"], 120)
    evidence = write_evidence(out, "t4.log", text)
    if code == 0 and "selftest ok" in text:
        rec.add("T4", "pass", text.strip().splitlines()[-1], evidence)
    else:
        rec.add("T4", "fail", f"exit {code}", evidence)


def gui_environment() -> dict:
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    if os.name == "nt" and Path("C:/Windows/Fonts").is_dir():
        env["QT_QPA_FONTDIR"] = "C:/Windows/Fonts"
    return env


def test_t5(rec: Recorder, gui: Path | None, out: Path) -> None:
    if gui is None:
        rec.add("T5", "not_run", "vss_gui was not built")
        return
    code, text = run_to_end([str(gui), "--selftest"], 180, gui_environment())
    evidence = write_evidence(out, "t5.log", text)
    if code == 0 and "selftest ok" in text:
        rec.add("T5", "pass", "selftest ok", evidence)
    else:
        rec.add("T5", "fail", f"exit {code}", evidence)


def test_t6(rec: Recorder, gui: Path | None, out: Path, cell: str) -> None:
    if gui is None:
        rec.add("T6", "not_run", "vss_gui was not built")
        return
    problems = []
    sizes = []
    for theme in ("light", "dark"):
        target = out / f"gui-{cell}-{theme}.png"
        env = gui_environment()
        env["VSS_SCREENSHOT_THEME"] = theme
        code, text = run_to_end([str(gui), f"--screenshot={target}"], 120, env)
        write_evidence(out, f"t6-{theme}.log", text)
        if code != 0 or not target.is_file():
            problems.append(f"{theme}: exit {code}")
            continue
        data = target.read_bytes()
        if data[:8] != b"\x89PNG\r\n\x1a\n" or len(data) < 20_000:
            problems.append(f"{theme}: not a plausible image ({len(data)} bytes)")
        else:
            sizes.append(f"{theme} {len(data) // 1024} KB")
    if problems:
        rec.add("T6", "fail", "; ".join(problems))
    else:
        rec.add(
            "T6",
            "pass",
            "rendered offscreen: " + ", ".join(sizes) + " (proves rendering, not use on a real display)",
        )


def write_topology(directory: Path, name: str, document: dict) -> Path:
    path = directory / name
    path.write_text(json.dumps(document), encoding="utf-8")
    return path


def test_t2(rec: Recorder, cli: Path | None, out: Path, work: Path) -> None:
    if not POSIX:
        rec.add("T2", "skip", "no pseudo terminals on this system; the driver cells cover Windows")
        return
    if cli is None:
        rec.add("T2", "not_run", "vss_cli was not built")
        return
    topology = write_topology(
        work,
        "t2.json",
        {
            "version": 1,
            "name": "t2",
            "ports": [
                {"name": "a", "kind": "virtual", "virtual_kind": "pty"},
                {"name": "b", "kind": "virtual", "virtual_kind": "pty"},
            ],
            "links": [{"mode": "duplex", "sources": ["a"], "targets": ["b"]}],
        },
    )
    child = Child([str(cli), "run", str(topology), "--seconds=60"])
    fds = []
    try:
        paths = topology_ports(child, ["a", "b"])
        if not paths:
            rec.add("T2", "fail", "vss_cli did not print both port paths", write_evidence(out, "t2.log", child.text()))
            return
        fd_a, fd_b = open_raw(paths["a"]), open_raw(paths["b"])
        fds = [fd_a, fd_b]
        forward = sequence(b"0123456789", 4096)
        backward = sequence(b"abcdefghij", 4096)
        write_all(fd_a, forward)
        got_forward = read_at_least(fd_b, len(forward), 15)
        write_all(fd_b, backward)
        got_backward = read_at_least(fd_a, len(backward), 15)
        evidence = write_evidence(out, "t2.log", child.text())
        if got_forward == forward and got_backward == backward:
            rec.add("T2", "pass", f"{paths['a']} <-> {paths['b']}: {len(forward)} bytes each way, in order", evidence)
        else:
            rec.add(
                "T2",
                "fail",
                f"forward {len(got_forward)}/{len(forward)} bytes, backward {len(got_backward)}/{len(backward)} bytes",
                evidence,
            )
    except Exception as error:  # noqa: BLE001 - a test reports, it does not crash the runner
        rec.add("T2", "fail", f"{type(error).__name__}: {error}", write_evidence(out, "t2.log", child.text()))
    finally:
        for fd in fds:
            os.close(fd)
        child.stop()


def test_t3(rec: Recorder, cli: Path | None, out: Path, work: Path) -> None:
    if not POSIX:
        rec.add("T3", "skip", "needs a pseudo terminal pair; Windows is covered in the driver cells")
        return
    if cli is None:
        rec.add("T3", "not_run", "vss_cli was not built")
        return
    pair = start_pty_pair()
    if pair is None:
        rec.add("T3", "not_run", "tools/pty_loopback.py gave no pair")
        return
    pty_child, near, far = pair
    notes = []
    failed = False
    try:
        # split: one device, two listeners
        split = write_topology(
            work,
            "t3-split.json",
            {
                "version": 1,
                "name": "t3-split",
                "ports": [
                    {"name": "device", "kind": "physical", "uri": f"serial://{near}?baud=115200"},
                    {"name": "l1", "kind": "virtual", "virtual_kind": "pty"},
                    {"name": "l2", "kind": "virtual", "virtual_kind": "pty"},
                ],
                "links": [{"mode": "split", "sources": ["device"], "targets": ["l1", "l2"]}],
            },
        )
        child = Child([str(cli), "run", str(split), "--seconds=60"])
        fds = []
        try:
            paths = topology_ports(child, ["l1", "l2"])
            if not paths:
                raise RuntimeError("split: vss_cli did not print the listener paths")
            fd_far = open_raw(far)
            fd_1, fd_2 = open_raw(paths["l1"]), open_raw(paths["l2"])
            fds = [fd_far, fd_1, fd_2]
            payload = sequence(b"0123456789abcdef", 8192)
            write_all(fd_far, payload)
            got_1 = read_at_least(fd_1, len(payload), 15)
            got_2 = read_at_least(fd_2, len(payload), 15)
            if got_1 == payload and got_2 == payload:
                notes.append(f"split: {len(payload)} bytes reached both listeners in order")
            else:
                failed = True
                notes.append(f"split: listener 1 got {len(got_1)}, listener 2 got {len(got_2)} of {len(payload)}")
        finally:
            for fd in fds:
                os.close(fd)
            child.stop()
            write_evidence(out, "t3-split.log", child.text())

        # join: two publishers, one device
        join = write_topology(
            work,
            "t3-join.json",
            {
                "version": 1,
                "name": "t3-join",
                "ports": [
                    {"name": "p1", "kind": "virtual", "virtual_kind": "pty"},
                    {"name": "p2", "kind": "virtual", "virtual_kind": "pty"},
                    {"name": "device", "kind": "physical", "uri": f"serial://{near}?baud=115200"},
                ],
                "links": [{"mode": "join", "sources": ["p1", "p2"], "targets": ["device"]}],
            },
        )
        child = Child([str(cli), "run", str(join), "--seconds=60"])
        fds = []
        try:
            paths = topology_ports(child, ["p1", "p2"])
            if not paths:
                raise RuntimeError("join: vss_cli did not print the publisher paths")
            fd_far = open_raw(far)
            fd_1, fd_2 = open_raw(paths["p1"]), open_raw(paths["p2"])
            fds = [fd_far, fd_1, fd_2]
            digits = sequence(b"0123456789", 4096)
            letters = sequence(b"abcdefghij", 4096)
            write_all(fd_1, digits)
            write_all(fd_2, letters)
            received = read_at_least(fd_far, len(digits) + len(letters), 15)
            only_digits = bytes(b for b in received if 0x30 <= b <= 0x39)
            only_letters = bytes(b for b in received if 0x61 <= b <= 0x6A)
            if only_digits == digits and only_letters == letters:
                notes.append(f"join: {len(received)} bytes arrived, each publisher's bytes in order")
            else:
                failed = True
                notes.append(
                    f"join: got {len(received)} of {len(digits) + len(letters)} bytes; "
                    f"digits ok={only_digits == digits}, letters ok={only_letters == letters}"
                )
        finally:
            for fd in fds:
                os.close(fd)
            child.stop()
            write_evidence(out, "t3-join.log", child.text())
    except Exception as error:  # noqa: BLE001
        failed = True
        notes.append(f"{type(error).__name__}: {error}")
    finally:
        pty_child.stop()
    rec.add("T3", "fail" if failed else "pass", "; ".join(notes), "t3-split.log")


def test_t7(rec: Recorder, cli: Path | None, out: Path) -> None:
    """Records what the system and common tools see. Observations are data, not assertions."""
    if not POSIX:
        rec.add("T7", "skip", "the enumeration probe is for pseudo terminals; Windows is covered in the driver cells")
        return
    if cli is None:
        rec.add("T7", "not_run", "vss_cli was not built")
        return
    child = Child([str(cli), "virtual", "--kind=pty", "--seconds=40"])
    try:
        found = child.reader.wait_for(r"open this from your application: (\S+)", 20)
        if not found:
            rec.add("T7", "fail", "vss_cli virtual printed no path", write_evidence(out, "t7-cli.log", child.text()))
            return
        path = found.group(1)
        seen: dict = {"path": path}

        def probe(name: str, command: list[str]) -> None:
            code, text = run_to_end(command, 20)
            seen[name] = {"exit": code, "output": text.strip()[:4000]}

        seen["exists"] = os.path.exists(path)
        seen["mode"] = oct(os.stat(path).st_mode & 0o7777) if os.path.exists(path) else None
        if sys.platform == "darwin":
            probe("dev_cu_tty", ["/bin/sh", "-c", "ls /dev/cu.* /dev/tty.* 2>&1 | head -40"])
            probe("ioreg_serial", ["/bin/sh", "-c", "ioreg -c IOSerialBSDClient -r 2>&1 | head -40"])
            probe("stty", ["stty", "-f", path, "-a"])
        else:
            probe("tty_drivers", ["/bin/sh", "-c", "cat /proc/tty/drivers 2>&1 | head -40"])
            probe("stty", ["stty", "-F", path, "-a"])
        try:
            import serial  # type: ignore
            from serial.tools import list_ports  # type: ignore

            listed = [f"{p.device} ({p.description})" for p in list_ports.comports()]
            seen["pyserial"] = {"version": serial.__version__, "comports": listed, "lists_the_path": any(path in item for item in listed)}
        except ImportError:
            seen["pyserial"] = "not installed in this environment"
        rec.observations["enumeration"] = seen
        evidence = write_evidence(out, "t7.json", json.dumps(seen, indent=2))
        rec.add("T7", "pass", f"probed {path}; the observations are in the result file", evidence)
    finally:
        child.stop()


def test_t10(rec: Recorder, cli: Path | None, out: Path) -> None:
    if not POSIX:
        rec.add("T10", "skip", "needs a pseudo terminal pair; Windows is covered in the driver cells")
        return
    if cli is None:
        rec.add("T10", "not_run", "vss_cli was not built")
        return
    pair = start_pty_pair()
    if pair is None:
        rec.add("T10", "not_run", "tools/pty_loopback.py gave no pair")
        return
    pty_child, near, far = pair
    try:
        code, text = run_to_end(
            [str(cli), "bench", f"--from=serial://{near}?baud=115200", f"--to=serial://{far}?baud=115200", "--bytes=4MB"],
            180,
        )
        evidence = write_evidence(out, "t10.log", text)
        throughput = re.search(r"throughput:\s*(.+)", text)
        if code == 0 and throughput:
            rec.observations["bench_pty"] = throughput.group(1).strip()
            rec.add("T10", "pass", f"4 MB over a pseudo terminal pair, {throughput.group(1).strip()} (recorded, not yet held to a floor)", evidence)
        else:
            rec.add("T10", "fail", f"exit {code}", evidence)
    finally:
        pty_child.stop()


def not_applicable(rec: Recorder, args) -> None:
    rec.add("T8", "skip", "vport is not implemented yet")
    rec.add("T9", "skip", "the package artifact does not exist yet")
    rec.add("T11", "skip", "an experiment for the Ubuntu guest only")


# ------------------------------------------------------------------- the main

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cell", required=True)
    parser.add_argument("--kind", choices=("hosted", "vm"), default="hosted")
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--commit", default=os.environ.get("GITHUB_SHA", ""))
    parser.add_argument("--runner-label", default="")
    parser.add_argument("--workflow-run", type=int, default=0)
    parser.add_argument("--vm", default="")
    parser.add_argument("--snapshot", default="")
    parser.add_argument("--t1-exit", type=int, default=None)
    parser.add_argument("--t1-log", default="")
    args = parser.parse_args(argv)

    out: Path = args.out
    out.mkdir(parents=True, exist_ok=True)
    started = datetime.now(timezone.utc)
    rec = Recorder()

    cli = find_program(args.build_dir, "vss_cli")
    gui = find_program(args.build_dir, "vss_gui")

    with tempfile.TemporaryDirectory(prefix="vss-cell-") as scratch:
        work = Path(scratch)
        test_t1(rec, args, out)
        test_t2(rec, cli, out, work)
        test_t3(rec, cli, out, work)
        test_t4(rec, cli, out)
        test_t5(rec, gui, out)
        test_t6(rec, gui, out, args.cell)
        test_t7(rec, cli, out)
        not_applicable(rec, args)
        test_t10(rec, cli, out)

    result = {
        "schema": SCHEMA,
        "cell": args.cell,
        "kind": args.kind,
        "host": {
            "os": platform.platform(),
            "kernel": platform.release(),
            "arch": platform.machine(),
            "runner_label": args.runner_label or None,
            "vm": args.vm or None,
            "snapshot": args.snapshot or None,
        },
        "commit": args.commit,
        "artifact": {"workflow_run": args.workflow_run},
        "started": started.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "finished": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "results": sorted(rec.results, key=lambda r: int(r["id"][1:])),
        "observations": rec.observations,
    }
    (out / f"result-{args.cell}.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    bad = [r for r in result["results"] if r["status"] in ("fail", "not_run")]
    print(f"\n{len(result['results']) - len(bad)} of {len(result['results'])} tests passed or do not apply", flush=True)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
