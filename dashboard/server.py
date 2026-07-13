"""Live dashboard for feature_location_msa runs.

Polls the JSON-Lines event log written by the C++ `msa` tool's writer
thread and serves a small web UI showing per-thread status and a
searchable table of processed files with timing and captured log output.

Usage:
    python server.py --events-file ../msa/build/output/events.jsonl
"""

import argparse
import asyncio
import json
import time
from pathlib import Path

from fastapi import FastAPI
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

POLL_INTERVAL_SECONDS = 0.5

app = FastAPI()

STATIC_DIR = Path(__file__).parent / "static"


class DashboardState:
    def __init__(self, events_file: Path):
        self.events_file = events_file
        self.offset = 0
        self.run_id: str | None = None
        self.thread_count = 0
        self.total_families = 0
        self.run_started_at: float | None = None
        self.run_duration_ms: float | None = None
        self.threads: dict[int, dict] = {}
        self.files: dict[str, dict] = {}

    def reset_run(self, run_id: str, thread_count: int, total_families: int):
        self.run_id = run_id
        self.thread_count = thread_count
        self.total_families = total_families
        self.run_started_at = time.time()
        self.run_duration_ms = None
        self.threads = {}
        self.files = {}

    def apply(self, e: dict):
        kind = e.get("kind")

        if kind == "run_started":
            self.reset_run(
                e.get("run_id", ""),
                e.get("thread_count", 0),
                e.get("total_families", 0),
            )
            return

        if kind == "run_finished":
            self.run_duration_ms = e.get("duration_ms")
            return

        slot = e.get("thread_slot")
        family = e.get("family")

        if kind == "family_stage_started":
            self.threads[slot] = {"family": family, "stage": e.get("stage")}
            entry = self.files.setdefault(
                family,
                {"status": "running", "duration_ms": None, "log": []},
            )
            entry["status"] = "running"
            entry["stage"] = e.get("stage")
            return

        if kind == "family_stage_finished":
            if slot in self.threads and self.threads[slot].get("family") == family:
                self.threads[slot]["stage"] = e.get("stage")
            return

        if kind == "family_progress":
            stage_label = (
                f"{e.get('stage')} ({e.get('current_step')}/{e.get('total_steps')})"
            )
            if slot in self.threads and self.threads[slot].get("family") == family:
                self.threads[slot]["stage"] = stage_label
            entry = self.files.setdefault(
                family, {"status": "running", "duration_ms": None, "log": []}
            )
            entry["stage"] = stage_label
            return

        if kind == "family_variant_info":
            entry = self.files.setdefault(
                family, {"status": "running", "duration_ms": None, "log": []}
            )
            entry["variant_count"] = e.get("variant_count")
            entry["distinct_variant_count"] = e.get("distinct_variant_count")
            return

        if kind == "family_log":
            entry = self.files.setdefault(
                family, {"status": "running", "duration_ms": None, "log": []}
            )
            entry["log"].append(e.get("message", ""))
            return

        if kind == "family_finished":
            entry = self.files.setdefault(
                family, {"status": "running", "duration_ms": None, "log": []}
            )
            entry["status"] = "done"
            entry["duration_ms"] = e.get("duration_ms")
            if slot in self.threads and self.threads[slot].get("family") == family:
                self.threads[slot] = {"family": None, "stage": None}
            return

    def poll(self):
        if not self.events_file.exists():
            return

        size = self.events_file.stat().st_size
        if size < self.offset:
            # File was truncated -- a new run started.
            self.offset = 0

        with self.events_file.open("r") as f:
            f.seek(self.offset)
            for line in f:
                line = line.strip()
                if not line:
                    continue
                try:
                    self.apply(json.loads(line))
                except json.JSONDecodeError:
                    # Partial line written mid-flush; will be re-read next poll.
                    break
            self.offset = f.tell()

    def status(self):
        done = sum(1 for f in self.files.values() if f["status"] == "done")
        threads = [
            {
                "slot": slot,
                "family": info.get("family"),
                "stage": info.get("stage"),
            }
            for slot, info in sorted(self.threads.items())
        ]
        return {
            "run_id": self.run_id,
            "thread_count": self.thread_count,
            "total_families": self.total_families,
            "families_done": done,
            "run_duration_ms": self.run_duration_ms,
            "threads": threads,
        }

    def file_list(self, query: str = ""):
        query = query.lower()
        result = []
        for name, info in self.files.items():
            if query and query not in name.lower():
                continue
            result.append(
                {
                    "family": name,
                    "status": info["status"],
                    "stage": info.get("stage"),
                    "duration_ms": info["duration_ms"],
                    "log_lines": len(info["log"]),
                    "variant_count": info.get("variant_count"),
                    "distinct_variant_count": info.get("distinct_variant_count"),
                }
            )
        # Longest-running first; files still running (no duration yet) sort
        # before all finished ones, since they're the most relevant to watch.
        result.sort(
            key=lambda f: (
                f["duration_ms"] is not None,
                -(f["duration_ms"] or 0),
            )
        )
        return result


state: DashboardState


@app.on_event("startup")
async def start_polling():
    async def loop():
        while True:
            state.poll()
            await asyncio.sleep(POLL_INTERVAL_SECONDS)

    asyncio.create_task(loop())


@app.get("/api/status")
async def get_status():
    return state.status()


@app.get("/api/files")
async def get_files(q: str = ""):
    return state.file_list(q)


@app.get("/api/files/{family:path}")
async def get_file_detail(family: str):
    entry = state.files.get(family)
    if entry is None:
        return {"error": "not found"}
    return {"family": family, **entry}


@app.get("/")
async def index():
    return FileResponse(STATIC_DIR / "index.html")


app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--events-file",
        type=Path,
        default=Path("output/events.jsonl"),
        help="Path to the events.jsonl file written by msa (default: output/events.jsonl)",
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()

    global state
    state = DashboardState(args.events_file.resolve())

    import uvicorn

    uvicorn.run(app, host=args.host, port=args.port)


if __name__ == "__main__":
    main()
