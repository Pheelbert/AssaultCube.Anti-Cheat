#!/usr/bin/env python3
"""AssaultCube Anti-Cheat Server Dashboard

A lightweight web dashboard that displays live server status by reading
the JSON status file exported by the AssaultCube server.

Tracks player connection history and telemetry events in-memory.

Usage:
    python3 app.py [--port 8080] [--status-file ../dashboard_status.json]
"""

import argparse
import json
import os
import time
import threading
from flask import Flask, jsonify, Response

app = Flask(__name__)

STATUS_FILE = os.environ.get("AC_STATUS_FILE", "dashboard_status.json")

# In-memory state for tracking player history and telemetry
player_history = {}  # keyed by (name, ip-hash via cn) -> player data + status
telemetry_log = []   # list of telemetry events with timestamps
_state_lock = threading.Lock()
_last_server_data = None


def read_status():
    try:
        with open(STATUS_FILE, "r") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        return None


def _player_key(player):
    """Generate a stable key for a player based on name + cn for the session."""
    return f"{player.get('name', 'unknown')}_{player.get('cn', -1)}"


def _update_tracking():
    """Called periodically to update player history and telemetry log."""
    global _last_server_data
    data = read_status()
    if data is None:
        return

    _last_server_data = data
    now = time.time()
    current_keys = set()

    with _state_lock:
        for p in data.get("players", []):
            key = _player_key(p)
            current_keys.add(key)

            prev = player_history.get(key)
            player_history[key] = {
                **p,
                "status": "connected",
                "first_seen": prev["first_seen"] if prev else now,
                "last_seen": now,
            }

            # Generate telemetry events from state changes
            ac = p.get("anticheat", {})
            if ac.get("has_kernel_ac"):
                prev_ac = prev.get("anticheat", {}) if prev else {}
                prev_hb_count = prev_ac.get("heartbeat_count", 0)
                cur_hb_count = ac.get("heartbeat_count", 0)

                # Detect new heartbeats
                if cur_hb_count > prev_hb_count:
                    for _ in range(cur_hb_count - prev_hb_count):
                        telemetry_log.append({
                            "timestamp": now,
                            "player": p.get("name", "unknown"),
                            "cn": p.get("cn", -1),
                            "type": "heartbeat",
                            "details": {
                                "driver_version": ac.get("driver_version", 0),
                                "uptime_seconds": ac.get("uptime_seconds", 0),
                                "scan_count": ac.get("scan_count", 0),
                            },
                        })

                # Detect windows version report (first time or change)
                if ac.get("windows_build", 0) > 0:
                    prev_build = prev_ac.get("windows_build", 0) if prev else 0
                    if prev_build == 0:
                        telemetry_log.append({
                            "timestamp": now,
                            "player": p.get("name", "unknown"),
                            "cn": p.get("cn", -1),
                            "type": "windows_version",
                            "details": {
                                "windows_major": ac.get("windows_major", 0),
                                "windows_minor": ac.get("windows_minor", 0),
                                "windows_build": ac.get("windows_build", 0),
                            },
                        })

                # Detect AC first detection
                if not prev or not prev.get("anticheat", {}).get("has_kernel_ac"):
                    telemetry_log.append({
                        "timestamp": now,
                        "player": p.get("name", "unknown"),
                        "cn": p.get("cn", -1),
                        "type": "ac_detected",
                        "details": {"has_kernel_ac": True},
                    })

        # Mark disconnected players
        for key, pdata in player_history.items():
            if key not in current_keys and pdata["status"] == "connected":
                pdata["status"] = "disconnected"
                pdata["disconnected_at"] = now
                telemetry_log.append({
                    "timestamp": now,
                    "player": pdata.get("name", "unknown"),
                    "cn": pdata.get("cn", -1),
                    "type": "disconnect",
                    "details": {},
                })

        # Cap telemetry log at 1000 entries
        if len(telemetry_log) > 1000:
            del telemetry_log[:len(telemetry_log) - 1000]


def _tracking_loop():
    """Background thread that polls the status file."""
    while True:
        try:
            _update_tracking()
        except Exception:
            pass
        time.sleep(2)


# Start background tracking thread
_tracker = threading.Thread(target=_tracking_loop, daemon=True)
_tracker.start()


