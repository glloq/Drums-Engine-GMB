#!/usr/bin/env bash
# =============================================================================
# Drums-Engine-MIDI  --  Automated REST API test suite
# =============================================================================
# Usage:
#   ./tests/test_api.sh [BASE_URL] [API_TOKEN]
#
# Arguments:
#   BASE_URL   - Device URL (default: http://midipercussion.local)
#   API_TOKEN  - Bearer token for authenticated endpoints.
#                If omitted the script fetches it from GET /api/auth-token.
#
# Exit codes:
#   0  - all tests passed
#   1  - one or more tests failed
# =============================================================================

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------
BASE_URL="${1:-http://midipercussion.local}"
API_TOKEN="${2:-}"

# Strip trailing slash
BASE_URL="${BASE_URL%/}"

CURL_TIMEOUT=10          # seconds per request
TOTAL=0
PASSED=0
FAILED=0

# ---------------------------------------------------------------------------
# Colour helpers  (gracefully degrade when stdout is not a terminal)
# ---------------------------------------------------------------------------
if [[ -t 1 ]]; then
  GREEN='\033[0;32m'
  RED='\033[0;31m'
  YELLOW='\033[0;33m'
  CYAN='\033[0;36m'
  BOLD='\033[1m'
  RESET='\033[0m'
else
  GREEN='' RED='' YELLOW='' CYAN='' BOLD='' RESET=''
fi

# ---------------------------------------------------------------------------
# Test helpers
# ---------------------------------------------------------------------------

# run_test <label> <expected_http_code> <curl_args...>
#   Performs the curl call, compares the HTTP status code.
#   Stores the response body in $RESPONSE_BODY and status in $RESPONSE_CODE
#   for optional follow-up assertions.
run_test() {
  local label="$1"
  local expected="$2"
  shift 2

  TOTAL=$((TOTAL + 1))

  # --silent --show-error: no progress bar, but show errors
  # --output captures body, --write-out captures status code
  local tmpfile
  tmpfile=$(mktemp)

  RESPONSE_CODE=$(curl --silent --show-error \
    --max-time "$CURL_TIMEOUT" \
    --output "$tmpfile" \
    --write-out '%{http_code}' \
    "$@" 2>/dev/null) || RESPONSE_CODE="000"

  RESPONSE_BODY=$(<"$tmpfile")
  rm -f "$tmpfile"

  if [[ "$RESPONSE_CODE" == "$expected" ]]; then
    PASSED=$((PASSED + 1))
    printf "${GREEN}  PASS${RESET}  %-60s [%s]\n" "$label" "$RESPONSE_CODE"
  else
    FAILED=$((FAILED + 1))
    printf "${RED}  FAIL${RESET}  %-60s [got %s, expected %s]\n" "$label" "$RESPONSE_CODE" "$expected"
    # Show truncated body on failure for debugging
    if [[ -n "$RESPONSE_BODY" ]]; then
      printf "        Body: %.200s\n" "$RESPONSE_BODY"
    fi
  fi
}

# assert_body_contains <label> <substring>
#   Call immediately after run_test to additionally check response body.
assert_body_contains() {
  local label="$1"
  local substring="$2"

  TOTAL=$((TOTAL + 1))

  if echo "$RESPONSE_BODY" | grep -q "$substring"; then
    PASSED=$((PASSED + 1))
    printf "${GREEN}  PASS${RESET}  %-60s [body contains '%s']\n" "$label" "$substring"
  else
    FAILED=$((FAILED + 1))
    printf "${RED}  FAIL${RESET}  %-60s [body missing '%s']\n" "$label" "$substring"
    printf "        Body: %.200s\n" "$RESPONSE_BODY"
  fi
}

# auth_header: returns the -H flag for Bearer auth
auth_header() {
  echo "Authorization: Bearer ${API_TOKEN}"
}

