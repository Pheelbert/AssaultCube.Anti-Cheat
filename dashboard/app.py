#!/usr/bin/env python3
"""AssaultCube Anti-Cheat Server Dashboard

A lightweight web dashboard that displays live server status by reading
the JSON status file exported by the AssaultCube server.

Usage:
    python3 app.py [--port 8080] [--status-file ../dashboard_status.json]
"""

import argparse
import json
import os
import time
from flask import Flask, jsonify, Response

app = Flask(__name__)

STATUS_FILE = os.environ.get("AC_STATUS_FILE", "dashboard_status.json")


def read_status():
    try:
        with open(STATUS_FILE, "r") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return None


@app.route("/")
def index():
    return Response(DASHBOARD_HTML, mimetype="text/html")


@app.route("/api/status")
def api_status():
    data = read_status()
    if data is None:
        return jsonify({"error": "Server status unavailable"}), 503
    return jsonify(data)


DASHBOARD_HTML = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>AssaultCube Server Dashboard</title>
<style>
:root {
    --bg: #0f1117;
    --surface: #1a1d27;
    --surface2: #232734;
    --border: #2d3140;
    --text: #e1e4ed;
    --text-dim: #8b90a0;
    --accent: #4f8ff7;
    --green: #3dd68c;
    --red: #f74f4f;
    --orange: #f7a94f;
    --yellow: #f7e44f;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
    font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
    background: var(--bg);
    color: var(--text);
    min-height: 100vh;
    line-height: 1.5;
}
.container { max-width: 1200px; margin: 0 auto; padding: 20px; }

header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 16px 0;
    border-bottom: 1px solid var(--border);
    margin-bottom: 24px;
}
header h1 {
    font-size: 1.4rem;
    font-weight: 600;
    letter-spacing: -0.02em;
}
header h1 span { color: var(--accent); }
.status-dot {
    display: inline-block;
    width: 8px; height: 8px;
    border-radius: 50%;
    margin-right: 8px;
    background: var(--green);
    animation: pulse 2s infinite;
}
.status-dot.offline { background: var(--red); animation: none; }
@keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.5; }
}
.header-right {
    display: flex;
    align-items: center;
    gap: 16px;
    color: var(--text-dim);
    font-size: 0.85rem;
}

/* Cards */
.cards {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 16px;
    margin-bottom: 24px;
}
.card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 16px 20px;
}
.card-label {
    font-size: 0.75rem;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: var(--text-dim);
    margin-bottom: 4px;
}
.card-value {
    font-size: 1.5rem;
    font-weight: 700;
    letter-spacing: -0.02em;
}
.card-value.green { color: var(--green); }
.card-value.accent { color: var(--accent); }
.card-value.orange { color: var(--orange); }

/* Tables */
.section {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 10px;
    margin-bottom: 24px;
    overflow: hidden;
}
.section-header {
    padding: 14px 20px;
    border-bottom: 1px solid var(--border);
    font-weight: 600;
    font-size: 0.95rem;
    display: flex;
    align-items: center;
    gap: 8px;
}
table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.88rem;
}
th {
    text-align: left;
    padding: 10px 16px;
    font-size: 0.75rem;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: var(--text-dim);
    border-bottom: 1px solid var(--border);
    font-weight: 600;
    white-space: nowrap;
}
td {
    padding: 10px 16px;
    border-bottom: 1px solid var(--border);
    white-space: nowrap;
}
tr:last-child td { border-bottom: none; }
tr:hover td { background: var(--surface2); }

.badge {
    display: inline-block;
    padding: 2px 8px;
    border-radius: 4px;
    font-size: 0.75rem;
    font-weight: 600;
}
.badge-green { background: rgba(61, 214, 140, 0.15); color: var(--green); }
.badge-red { background: rgba(247, 79, 79, 0.15); color: var(--red); }
.badge-orange { background: rgba(247, 169, 79, 0.15); color: var(--orange); }
.badge-blue { background: rgba(79, 143, 247, 0.15); color: var(--accent); }
.badge-dim { background: rgba(139, 144, 160, 0.15); color: var(--text-dim); }