@app.route("/")
def index():
    return Response(DASHBOARD_HTML, mimetype="text/html")


@app.route("/api/status")
def api_status():
    """Full status including player history and telemetry log."""
    with _state_lock:
        # Build player list: connected first, then disconnected
        players = sorted(
            player_history.values(),
            key=lambda p: (0 if p["status"] == "connected" else 1, -p.get("last_seen", 0)),
        )
        return jsonify({
            "server": _last_server_data.get("server", {}) if _last_server_data else {},
            "players": players,
            "telemetry": telemetry_log[-500:],  # last 500 events
            "has_data": _last_server_data is not None,
        })


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
    --surface3: #2a2e3d;
    --border: #2d3140;
    --text: #e1e4ed;
    --text-dim: #8b90a0;
    --accent: #4f8ff7;
    --green: #3dd68c;
    --red: #f74f4f;
    --orange: #f7a94f;
    --yellow: #f7e44f;
    --purple: #a77bfc;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
    font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;
    background: var(--bg);
    color: var(--text);
    min-height: 100vh;
    line-height: 1.5;
}
.container { max-width: 1300px; margin: 0 auto; padding: 20px; }

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

/* Sections */
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

/* Tables */
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

/* Player rows */
.player-row { cursor: pointer; transition: background 0.15s; }
.player-row:hover td { background: var(--surface2); }
.player-row.expanded td { background: var(--surface2); border-bottom-color: var(--surface2); }
.player-row td:first-child { position: relative; padding-left: 32px; }
.player-row td:first-child::before {
    content: '\25B6';
    position: absolute;
    left: 12px;
    top: 50%;
    transform: translateY(-50%);
    font-size: 0.6rem;
    color: var(--text-dim);
    transition: transform 0.2s;
}
.player-row.expanded td:first-child::before {
    transform: translateY(-50%) rotate(90deg);
    color: var(--accent);
}
.player-detail {
    display: none;
}
.player-detail.visible {
    display: table-row;
}
.player-detail td {
    padding: 0;
    background: var(--surface2);
}
.detail-panel {
    padding: 16px 20px;
}
.detail-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
    gap: 16px;
}
.detail-card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 14px 16px;
}
.detail-card h4 {
    font-size: 0.75rem;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: var(--text-dim);
    margin-bottom: 10px;
}
.detail-row {
    display: flex;
    justify-content: space-between;
    padding: 4px 0;
    font-size: 0.85rem;
}
.detail-row .label { color: var(--text-dim); }
.detail-row .value { font-weight: 600; }

/* Badges */
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
.badge-purple { background: rgba(167, 123, 252, 0.15); color: var(--purple); }

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

/* Telemetry filter controls */
.filter-bar {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 10px 20px;
    border-bottom: 1px solid var(--border);
    flex-wrap: wrap;
}
.filter-bar label {
    font-size: 0.8rem;
    color: var(--text-dim);
    margin-right: 4px;
}
.filter-chip {
    display: inline-flex;
    align-items: center;
    gap: 4px;
    padding: 4px 10px;
    border-radius: 6px;
    font-size: 0.78rem;
    font-weight: 500;
    cursor: pointer;
    border: 1px solid var(--border);
    background: var(--surface);
    color: var(--text-dim);
    transition: all 0.15s;
    user-select: none;
}
.filter-chip:hover { border-color: var(--accent); color: var(--text); }
.filter-chip.active {
    background: rgba(79, 143, 247, 0.15);
    border-color: var(--accent);
    color: var(--accent);
}
.filter-chip .chip-count {
    background: var(--surface2);
    padding: 1px 5px;
    border-radius: 4px;
    font-size: 0.7rem;
}
.filter-chip.active .chip-count {
    background: rgba(79, 143, 247, 0.25);
}

.telemetry-type {
    font-weight: 600;
    font-size: 0.78rem;
    text-transform: uppercase;
}
.type-heartbeat { color: var(--text-dim); }
.type-windows_version { color: var(--accent); }
.type-ac_detected { color: var(--green); }
.type-disconnect { color: var(--red); }

