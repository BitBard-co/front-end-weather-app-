# Weather REST API (C, v1)

This is a tiny HTTP server written in C that serves demo weather data for a frontend app. It is intentionally simple and beginner-friendly.

- Base URL: `http://localhost:8080/api/v1`
- Endpoints:
	- `GET /api/v1/geo?city=NAME` → returns coordinates for a demo city
	- `GET /api/v1/weather?lat=LAT&lon=LON` → returns current weather for coordinates
- CORS: enabled for `http://localhost:*` via `Access-Control-Allow-*` headers

See full API docs in `docs/api.md` and `docs/openapi.yaml`.

## Build and Run

This server uses POSIX sockets. On Windows, the easiest way is to run it under WSL. Linux and macOS work out of the box with `gcc`.

### Windows (WSL)

1) Open a PowerShell window in the project folder and run:

```powershell
wsl make -C "c:/Academy/Front_end_API/front-end-weather-app-"
wsl ./server
```

2) In another terminal, call the API (examples below) or open your frontend pointing at `http://localhost:8080`.

### Linux/macOS

```bash
make
./server
```

## Try it (curl)

```bash
# City → Coordinates
curl "http://localhost:8080/api/v1/geo?city=Malmo"

# Coordinates → Weather
curl "http://localhost:8080/api/v1/weather?lat=55.6050&lon=13.0038"
```

More commands: `docs/curl-examples.sh`

## Postman

Import `postman/collection.json` into Postman. It contains ready-made requests for the two endpoints.

## Versioning and Stability

This repository exposes a stable `v1` API. Breaking changes will be released under a new path, e.g. `/api/v2`.

## Demo Data

The server serves a small, built-in list of Swedish cities:

- Stockholm (SE) — 59.3293, 18.0686
- Orebro (SE) — 59.2741, 15.2066
- Malmo (SE) — 55.6050, 13.0038
- Gothenburg (SE) — 57.7089, 11.9746
- Uppsala (SE) — 59.8586, 17.6389

## Production Notes (Future)

- Security: Add API keys or tokens (e.g., `Authorization: Bearer <token>`) and enforce HTTPS behind a proxy.
- Real data: Replace demo logic with a real weather provider and cache responses.
- Windows native: Port sockets to Winsock2 (WSAStartup, closesocket, etc.).