# ---------------------------------------------------------------------------
# Banner
# ---------------------------------------------------------------------------
echo ""
printf "${BOLD}${CYAN}======================================================${RESET}\n"
printf "${BOLD}${CYAN}  Drums-Engine-MIDI  --  API Test Suite${RESET}\n"
printf "${BOLD}${CYAN}======================================================${RESET}\n"
printf "  Target : %s\n" "$BASE_URL"
echo ""

# ---------------------------------------------------------------------------
# 0. Auto-fetch API token if not provided
# ---------------------------------------------------------------------------
if [[ -z "$API_TOKEN" ]]; then
  printf "${YELLOW}  Fetching API token from GET /api/auth-token ...${RESET}\n"
  API_TOKEN=$(curl --silent --max-time "$CURL_TIMEOUT" \
    "${BASE_URL}/api/auth-token" 2>/dev/null \
    | grep -o '"token":"[^"]*"' | head -1 | cut -d'"' -f4) || true

  if [[ -z "$API_TOKEN" ]]; then
    printf "${RED}  Could not auto-fetch API token. Auth tests will fail.${RESET}\n"
  else
    printf "  Token  : %s\n" "$API_TOKEN"
  fi
fi
echo ""

# ===== Track IDs created during tests for cleanup =====
CREATED_ACTUATOR_ID=""
CREATED_INSTRUMENT_ID=""
CREATED_LOOP_INDEX=""

# ===========================================================================
# 1. System / Status endpoints  (GET, no auth)
# ===========================================================================
printf "${BOLD}--- System / Status (GET, no auth) ---${RESET}\n"

run_test "GET /api/status" 200 \
  "${BASE_URL}/api/status"

run_test "GET /api/capabilities" 200 \
  "${BASE_URL}/api/capabilities"

run_test "GET /api/midi-notes" 200 \
  "${BASE_URL}/api/midi-notes"

run_test "GET /api/scan-i2c" 200 \
  "${BASE_URL}/api/scan-i2c"

run_test "GET /api/cc-routes" 200 \
  "${BASE_URL}/api/cc-routes"

run_test "GET /api/templates" 200 \
  "${BASE_URL}/api/templates"

run_test "GET /api/wifi" 200 \
  "${BASE_URL}/api/wifi"

run_test "GET /api/logs" 200 \
  "${BASE_URL}/api/logs"

run_test "GET /api/auth-token" 200 \
  "${BASE_URL}/api/auth-token"
echo ""

# ===========================================================================
# 2. Authentication tests
# ===========================================================================
printf "${BOLD}--- Authentication ---${RESET}\n"

run_test "POST /api/save without auth -> 401" 401 \
  -X POST "${BASE_URL}/api/save"

run_test "POST /api/save with wrong token -> 401" 401 \
  -X POST \
  -H "Authorization: Bearer 0000000000000000" \
  "${BASE_URL}/api/save"

run_test "POST /api/save with correct token -> 200" 200 \
  -X POST \
  -H "$(auth_header)" \
  "${BASE_URL}/api/save"
echo ""

# ===========================================================================
# 3. Actuator CRUD  (requires auth)
# ===========================================================================
printf "${BOLD}--- Actuator CRUD ---${RESET}\n"

# Create
ACTUATOR_JSON='{"id":99,"name":"Test Solenoid","type":1,"behavior":0,"bus":2,"hwAddress":0,"hwPin":25,"paramMin":8,"paramMax":30,"paramDefault":15,"cooldownUs":200,"enabled":true}'

run_test "POST /api/actuators (create test solenoid) -> 201" 201 \
  -X POST \
  -H "Content-Type: application/json" \
  -H "$(auth_header)" \
  -d "$ACTUATOR_JSON" \
  "${BASE_URL}/api/actuators"

# Extract the ID returned by the server (fallback to 99 if parsing fails)
CREATED_ACTUATOR_ID=$(echo "$RESPONSE_BODY" | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)
CREATED_ACTUATOR_ID="${CREATED_ACTUATOR_ID:-99}"

