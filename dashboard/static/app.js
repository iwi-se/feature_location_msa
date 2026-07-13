const POLL_MS = 1000;
let searchQuery = "";
const openLogs = new Set();
const logCache = new Map(); // family -> rendered log text

function fmtDuration(ms) {
  if (ms === null || ms === undefined) return "-";
  if (ms < 1000) return `${ms.toFixed(1)} ms`;
  return `${(ms / 1000).toFixed(2)} s`;
}

async function refreshStatus() {
  const res = await fetch("/api/status");
  const s = await res.json();

  const summary = document.getElementById("summary");
  if (!s.run_id) {
    summary.textContent = "Waiting for a run...";
  } else {
    const runState = s.run_duration_ms !== null ? "finished" : "running";
    summary.textContent =
      `Run ${s.run_id} (${runState}) — ${s.thread_count} threads — ` +
      `${s.families_done}/${s.total_families} files done` +
      (s.run_duration_ms !== null ? ` in ${fmtDuration(s.run_duration_ms)}` : "");
  }

  const threadsEl = document.getElementById("threads");
  threadsEl.innerHTML = "";
  for (const t of s.threads) {
    const idle = !t.family;
    const card = document.createElement("div");
    card.className = "thread-card" + (idle ? " idle" : "");
    card.innerHTML = `
      <div class="slot">thread ${t.slot}</div>
      <div class="family">${idle ? "idle" : escapeHtml(t.family)}</div>
      <div class="stage">${idle ? "" : escapeHtml(t.stage || "")}</div>
    `;
    threadsEl.appendChild(card);
  }
}

function escapeHtml(str) {
  const div = document.createElement("div");
  div.textContent = str;
  return div.innerHTML;
}

function fmtVariants(f) {
  if (f.distinct_variant_count === null || f.distinct_variant_count === undefined) {
    return "-";
  }
  if (f.distinct_variant_count === f.variant_count) {
    return `${f.variant_count}`;
  }
  return `${f.distinct_variant_count} distinct / ${f.variant_count}`;
}

async function refreshFiles() {
  const res = await fetch(`/api/files?q=${encodeURIComponent(searchQuery)}`);
  const files = await res.json();

  const tbody = document.getElementById("file-rows");
  tbody.innerHTML = "";

  if (files.length === 0) {
    const tr = document.createElement("tr");
    tr.innerHTML = `<td colspan="6" class="empty">No files match.</td>`;
    tbody.appendChild(tr);
    return;
  }

  for (const f of files) {
    const row = document.createElement("tr");
    row.className = "file-row";
    row.innerHTML = `
      <td>${escapeHtml(f.family)}</td>
      <td class="status-${f.status}">${f.status}</td>
      <td>${escapeHtml(f.stage || "")}</td>
      <td>${fmtVariants(f)}</td>
      <td>${fmtDuration(f.duration_ms)}</td>
      <td>${f.log_lines} lines</td>
    `;

    const logRow = document.createElement("tr");
    const isOpen = openLogs.has(f.family);
    logRow.className = "log-panel" + (isOpen ? " open" : "");
    const logCell = document.createElement("td");
    logCell.colSpan = 6;
    logCell.textContent = logCache.get(f.family) || "Loading...";
    logRow.appendChild(logCell);

    const loadLog = async () => {
      const detail = await (
        await fetch(`/api/files/${encodeURIComponent(f.family)}`)
      ).json();
      const text = (detail.log || []).join("\n") || "(no log output)";
      logCache.set(f.family, text);
      logCell.textContent = text;
    };

    if (isOpen) {
      loadLog();
    }

    row.addEventListener("click", () => {
      if (openLogs.has(f.family)) {
        openLogs.delete(f.family);
        logRow.classList.remove("open");
        return;
      }
      openLogs.add(f.family);
      logRow.classList.add("open");
      loadLog();
    });

    tbody.appendChild(row);
    tbody.appendChild(logRow);
  }
}

document.getElementById("search").addEventListener("input", (e) => {
  searchQuery = e.target.value;
  refreshFiles();
});

async function tick() {
  await Promise.all([refreshStatus(), refreshFiles()]);
}

tick();
setInterval(tick, POLL_MS);
