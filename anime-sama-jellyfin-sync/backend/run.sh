#!/usr/bin/env bash
# Lance le backend en rechargement automatique (développement).
set -euo pipefail

cd "$(dirname "$0")"

# Charge HOST/PORT depuis .env s'il existe (sinon valeurs par défaut).
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-8000}"
if [ -f .env ]; then
  # shellcheck disable=SC1091
  set -a; source .env; set +a
fi

exec uvicorn app.main:app --host "${HOST}" --port "${PORT}" --reload
