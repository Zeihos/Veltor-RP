#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

pid=""
log=""
cleanup() {
  if [[ -n "$pid" ]]; then
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
  fi
  if [[ -n "$log" ]]; then rm -f "$log"; fi
}
trap cleanup EXIT

assert_status() {
  local expected="$1" url="$2" actual
  actual="$(curl --max-time 5 -s -o /dev/null -w '%{http_code}' "$url")"
  if [[ "$actual" != "$expected" ]]; then
    echo "FAIL: $url (expected HTTP $expected, got $actual)" >&2
    cat "$log" >&2
    exit 1
  fi
  echo "PASS: HTTP $actual $url"
}

for app in 'Сайт' 'Форум'; do
  port=18080
  css='style.css'
  if [[ "$app" == 'Форум' ]]; then
    port=18081
    css='forum.css'
  fi
  log="$(mktemp)"
  php -S "127.0.0.1:$port" -t "$app" tools/dev-router.php >"$log" 2>&1 &
  pid=$!
  ready=0
  for attempt in {1..30}; do
    if curl --max-time 2 -s -o /dev/null "http://127.0.0.1:$port/install.php"; then ready=1; break; fi
    sleep 0.2
  done
  if [[ "$ready" != 1 ]]; then
    echo "FAIL: $app local server did not start" >&2
    cat "$log" >&2
    exit 1
  fi
  url="http://127.0.0.1:$port"
  assert_status 200 "$url/install.php"
  assert_status 200 "$url/assets/css/$css"
  for path in /app/core.php /app/index.php /storage/index.php /storage/.htaccess /.htaccess; do
    assert_status 403 "$url$path"
  done
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  pid=""
  rm -f "$log"
  log=""
done
echo "All local HTTP smoke checks passed."
