#!/usr/bin/env bash
set -euo pipefail

BASE="http://localhost:8080"

echo "City → Coordinates (Malmo)"
curl -sS "$BASE/api/v1/geo?city=Malmo" | jq . || true

echo
echo "Coordinates → Weather (55.6050, 13.0038)"
curl -sS "$BASE/api/v1/weather?lat=55.6050&lon=13.0038" | jq . || true

echo
echo "Example errors"
curl -sS "$BASE/api/v1/geo" || true
curl -sS "$BASE/api/v1/weather?lat=500&lon=13" || true