.team-cla { color: #f74f4f; font-weight: 600; }
.team-rvsf { color: #4f8ff7; font-weight: 600; }
.team-spec { color: var(--text-dim); }

.empty-state {
    padding: 40px 20px;
    text-align: center;
    color: var(--text-dim);
}

.kd-ratio { font-weight: 600; }
.kd-positive { color: var(--green); }
.kd-negative { color: var(--red); }
.kd-neutral { color: var(--text-dim); }

.accuracy { color: var(--orange); font-weight: 600; }

/* Responsive */
@media (max-width: 768px) {
    .cards { grid-template-columns: repeat(2, 1fr); }
    table { font-size: 0.8rem; }
    th, td { padding: 8px 10px; }
    .hide-mobile { display: none; }
}

.error-banner {
    background: rgba(247, 79, 79, 0.1);
    border: 1px solid rgba(247, 79, 79, 0.3);
    border-radius: 10px;
    padding: 20px;
    text-align: center;
    color: var(--red);
    margin-bottom: 24px;
}
</style>
</head>
<body>
<div class="container">
    <header>
        <h1><span class="status-dot" id="statusDot"></span><span>AssaultCube</span> Server Dashboard</h1>
        <div class="header-right">
            <span id="serverDesc"></span>
            <span id="lastUpdate"></span>
        </div>
    </header>

    <div id="errorBanner" class="error-banner" style="display:none">
        Waiting for server data...
    </div>

    <div class="cards" id="cards">
        <div class="card">
            <div class="card-label">Players Online</div>
            <div class="card-value green" id="cardPlayers">-</div>
        </div>
        <div class="card">
            <div class="card-label">Current Map</div>
            <div class="card-value accent" id="cardMap">-</div>
        </div>
        <div class="card">
            <div class="card-label">Game Mode</div>
            <div class="card-value" id="cardMode">-</div>
        </div>
        <div class="card">
            <div class="card-label">Time Remaining</div>
            <div class="card-value orange" id="cardTime">-</div>
        </div>
        <div class="card">
            <div class="card-label">Server Uptime</div>
            <div class="card-value" id="cardUptime">-</div>
        </div>
        <div class="card">
            <div class="card-label">Master Mode</div>
            <div class="card-value" id="cardMM">-</div>
        </div>
    </div>

    <div class="section">
        <div class="section-header">Players</div>
        <div id="playersContent">
            <div class="empty-state">No players connected</div>
        </div>
    </div>

    <div class="section">
        <div class="section-header">Anti-Cheat Telemetry</div>
        <div id="acContent">
            <div class="empty-state">No anti-cheat data available</div>
        </div>
    </div>
</div>

<script>
function formatDuration(seconds) {
    if (seconds < 0) seconds = 0;
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    if (h > 0) return `${h}h ${m}m`;
    if (m > 0) return `${m}m ${s}s`;
    return `${s}s`;
}

function formatTimeRemaining(gameMillis, gameLimit) {
    if (gameLimit <= 0) return "--";
    const remainMs = gameLimit - gameMillis;
    if (remainMs <= 0) return "0:00";
    const mins = Math.floor(remainMs / 60000);
    const secs = Math.floor((remainMs % 60000) / 1000);
    return `${mins}:${secs.toString().padStart(2, '0')}`;
}

function teamClass(team) {
    const t = team.toUpperCase();
    if (t === 'CLA') return 'team-cla';
    if (t === 'RVSF') return 'team-rvsf';
    return 'team-spec';
}

function stateString(state) {
    switch(state) {
        case 0: return '<span class="badge badge-dim">Dead</span>';
        case 1: return '<span class="badge badge-green">Alive</span>';
        case 2: return '<span class="badge badge-dim">Dead</span>';
        case 5: return '<span class="badge badge-blue">Spectating</span>';
        case 6: return '<span class="badge badge-orange">Editing</span>';
        default: return '<span class="badge badge-dim">Unknown</span>';
    }
}

function roleString(role) {
    switch(role) {
        case 1: return '<span class="badge badge-orange">Admin</span>';
        default: return '';
    }
}

function kdRatio(frags, deaths) {
    if (deaths === 0) {
        if (frags === 0) return '<span class="kd-ratio kd-neutral">0.00</span>';
        return `<span class="kd-ratio kd-positive">${frags.toFixed(2)}</span>`;
    }
    const ratio = frags / deaths;
    const cls = ratio >= 1.0 ? 'kd-positive' : 'kd-negative';
    return `<span class="kd-ratio ${cls}">${ratio.toFixed(2)}</span>`;
}

function accuracy(shotdamage, damage) {
    if (shotdamage === 0) return '<span class="accuracy">-</span>';
    const pct = (damage / shotdamage * 100).toFixed(1);
    return `<span class="accuracy">${pct}%</span>`;
}

function renderPlayers(players) {
    const el = document.getElementById('playersContent');
    if (!players || players.length === 0) {
        el.innerHTML = '<div class="empty-state">No players connected</div>';
        return;
    }
    // Sort by frags descending
    const sorted = [...players].sort((a, b) => b.frags - a.frags);
    let html = `<table>
        <thead><tr>
            <th>#</th><th>Name</th><th>Team</th><th>Status</th>
            <th>Frags</th><th>Deaths</th><th>K/D</th>
            <th class="hide-mobile">Accuracy</th>
            <th class="hide-mobile">Flags</th>
            <th class="hide-mobile">TK</th>
            <th>Ping</th>
            <th class="hide-mobile">Time</th>
            <th class="hide-mobile">Country</th>
        </tr></thead><tbody>`;
    for (const p of sorted) {
        html += `<tr>
            <td>${p.cn}</td>
            <td><strong>${escapeHtml(p.name)}</strong> ${roleString(p.role)}</td>
            <td><span class="${teamClass(p.team)}">${escapeHtml(p.team)}</span></td>
            <td>${stateString(p.state)}</td>
            <td>${p.frags}</td>
            <td>${p.deaths}</td>
            <td>${kdRatio(p.frags, p.deaths)}</td>
            <td class="hide-mobile">${accuracy(p.shotdamage, p.damage)}</td>
            <td class="hide-mobile">${p.flagscore}</td>
            <td class="hide-mobile">${p.teamkills}</td>
            <td>${p.ping}ms</td>
            <td class="hide-mobile">${formatDuration(p.connected_seconds)}</td>
            <td class="hide-mobile">${escapeHtml(p.country)}</td>
        </tr>`;
    }
    html += '</tbody></table>';
    el.innerHTML = html;
}

function renderAntiCheat(players) {
    const el = document.getElementById('acContent');
    if (!players || players.length === 0) {
        el.innerHTML = '<div class="empty-state">No anti-cheat data available</div>';
        return;
    }
    let html = `<table>
        <thead><tr>
            <th>Player</th>
            <th>Kernel AC</th>
            <th>Windows Version</th>
            <th>Time Connected</th>
        </tr></thead><tbody>`;
    for (const p of players) {
        const hasAC = p.anticheat && p.anticheat.has_kernel_ac;
        const acBadge = hasAC
            ? '<span class="badge badge-green">Active</span>'
            : '<span class="badge badge-red">Not Detected</span>';
        let winVer = '-';
        if (hasAC && p.anticheat.windows_build > 0) {
            winVer = `${p.anticheat.windows_major}.${p.anticheat.windows_minor}.${p.anticheat.windows_build}`;
        }
        html += `<tr>
            <td><strong>${escapeHtml(p.name)}</strong></td>
            <td>${acBadge}</td>
            <td>${winVer}</td>
            <td>${formatDuration(p.connected_seconds)}</td>
        </tr>`;
    }
    html += '</tbody></table>';
    el.innerHTML = html;
}

function escapeHtml(s) {
    const div = document.createElement('div');
    div.textContent = s;
    return div.innerHTML;
}

let lastError = false;

async function update() {
    try {
        const res = await fetch('/api/status');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        if (data.error) throw new Error(data.error);

        lastError = false;
        document.getElementById('errorBanner').style.display = 'none';
        document.getElementById('statusDot').className = 'status-dot';

        const s = data.server;
        document.getElementById('serverDesc').textContent = s.description || '';
        document.getElementById('cardPlayers').textContent = `${s.players_online} / ${s.max_players}`;
        document.getElementById('cardMap').textContent = s.map || '-';
        document.getElementById('cardMode').textContent = `${s.mode} - ${s.mode_full}`;
        document.getElementById('cardTime').textContent = formatTimeRemaining(s.game_millis, s.game_limit);
        document.getElementById('cardUptime').textContent = formatDuration(s.uptime_seconds);
        document.getElementById('cardMM').textContent = s.mastermode || '-';

        renderPlayers(data.players);
        renderAntiCheat(data.players);

        document.getElementById('lastUpdate').textContent = 'Updated ' + new Date().toLocaleTimeString();
    } catch (e) {
        if (!lastError) {
            document.getElementById('errorBanner').style.display = 'block';
            document.getElementById('errorBanner').textContent = 'Waiting for server data... (' + e.message + ')';
            document.getElementById('statusDot').className = 'status-dot offline';
        }
        lastError = true;
    }
}

// Refresh every 2 seconds
update();
setInterval(update, 2000);
</script>
</body>
</html>"""


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="AssaultCube Server Dashboard")
    parser.add_argument("--port", type=int, default=8080, help="Dashboard web port (default: 8080)")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address (default: 0.0.0.0)")
    parser.add_argument("--status-file", default=None, help="Path to dashboard_status.json")
    args = parser.parse_args()

    if args.status_file:
        STATUS_FILE = args.status_file

    print(f"AssaultCube Dashboard starting on http://{args.host}:{args.port}")
    print(f"Reading status from: {os.path.abspath(STATUS_FILE)}")
    app.run(host=args.host, port=args.port, debug=False)