# List and verify
run_test "GET /api/actuators -> 200" 200 \
  "${BASE_URL}/api/actuators"

assert_body_contains "GET /api/actuators contains Test Solenoid" "Test Solenoid"

# Update
run_test "PUT /api/actuator?id=${CREATED_ACTUATOR_ID} (rename) -> 200" 200 \
  -X PUT \
  -H "Content-Type: application/json" \
  -H "$(auth_header)" \
  -d '{"name":"Renamed Solenoid"}' \
  "${BASE_URL}/api/actuator?id=${CREATED_ACTUATOR_ID}"

# Test actuator
run_test "POST /api/test/actuator -> 200" 200 \
  -X POST \
  -H "Content-Type: application/json" \
  -H "$(auth_header)" \
  -d "{\"id\":${CREATED_ACTUATOR_ID},\"value\":80}" \
  "${BASE_URL}/api/test/actuator"

# Delete  (deferred to cleanup section)
echo ""

# ===========================================================================
# 4. Instrument CRUD  (requires auth)
# ===========================================================================
printf "${BOLD}--- Instrument CRUD ---${RESET}\n"

INSTRUMENT_JSON='{"name":"Test Instrument","category":"drums","midiNote":36,"midiChannel":10,"enabled":true,"noteOnCount":0,"noteOffCount":0,"ccBindingCount":0}'

run_test "POST /api/instruments (create) -> 201" 201 \
  -X POST \
  -H "Content-Type: application/json" \
  -H "$(auth_header)" \
  -d "$INSTRUMENT_JSON" \
  "${BASE_URL}/api/instruments"

# Extract instrument id from response
CREATED_INSTRUMENT_ID=$(echo "$RESPONSE_BODY" | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)
CREATED_INSTRUMENT_ID="${CREATED_INSTRUMENT_ID:-0}"

# List
run_test "GET /api/instruments -> 200" 200 \
  "${BASE_URL}/api/instruments"

assert_body_contains "GET /api/instruments contains Test Instrument" "Test Instrument"

# Get single
run_test "GET /api/instrument?id=${CREATED_INSTRUMENT_ID} -> 200" 200 \
  "${BASE_URL}/api/instrument?id=${CREATED_INSTRUMENT_ID}"

# Update
run_test "PUT /api/instrument?id=${CREATED_INSTRUMENT_ID} (update) -> 200" 200 \
  -X PUT \
  -H "Content-Type: application/json" \
  -H "$(auth_header)" \
  -d '{"name":"Updated Instrument","category":"drums","midiNote":37,"midiChannel":10,"enabled":true,"noteOnCount":0,"noteOffCount":0,"ccBindingCount":0}' \
  "${BASE_URL}/api/instrument?id=${CREATED_INSTRUMENT_ID}"

# Delete (deferred to cleanup section)
echo ""

# ===========================================================================
# 5. Validation tests
# ===========================================================================
printf "${BOLD}--- Validation ---${RESET}\n"

# paramMin > paramMax  -> 400
run_test "POST /api/actuators paramMin>paramMax -> 400" 400 \
  -X POST \
  -H "Content-Type: application/json" \
  -H "$(auth_header)" \
  -d '{"id":98,"name":"Bad Actuator","type":1,"behavior":0,"bus":2,"hwAddress":0,"hwPin":26,"paramMin":50,"paramMax":10,"paramDefault":15,"cooldownUs":200,"enabled":true}' \
  "${BASE_URL}/api/actuators"

assert_body_contains "Validation error mentions paramMin" "paramMin"

# midiNote=255  -> 400
run_test "POST /api/instruments midiNote=255 -> 400" 400 \
  -X POST \
  -H "Content-Type: application/json" \
  -H "$(auth_header)" \
  -d '{"name":"Bad Instrument","category":"drums","midiNote":255,"midiChannel":10,"enabled":true,"noteOnCount":0,"noteOffCount":0,"ccBindingCount":0}' \
  "${BASE_URL}/api/instruments"