/* Responsive */
@media (max-width: 768px) {
    .cards { grid-template-columns: repeat(2, 1fr); }
    table { font-size: 0.8rem; }
    th, td { padding: 8px 10px; }
    .hide-mobile { display: none; }
    .detail-grid { grid-template-columns: 1fr; }
    .player-row td:first-child { padding-left: 26px; }
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

.disconnected-row td {
    opacity: 0.5;
}
.disconnected-row:hover td {
    opacity: 0.8;
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
        <div id="telemetryFilterBar"></div>
        <div id="telemetryContent">
            <div class="empty-state">No telemetry events recorded</div>
        </div>
    </div>
</div>

<script>
// State
let expandedPlayers = new Set();
let telemetryFilters = {
    heartbeat: false,
    windows_version: true,
    ac_detected: true,
    disconnect: true,
};
let currentData = null;

function formatDuration(seconds) {
    if (seconds < 0) seconds = 0;
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = Math.floor(seconds % 60);
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

function formatTimestamp(ts) {
    return new Date(ts * 1000).toLocaleTimeString();
}

function formatTimeAgo(ts) {
    const diff = Math.floor(Date.now() / 1000 - ts);
    if (diff < 60) return `${diff}s ago`;
    if (diff < 3600) return `${Math.floor(diff / 60)}m ago`;
    return `${Math.floor(diff / 3600)}h ${Math.floor((diff % 3600) / 60)}m ago`;
}

function teamClass(team) {
    const t = (team || '').toUpperCase();
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
    if (role === 1) return ' <span class="badge badge-orange">Admin</span>';
    return '';
}

function connectionBadge(status) {
    if (status === 'connected') return '<span class="badge badge-green">Online</span>';
    return '<span class="badge badge-dim">Disconnected</span>';
}

function kdRatio(frags, deaths) {
    if (deaths === 0) {
        if (frags === 0) return '<span class="kd-ratio kd-neutral">0.00</span>';
        return '<span class="kd-ratio kd-positive">' + frags.toFixed(2) + '</span>';
    }
    const ratio = frags / deaths;
    const cls = ratio >= 1.0 ? 'kd-positive' : 'kd-negative';
    return '<span class="kd-ratio ' + cls + '">' + ratio.toFixed(2) + '</span>';
}

function accuracyStr(shotdamage, damage) {
    if (shotdamage === 0) return '<span class="accuracy">-</span>';
    const pct = (damage / shotdamage * 100).toFixed(1);
    return '<span class="accuracy">' + pct + '%</span>';
}

function hitAccuracy(shots, hits) {
    if (!shots) return '-';
    return (hits / shots * 100).toFixed(1) + '%';
}

function escapeHtml(s) {
    if (!s) return '';
    const div = document.createElement('div');
    div.textContent = s;
    return div.innerHTML;
}

function togglePlayer(key) {
    if (expandedPlayers.has(key)) {
        expandedPlayers.delete(key);
    } else {
        expandedPlayers.add(key);
    }
    if (currentData) renderPlayers(currentData.players);
}

function toggleFilter(type) {
    telemetryFilters[type] = !telemetryFilters[type];
    if (currentData) renderTelemetry(currentData.telemetry);
}

function renderPlayers(players) {
    const el = document.getElementById('playersContent');
    if (!players || players.length === 0) {
        el.innerHTML = '<div class="empty-state">No players connected</div>';
        return;
    }

    let html = '<table><thead><tr>';
    html += '<th>Name</th><th>Status</th><th>Team</th>';
    html += '<th>Frags</th><th>Deaths</th><th>K/D</th>';
    html += '<th class="hide-mobile">Ping</th>';
    html += '<th class="hide-mobile">AC</th>';
    html += '<th class="hide-mobile">Time</th>';
    html += '</tr></thead><tbody>';

    for (const p of players) {
        const key = escapeHtml(p.name) + '_' + p.cn;
        const isExpanded = expandedPlayers.has(key);
        const isDisconnected = p.status === 'disconnected';
        const rowClass = 'player-row' + (isExpanded ? ' expanded' : '') + (isDisconnected ? ' disconnected-row' : '');

        const hasAC = p.anticheat && p.anticheat.has_kernel_ac;
        const acBadge = hasAC
            ? '<span class="badge badge-green">Active</span>'
            : '<span class="badge badge-red">None</span>';

        const timeStr = isDisconnected
            ? formatTimeAgo(p.last_seen)
            : formatDuration(p.connected_seconds || 0);

        html += '<tr class="' + rowClass + '" onclick="togglePlayer(\'' + key.replace(/'/g, "\\'") + '\')">';
        html += '<td><strong>' + escapeHtml(p.name) + '</strong>' + roleString(p.role) + '</td>';
        html += '<td>' + (isDisconnected ? connectionBadge('disconnected') : stateString(p.state)) + '</td>';
        html += '<td><span class="' + teamClass(p.team) + '">' + escapeHtml(p.team || '') + '</span></td>';
        html += '<td>' + (p.frags || 0) + '</td>';
        html += '<td>' + (p.deaths || 0) + '</td>';
        html += '<td>' + kdRatio(p.frags || 0, p.deaths || 0) + '</td>';
        html += '<td class="hide-mobile">' + (isDisconnected ? '-' : (p.ping || 0) + 'ms') + '</td>';
        html += '<td class="hide-mobile">' + acBadge + '</td>';
        html += '<td class="hide-mobile">' + timeStr + '</td>';
        html += '</tr>';

        // Detail row
        html += '<tr class="player-detail' + (isExpanded ? ' visible' : '') + '">';
        html += '<td colspan="9">' + renderPlayerDetail(p) + '</td>';
        html += '</tr>';
    }

    html += '</tbody></table>';
    el.innerHTML = html;
}

function renderPlayerDetail(p) {
    const ac = p.anticheat || {};
    const hasAC = ac.has_kernel_ac;
    const shots = p.session_shotcount || 0;
    const hits = p.session_hits || 0;
    const missed = shots - hits;
    const isDisconnected = p.status === 'disconnected';

    let html = '<div class="detail-panel"><div class="detail-grid">';

    // Combat stats
    html += '<div class="detail-card"><h4>Combat Stats</h4>';
    html += detailRow('Frags', p.frags || 0);
    html += detailRow('Deaths', p.deaths || 0);
    html += detailRow('K/D Ratio', kdRatio(p.frags || 0, p.deaths || 0));
    html += detailRow('Accuracy', accuracyStr(p.shotdamage || 0, p.damage || 0));
    html += detailRow('Flag Score', p.flagscore || 0);
    html += detailRow('Team Kills', p.teamkills || 0);
    html += detailRow('Damage Dealt', p.damage || 0);
    html += '</div>';

    // Session stats
    html += '<div class="detail-card"><h4>Session Stats</h4>';
    html += detailRow('Session Kills', p.session_frags || 0);
    html += detailRow('Session Deaths', p.session_deaths || 0);
    html += detailRow('Shots Fired', shots);
    html += detailRow('Shots Hit', hits);
    html += detailRow('Shots Missed', missed);
    html += detailRow('Hit Accuracy', '<span class="accuracy">' + hitAccuracy(shots, hits) + '</span>');
    html += '</div>';

    // Connection info
    html += '<div class="detail-card"><h4>Connection</h4>';
    html += detailRow('Client #', p.cn);
    html += detailRow('Country', escapeHtml(p.country || '-'));
    html += detailRow('Ping', isDisconnected ? '-' : (p.ping || 0) + 'ms');
    html += detailRow('Connected', formatDuration(p.connected_seconds || 0));
    if (isDisconnected) {
        html += detailRow('Last Seen', formatTimeAgo(p.last_seen));
    }
    html += '</div>';

    // Anti-cheat
    html += '<div class="detail-card"><h4>Anti-Cheat</h4>';
    if (hasAC) {
        html += detailRow('Kernel AC', '<span class="badge badge-green">Active</span>');
        if (ac.windows_build > 0) {
            html += detailRow('Windows', ac.windows_major + '.' + ac.windows_minor + '.' + ac.windows_build);
        }
        if (ac.driver_version > 0) {
            html += detailRow('Driver Version', ac.driver_version);
        }
        if (ac.uptime_seconds > 0) {
            html += detailRow('Driver Uptime', formatDuration(ac.uptime_seconds));
        }
        if (ac.scan_count > 0) {
            html += detailRow('Scan Count', ac.scan_count);
        }
        html += detailRow('Heartbeats', ac.heartbeat_count || 0);
    } else {
        html += detailRow('Kernel AC', '<span class="badge badge-red">Not Detected</span>');
    }
    html += '</div>';

    html += '</div></div>';
    return html;
}

function detailRow(label, value) {
    return '<div class="detail-row"><span class="label">' + label + '</span><span class="value">' + value + '</span></div>';
}

function renderTelemetry(events) {
    const filterBar = document.getElementById('telemetryFilterBar');
    const el = document.getElementById('telemetryContent');

    if (!events || events.length === 0) {
        filterBar.innerHTML = '';
        el.innerHTML = '<div class="empty-state">No telemetry events recorded</div>';
        return;
    }

    // Count by type
    const counts = {};
    for (const e of events) {
        counts[e.type] = (counts[e.type] || 0) + 1;
    }

    // Render filter bar
    const typeLabels = {
        heartbeat: 'Heartbeat',
        windows_version: 'Windows Version',
        ac_detected: 'AC Detected',
        disconnect: 'Disconnect',
    };
    let filterHtml = '<div class="filter-bar"><label>Show:</label>';
    for (const [type, label] of Object.entries(typeLabels)) {
        if (!counts[type]) continue;
        const active = telemetryFilters[type] ? ' active' : '';
        filterHtml += '<span class="filter-chip' + active + '" onclick="toggleFilter(\'' + type + '\')">';
        filterHtml += label + ' <span class="chip-count">' + (counts[type] || 0) + '</span>';
        filterHtml += '</span>';
    }
    filterHtml += '</div>';
    filterBar.innerHTML = filterHtml;

    // Filter events
    const filtered = events.filter(e => telemetryFilters[e.type]);

    if (filtered.length === 0) {
        el.innerHTML = '<div class="empty-state">No events match current filters (adjust filters above)</div>';
        return;
    }

    // Render table (newest first)
    let html = '<table><thead><tr>';
    html += '<th>Time</th><th>Player</th><th>Type</th><th>Details</th>';
    html += '</tr></thead><tbody>';

    const reversed = [...filtered].reverse();
    for (const e of reversed) {
        const typeClass = 'type-' + e.type;
        html += '<tr>';
        html += '<td>' + formatTimestamp(e.timestamp) + '</td>';
        html += '<td><strong>' + escapeHtml(e.player) + '</strong></td>';
        html += '<td><span class="telemetry-type ' + typeClass + '">' + escapeHtml(e.type.replace(/_/g, ' ')) + '</span></td>';
        html += '<td>' + formatTelemetryDetails(e) + '</td>';
        html += '</tr>';
    }

    html += '</tbody></table>';
    el.innerHTML = html;
}

function formatTelemetryDetails(event) {
    const d = event.details || {};
    switch (event.type) {
        case 'heartbeat':
            return 'Driver v' + d.driver_version + ', uptime ' + formatDuration(d.uptime_seconds || 0) + ', ' + (d.scan_count || 0) + ' scans';
        case 'windows_version':
            return 'Windows ' + d.windows_major + '.' + d.windows_minor + '.' + d.windows_build;
        case 'ac_detected':
            return 'Kernel anti-cheat first detected';
        case 'disconnect':
            return 'Player disconnected';
        default:
            return JSON.stringify(d);
    }
}

let lastError = false;

async function update() {
    try {
        const res = await fetch('/api/status');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        if (!data.has_data) throw new Error('No server data');

        lastError = false;
        currentData = data;
        document.getElementById('errorBanner').style.display = 'none';
        document.getElementById('statusDot').className = 'status-dot';

        const s = data.server;
        document.getElementById('serverDesc').textContent = s.description || '';
        document.getElementById('cardPlayers').textContent =
            (s.players_online || 0) + ' / ' + (s.max_players || 0);
        document.getElementById('cardMap').textContent = s.map || '-';
        document.getElementById('cardMode').textContent = (s.mode || '-') + ' - ' + (s.mode_full || '');
        document.getElementById('cardTime').textContent = formatTimeRemaining(s.game_millis || 0, s.game_limit || 0);
        document.getElementById('cardUptime').textContent = formatDuration(s.uptime_seconds || 0);
        document.getElementById('cardMM').textContent = s.mastermode || '-';

        renderPlayers(data.players);
        renderTelemetry(data.telemetry);

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
