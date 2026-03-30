## Building the Windows Client

### Prerequisites

- **Visual Studio 2019+** (or Visual Studio Build Tools) with the **C++ Desktop** workload installed

### Using build_client.bat

Open a command prompt in the repository root and run:

```batch
build_client.bat              # Build Release client (default)
build_client.bat debug        # Build Debug client
build_client.bat release      # Build Release client (explicit)
build_client.bat clean        # Clean all build artifacts
```

The script auto-detects your MSBuild installation via `vswhere.exe`. Build output is placed in `bin_win32/`.

## Building the Anti-Cheat Driver (Windows)

### Prerequisites

- **Visual Studio 2019+** with the **C++ Desktop** workload
- **Windows Driver Kit (WDK)** matching your Windows SDK version

### Build

The driver project is at `source/src/anticheat/driver/phanticheat.vcxproj`. Build it with MSBuild (x64 only):

```batch
msbuild source\src\anticheat\driver\phanticheat.vcxproj /p:Configuration=Release /p:Platform=x64
```

Or open the `.vcxproj` in Visual Studio and build from there. Output is placed in `bin_win32/` as `phanticheat.sys`, alongside `ac_client.exe`.

## Linux Server Deployment with Docker

Docker builds the server from source on Linux (Ubuntu 22.04) and runs it alongside a web monitoring dashboard.

### Prerequisites

- [Docker](https://docs.docker.com/get-docker/) and [Docker Compose](https://docs.docker.com/compose/install/)

### Architecture

`docker compose` starts two services:

| Service | Port | Description |
|---------|------|-------------|
| **acserver** | UDP 28763 (game), UDP 28764 (server info) | Game server |
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