assert_body_contains "Validation error mentions midiNote" "midiNote"
echo ""

# ===========================================================================
# 6. Loop CRUD  (requires auth)
# ===========================================================================
printf "${BOLD}--- Loop CRUD ---${RESET}\n"

LOOP_JSON='{"name":"Test Loop","bpm":120,"bars":1,"beatsPerBar":4,"beatValue":4}'

run_test "POST /api/loops (create) -> 201" 201 \
  -X POST \
  -H "Content-Type: application/json" \
  -H "$(auth_header)" \
  -d "$LOOP_JSON" \
  "${BASE_URL}/api/loops"

# Extract loop index
CREATED_LOOP_INDEX=$(echo "$RESPONSE_BODY" | grep -o '"index":[0-9]*' | head -1 | cut -d: -f2)
CREATED_LOOP_INDEX="${CREATED_LOOP_INDEX:-0}"

# List
run_test "GET /api/loops -> 200" 200 \
  "${BASE_URL}/api/loops"

assert_body_contains "GET /api/loops contains Test Loop" "Test Loop"

# Delete loop
run_test "DELETE /api/loop?id=${CREATED_LOOP_INDEX} -> 200" 200 \
  -X DELETE \
  -H "$(auth_header)" \
  "${BASE_URL}/api/loop?id=${CREATED_LOOP_INDEX}"
echo ""

# ===========================================================================
# 7. Not found
# ===========================================================================
printf "${BOLD}--- Not Found ---${RESET}\n"

run_test "GET /api/nonexistent -> 404" 404 \
  "${BASE_URL}/api/nonexistent"
echo ""

# ===========================================================================
# 8. Cleanup  --  delete test resources created above
# ===========================================================================
printf "${BOLD}--- Cleanup ---${RESET}\n"

# Delete test instrument
if [[ -n "$CREATED_INSTRUMENT_ID" ]]; then
  run_test "DELETE /api/instrument?id=${CREATED_INSTRUMENT_ID} (cleanup) -> 200" 200 \
    -X DELETE \
    -H "$(auth_header)" \
    "${BASE_URL}/api/instrument?id=${CREATED_INSTRUMENT_ID}"
fi

# Delete test actuator
if [[ -n "$CREATED_ACTUATOR_ID" ]]; then
  run_test "DELETE /api/actuator?id=${CREATED_ACTUATOR_ID} (cleanup) -> 200" 200 \
    -X DELETE \
    -H "$(auth_header)" \
    "${BASE_URL}/api/actuator?id=${CREATED_ACTUATOR_ID}"
fi

# Save clean state
run_test "POST /api/save (persist clean state) -> 200" 200 \
  -X POST \
  -H "$(auth_header)" \
  "${BASE_URL}/api/save"
echo ""

# ===========================================================================
# Summary
# ===========================================================================
printf "${BOLD}${CYAN}======================================================${RESET}\n"
printf "${BOLD}${CYAN}  Test Summary${RESET}\n"
printf "${BOLD}${CYAN}======================================================${RESET}\n"
printf "  Total  : %d\n" "$TOTAL"
printf "  ${GREEN}Passed : %d${RESET}\n" "$PASSED"
if [[ "$FAILED" -gt 0 ]]; then
  printf "  ${RED}Failed : %d${RESET}\n" "$FAILED"
else
  printf "  Failed : %d\n" "$FAILED"
fi
printf "${BOLD}${CYAN}======================================================${RESET}\n"
echo ""

if [[ "$FAILED" -gt 0 ]]; then
  printf "${RED}${BOLD}  SOME TESTS FAILED${RESET}\n\n"
  exit 1
else
  printf "${GREEN}${BOLD}  ALL TESTS PASSED${RESET}\n\n"
  exit 0
fi
