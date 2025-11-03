# Weather REST API — v1

Base URL: `http://localhost:8080/api/v1`

Status: Stable v1. Breaking changes will be introduced under `/api/v2`.

## CORS (Cross-Origin Resource Sharing)

All responses include CORS headers:

- `Access-Control-Allow-Origin: *`
- `Access-Control-Allow-Methods: GET, OPTIONS`
- `Access-Control-Allow-Headers: Content-Type`

The server also replies to `OPTIONS` preflight with `204 No Content`.

## Error Model

All errors share this JSON structure:

```json
{ "error": { "code": 404, "message": "city not found" } }
```

- `code`: HTTP status code (integer)
- `message`: human-readable reason

## Limits

- Max city name length: 100 characters
- Latitude: -90.0 … 90.0
- Longitude: -180.0 … 180.0

Requests outside these limits return `400 Bad Request` with the error model.

## Demo Cities

These always return data:

- Stockholm (SE) — 59.3293, 18.0686
- Orebro (SE) — 59.2741, 15.2066
- Malmo (SE) — 55.6050, 13.0038
- Gothenburg (SE) — 57.7089, 11.9746
- Uppsala (SE) — 59.8586, 17.6389

---

## GET /api/v1/geo

City → Coordinates

Query Parameters:

- `city` (string, required): demo city name (case-sensitive)

Response 200 (application/json):

```json
{
	"city": "Malmo",
	"country": "SE",
	"lat": 55.605,
	"lon": 13.0038
}
```

Errors:

- 400 — `{ "error": { "code": 400, "message": "missing query param: city" } }`
- 400 — `{ "error": { "code": 400, "message": "city too long (max 100)" } }`
- 404 — `{ "error": { "code": 404, "message": "city not found" } }`

Example:

```bash
curl "http://localhost:8080/api/v1/geo?city=Malmo"
```

---

## GET /api/v1/weather

Coordinates → Current weather

Query Parameters:

- `lat` (number, required) — range -90..90
- `lon` (number, required) — range -180..180

Response 200 (application/json):

```json
{
	"tempC": 10.5,
	"description": "Sunny",
	"updatedAt": "2025-11-03T12:34:56Z"
}
```

Notes:

- `updatedAt` is in ISO-8601 format (UTC): `YYYY-MM-DDThh:mm:ssZ`
- Weather values are demo-only and vary slightly by city.

Errors:

- 400 — `{ "error": { "code": 400, "message": "missing query params: lat, lon" } }`
- 400 — `{ "error": { "code": 400, "message": "lat out of range (-90..90)" } }`
- 400 — `{ "error": { "code": 400, "message": "lon out of range (-180..180)" } }`

Example:

```bash
curl "http://localhost:8080/api/v1/weather?lat=55.6050&lon=13.0038"
```

---

## Update Frequency

Demo responses can be requested as often as needed. You might cache for 30–300 seconds.

## Security (Production Idea)

No auth is required in development. In production, add authentication (e.g., API key or OAuth2 bearer token) and enforce HTTPS behind a reverse proxy.

