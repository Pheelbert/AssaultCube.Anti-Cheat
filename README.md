## What is AssaultCube?
AssaultCube is a **FREE**, multiplayer, first-person shooter game, based on the
[CUBE engine](http://cubeengine.com/cube.php).

Taking place in realistic environments, with fast, arcade gameplay,
it's addictive and fun!

With efficient bandwidth usage, it's low-latency and can even run over a 56 Kbps
connection. It's tiny too, weighing in at a lightweight about 50 MB package
available for [Windows, Linux, Mac](https://assault.cubers.net/download.html).
On the correct settings, it can even run on old hardware (Pentium III and above).

![screenshot](https://user-images.githubusercontent.com/7680684/69888836-9a931d80-12b3-11ea-8123-bbf06908a96f.jpg)

## Features in a nutshell:

 * It's **FREE**.
 * Source code is available under a zlib-like open source license.
 * Low latency, it can even run across a 56 Kbps connection!
 * Lightweight size, only about 50 MB to download, plus additional maps
 average 20 KB each!
 * With the correct settings, it can run on old hardware
 (Pentium III and above).
 * Officially runs on most major systems (Windows: 2000/XP/Vista/7/8/10/11, Linux,
 macOS: 10.6+ 64-bit), and maybe even some
 [non-major ones](https://assault.cubers.net/docs/getstarted.html)?
 * Has a built in, in-game map editor to help players create their own maps and
 allows for co-operative editmode in realtime with others!
 * Features a single-player bot system.
 * Supports recording of your game by the "demo" system.
 * Contains many multiplayer game modes, including: Deathmatch, Survivor,
 Capture the Flag, Hunt the Flag, Keep the Flag, Pistol Frenzy, Last Swiss
 Standing & One-Shot One-Kill (plus team versions of these modes).
 * Comes pre-packaged with several dozen different maps!

## More info:

Most of this README was directly copied from the
[AssaultCube Homepage](https://assault.cubers.net), which should have everything
you need in relation to AssaultCube.

## Building the Client (Windows)

### Prerequisites

- **Visual Studio 2019+** (or Visual Studio Build Tools) with the **C++ Desktop** workload installed

### Using build.bat

Open a command prompt in the repository root and run:

```batch
build.bat                  # Build Release client (default)
build.bat debug            # Build Debug client
build.bat release          # Build Release client (explicit)
build.bat server           # Build Release server (Standalone)
build.bat server debug     # Build Debug server (Standalone Debug)
build.bat all              # Build Release client + server
build.bat all debug        # Build Debug client + server
build.bat clean            # Clean all build artifacts
```

The script auto-detects your MSBuild installation via `vswhere.exe`. Build output is placed in `bin_win32/`.

## Server Deployment with Docker

Docker builds the server from source on Linux (Ubuntu 22.04) and runs it alongside a web monitoring dashboard.

### Prerequisites

- [Docker](https://docs.docker.com/get-docker/) and [Docker Compose](https://docs.docker.com/compose/install/)

### Architecture

`docker compose` starts two services:

| Service | Port | Description |
|---------|------|-------------|
| **acserver** | UDP 28763 (game), UDP 28764 (server info) | AssaultCube game server |
| **dashboard** | TCP 8080 | Web dashboard for live player stats and anti-cheat telemetry |

### Deploy (first time)

Build images and start both services in the background:

```bash
docker compose up -d
```

Verify the services are running:

```bash
docker compose ps
```

### Configuration

Server config files live in the `config/` directory and are bind-mounted into the container, so edits take effect without rebuilding the image. Key files:

| File | Purpose |
|------|---------|
| `config/servercmdline.txt` | Startup flags (port, max clients, MOTD, etc.) |
| `config/maprot.cfg` | Map rotation |
| `config/serverpwd.cfg` | Server and admin passwords |
| `config/serverblacklist.cfg` | IP blacklist |
| `config/serverparameters.cfg` | Runtime parameters (re-read every 60 s) |

After editing config, restart the server to pick up the changes:

```bash
docker compose restart acserver
```

### Redeploy (after code changes)

When you modify server source code, rebuild the images and restart:

```bash
docker compose up -d --build
```

To rebuild only a specific service:

```bash
docker compose up -d --build acserver    # rebuild server only
docker compose up -d --build dashboard   # rebuild dashboard only
```

### Viewing Logs

```bash
docker compose logs -f              # all services
docker compose logs -f acserver     # server only
docker compose logs -f dashboard    # dashboard only
```

Server file logs are also persisted to the `logs/` directory on the host.

### Take Down

Stop and remove all containers:

```bash
docker compose down
```

To also remove the shared data volume:

```bash
docker compose down -v
```

## Contributing:

Learn [how to become a contributor and submit your own code](CONTRIBUTING.md)

## Redistribution:

You may redistribute AssaultCube in any way the license permits, such as the
free unmodified distribution of AssaultCube's source and binaries. If you have
any doubts, you can look at the
[license](https://assault.cubers.net/docs/license.html).

