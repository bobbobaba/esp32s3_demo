#!/usr/bin/env python3
"""Sync a sanitized ccswitch/API usage summary to private OTA backend.

This script intentionally does not upload API keys or provider tokens.

Default input is ~/.cc-switch/cc-switch.db. It reads the current provider's
usage_script and calls the provider's real usage endpoint. It does not print or
upload API keys, tokens, or secrets.

Optional --json can still be used to sync an externally collected, sanitized
summary. JSON mode must contain real quota/usage fields.

Expected JSON fields are flexible, but the file must contain real quota
fields from a local collector. This helper will not invent quota numbers.
{
  "source": "ccswitch",
  "provider": "openai",
  "account": "your-masked-account",
  "plan": "pro",
  "status": "ok",
  "currency": "$",
  "limit_amount": "<real monthly limit>",
  "used_amount": "<real used amount>",
  "remaining_amount": "<real remaining amount>",
  "used_percent": "<real used percent>",
  "reset_at": "<real reset date>",
  "note": "synced from local ccswitch"
}
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import calendar
import sqlite3
from datetime import datetime, timedelta
from decimal import Decimal, InvalidOperation
from pathlib import Path
from urllib import request, error


DEFAULT_BASE_URL = "http://example.invalid/<PRIVATE_OTA_PATH>"
DEFAULT_USERNAME = "<PRIVATE_USER>"
DEFAULT_PASSWORD = "<PRIVATE_PASSWORD>"
DEFAULT_CCSWITCH_DB = Path.home() / ".cc-switch/cc-switch.db"
QUOTA_FIELD_ALIASES = {
    "limit_amount": ("limit_amount", "limit"),
    "used_amount": ("used_amount", "used"),
    "remaining_amount": ("remaining_amount", "remaining"),
    "used_percent": ("used_percent",),
}


def first_present_number(raw: dict, aliases: tuple[str, ...]) -> tuple[bool, float]:
    for key in aliases:
        if key not in raw:
            continue
        value = raw.get(key)
        if value is None or value == "":
            continue
        return True, float(value)
    return False, 0.0


def decimal_from_db(value: object) -> Decimal:
    if value is None:
        return Decimal("0")
    text = str(value).strip()
    if not text:
        return Decimal("0")
    try:
        return Decimal(text)
    except InvalidOperation:
        return Decimal("0")


def decimal_from_any(value: object | None) -> Decimal | None:
    if value is None:
        return None
    text = str(value).strip()
    if not text:
        return None
    try:
        return Decimal(text)
    except InvalidOperation:
        return None


def current_month_range() -> tuple[str, str, str]:
    now = datetime.now()
    start = now.replace(day=1).strftime("%Y-%m-%d")
    last_day = calendar.monthrange(now.year, now.month)[1]
    end = now.replace(day=last_day).strftime("%Y-%m-%d")
    return start, end, now.strftime("%Y-%m")


def table_columns(conn: sqlite3.Connection, table: str) -> set[str]:
    try:
        return {row[1] for row in conn.execute(f'PRAGMA table_info("{table}")')}
    except sqlite3.Error:
        return set()


def table_exists(conn: sqlite3.Connection, table: str) -> bool:
    row = conn.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
        (table,),
    ).fetchone()
    return row is not None


def read_decimal(row: sqlite3.Row | None, key: str) -> Decimal:
    if row is None:
        return Decimal("0")
    return decimal_from_db(row[key])


def query_usage_rollups(
    conn: sqlite3.Connection,
    app_type: str,
    provider_id: str,
    month_label: str,
    today_label: str,
) -> tuple[dict, dict, list[dict], str]:
    cols = table_columns(conn, "usage_daily_rollups")
    if not cols:
        return {}, {}, [], ""

    cost_col = "total_cost_usd" if "total_cost_usd" in cols else "total_cost"
    req_col = "request_count" if "request_count" in cols else "requests"
    success_col = "success_count" if "success_count" in cols else req_col
    input_col = "input_tokens" if "input_tokens" in cols else "0"
    output_col = "output_tokens" if "output_tokens" in cols else "0"
    cache_read_col = "cache_read_tokens" if "cache_read_tokens" in cols else "0"
    cache_create_col = "cache_creation_tokens" if "cache_creation_tokens" in cols else "0"
    date_col = "date"

    filters = "app_type = ? AND provider_id = ?"
    params = (app_type, provider_id)
    month_like = f"{month_label}%"

    def aggregate(where_extra: str, extra_params: tuple) -> dict:
        row = conn.execute(
            f"""
            SELECT
                COALESCE(SUM({req_col}), 0) AS requests,
                COALESCE(SUM({success_col}), 0) AS success,
                COALESCE(SUM({input_col}), 0) AS input_tokens,
                COALESCE(SUM({output_col}), 0) AS output_tokens,
                COALESCE(SUM({cache_read_col}), 0) AS cache_read_tokens,
                COALESCE(SUM({cache_create_col}), 0) AS cache_creation_tokens,
                COALESCE(SUM(CAST({cost_col} AS REAL)), 0) AS cost
            FROM usage_daily_rollups
            WHERE {filters} AND {where_extra}
            """,
            params + extra_params,
        ).fetchone()
        input_tokens = int(row["input_tokens"] or 0)
        output_tokens = int(row["output_tokens"] or 0)
        cache_read = int(row["cache_read_tokens"] or 0)
        cache_create = int(row["cache_creation_tokens"] or 0)
        return {
            "requests": int(row["requests"] or 0),
            "success": int(row["success"] or 0),
            "total_tokens": input_tokens + output_tokens + cache_read + cache_create,
            "input_tokens": input_tokens,
            "output_tokens": output_tokens,
            "cache_read_tokens": cache_read,
            "cache_creation_tokens": cache_create,
            "actual_cost": float(row["cost"] or 0.0),
        }

    month = aggregate(f"{date_col} LIKE ?", (month_like,))
    today = aggregate(f"{date_col} = ?", (today_label,))

    models = []
    for row in conn.execute(
        f"""
        SELECT
            model,
            COALESCE(SUM({req_col}), 0) AS requests,
            COALESCE(SUM(CAST({cost_col} AS REAL)), 0) AS cost,
            COALESCE(SUM({input_col} + {output_col} + {cache_read_col} + {cache_create_col}), 0) AS tokens
        FROM usage_daily_rollups
        WHERE {filters} AND {date_col} LIKE ?
        GROUP BY model
        ORDER BY cost DESC, requests DESC
        LIMIT 5
        """,
        params + (month_like,),
    ):
        models.append({
            "model": row["model"] or "-",
            "requests": int(row["requests"] or 0),
            "total_tokens": int(row["tokens"] or 0),
            "actual_cost": float(row["cost"] or 0.0),
        })

    latest = conn.execute(
        f"""
        SELECT MAX({date_col}) AS latest_date
        FROM usage_daily_rollups
        WHERE {filters}
        """,
        params,
    ).fetchone()
    return today, month, models, (latest["latest_date"] if latest else "") or ""


def query_proxy_logs(
    conn: sqlite3.Connection,
    app_type: str,
    provider_id: str,
    month_start: str,
    month_end: str,
    today_label: str,
) -> tuple[dict, dict, list[dict], str]:
    cols = table_columns(conn, "proxy_request_logs")
    if not cols:
        return {}, {}, [], ""

    start_dt = datetime.strptime(month_start, "%Y-%m-%d")
    end_dt = datetime.strptime(month_end, "%Y-%m-%d") + timedelta(days=1)
    today_dt = datetime.strptime(today_label, "%Y-%m-%d")
    tomorrow_dt = today_dt + timedelta(days=1)
    month_start_ts = int(start_dt.timestamp())
    month_end_ts = int(end_dt.timestamp())
    today_start_ts = int(today_dt.timestamp())
    today_end_ts = int(tomorrow_dt.timestamp())

    input_col = "input_tokens" if "input_tokens" in cols else "0"
    output_col = "output_tokens" if "output_tokens" in cols else "0"
    cache_read_col = "cache_read_tokens" if "cache_read_tokens" in cols else "0"
    cache_create_col = "cache_creation_tokens" if "cache_creation_tokens" in cols else "0"
    cost_col = "total_cost_usd" if "total_cost_usd" in cols else "total_cost"
    latency_col = "latency_ms" if "latency_ms" in cols else "duration_ms"

    filters = "app_type = ? AND provider_id = ?"
    params = (app_type, provider_id)

    def aggregate(start_ts: int, end_ts: int) -> dict:
        row = conn.execute(
            f"""
            SELECT
                COUNT(*) AS requests,
                COALESCE(SUM(CASE WHEN status_code >= 200 AND status_code < 300 THEN 1 ELSE 0 END), 0) AS success,
                COALESCE(SUM({input_col}), 0) AS input_tokens,
                COALESCE(SUM({output_col}), 0) AS output_tokens,
                COALESCE(SUM({cache_read_col}), 0) AS cache_read_tokens,
                COALESCE(SUM({cache_create_col}), 0) AS cache_creation_tokens,
                COALESCE(SUM(CAST({cost_col} AS REAL)), 0) AS cost,
                COALESCE(AVG({latency_col}), 0) AS avg_ms
            FROM proxy_request_logs
            WHERE {filters} AND created_at >= ? AND created_at < ?
            """,
            params + (start_ts, end_ts),
        ).fetchone()
        input_tokens = int(row["input_tokens"] or 0)
        output_tokens = int(row["output_tokens"] or 0)
        cache_read = int(row["cache_read_tokens"] or 0)
        cache_create = int(row["cache_creation_tokens"] or 0)
        return {
            "requests": int(row["requests"] or 0),
            "success": int(row["success"] or 0),
            "total_tokens": input_tokens + output_tokens + cache_read + cache_create,
            "input_tokens": input_tokens,
            "output_tokens": output_tokens,
            "cache_read_tokens": cache_read,
            "cache_creation_tokens": cache_create,
            "actual_cost": float(row["cost"] or 0.0),
            "average_duration_ms": float(row["avg_ms"] or 0.0),
        }

    month = aggregate(month_start_ts, month_end_ts)
    today = aggregate(today_start_ts, today_end_ts)

    models = []
    for row in conn.execute(
        f"""
        SELECT
            model,
            COUNT(*) AS requests,
            COALESCE(SUM(CAST({cost_col} AS REAL)), 0) AS cost,
            COALESCE(SUM({input_col} + {output_col} + {cache_read_col} + {cache_create_col}), 0) AS tokens
        FROM proxy_request_logs
        WHERE {filters} AND created_at >= ? AND created_at < ?
        GROUP BY model
        ORDER BY cost DESC, requests DESC
        LIMIT 5
        """,
        params + (month_start_ts, month_end_ts),
    ):
        models.append({
            "model": row["model"] or "-",
            "requests": int(row["requests"] or 0),
            "total_tokens": int(row["tokens"] or 0),
            "actual_cost": float(row["cost"] or 0.0),
        })

    latest = conn.execute(
        f"""
        SELECT MAX(created_at) AS latest_at
        FROM proxy_request_logs
        WHERE {filters}
        """,
        params,
    ).fetchone()
    latest_at = int(latest["latest_at"] or 0) if latest else 0
    latest_text = datetime.fromtimestamp(latest_at).strftime("%Y-%m-%d %H:%M:%S") if latest_at > 0 else ""
    return today, month, models, latest_text


def merge_usage(primary: dict, fallback: dict) -> dict:
    if primary.get("requests", 0) > 0 or primary.get("actual_cost", 0.0) > 0 or primary.get("total_tokens", 0) > 0:
        return primary
    return fallback


def load_json_response_payload(response: dict) -> dict:
    quota = response.get("quota") if isinstance(response.get("quota"), dict) else {}
    remaining = decimal_from_any(response.get("remaining"))
    if remaining is None:
        remaining = decimal_from_any(quota.get("remaining"))
    if remaining is None:
        remaining = decimal_from_any(response.get("balance"))
    if remaining is None:
        raise ValueError("usage endpoint did not return remaining/balance")

    limit = decimal_from_any(response.get("limit"))
    if limit is None:
        limit = decimal_from_any(quota.get("limit"))
    if limit is None:
        limit = decimal_from_any(response.get("total"))
    if limit is None:
        limit = decimal_from_any(quota.get("total"))

    used = decimal_from_any(response.get("used"))
    if used is None:
        used = decimal_from_any(quota.get("used"))

    unit = str(response.get("unit") or quota.get("unit") or "USD")
    active = response.get("is_active")
    if active is None:
        active = response.get("isValid")
    status = "ok"
    if active is False:
        status = "disabled"
    elif limit is None or limit <= 0:
        status = "balance_only"
    elif used is None:
        used = max(Decimal("0"), limit - remaining)

    if used is None:
        used = Decimal("0")
    if limit is None:
        limit = Decimal("0")

    if limit > 0:
        remaining = max(Decimal("0"), limit - used) if remaining is None else remaining
        used_percent = min(Decimal("100"), (used * Decimal("100")) / limit)
    else:
        used_percent = Decimal("0")

    reset_at = str(response.get("reset_at") or quota.get("reset_at") or response.get("cycle_end") or "")
    note = str(response.get("note") or response.get("message") or "synced from ccswitch usage endpoint")

    return {
        "status": status,
        "currency": unit,
        "limit_amount": float(limit),
        "used_amount": float(used),
        "remaining_amount": float(remaining),
        "used_percent": float(used_percent),
        "reset_at": reset_at,
        "note": note,
    }


def load_ccswitch_usage(db_path: Path, app_type: str = "codex", provider_id: str | None = None) -> dict:
    if not db_path.exists():
        raise ValueError(f"ccswitch database not found: {db_path}")

    start_date, end_date, month_label = current_month_range()
    today_label = datetime.now().strftime("%Y-%m-%d")
    with sqlite3.connect(db_path) as conn:
        conn.row_factory = sqlite3.Row
        if not table_exists(conn, "providers"):
            raise ValueError("ccswitch database has no providers table")
        provider = None
        if provider_id:
            provider = conn.execute(
                """
                SELECT id, app_type, name, provider_type, limit_daily_usd, limit_monthly_usd,
                       meta, website_url,
                       cost_multiplier, is_current
                FROM providers
                WHERE app_type = ? AND id = ?
                """,
                (app_type, provider_id),
            ).fetchone()
        if provider is None:
            provider = conn.execute(
                """
                SELECT id, app_type, name, provider_type, limit_daily_usd, limit_monthly_usd,
                       meta, website_url,
                       cost_multiplier, is_current
                FROM providers
                WHERE app_type = ? AND is_current = 1
                ORDER BY sort_index
                LIMIT 1
                """,
                (app_type,),
            ).fetchone()
        if provider is None:
            provider = conn.execute(
                """
                SELECT id, app_type, name, provider_type, limit_daily_usd, limit_monthly_usd,
                       meta, website_url,
                       cost_multiplier, is_current
                FROM providers
                WHERE app_type = ?
                ORDER BY sort_index
                LIMIT 1
                """,
                (app_type,),
            ).fetchone()
        if provider is None:
            raise ValueError(f"no ccswitch provider found for app_type={app_type}")
        meta = json.loads(provider["meta"] or "{}")
        usage_script = meta.get("usage_script") if isinstance(meta, dict) else {}
        if not isinstance(usage_script, dict):
            usage_script = {}
        base_url = str(usage_script.get("baseUrl") or provider["website_url"] or "").strip()
        api_key = str(usage_script.get("apiKey") or "").strip()
        provider_name = str(provider["name"] or provider["id"])
        provider_id_value = str(provider["id"])
        provider_type = str(provider["provider_type"] or app_type)

        today_rollup, month_rollup, model_rollup, latest_rollup = query_usage_rollups(
            conn, app_type, provider_id_value, month_label, today_label
        )
        today_logs, month_logs, model_logs, latest_log = query_proxy_logs(
            conn, app_type, provider_id_value, start_date, end_date, today_label
        )
        today_usage = merge_usage(today_logs, today_rollup)
        total_usage = merge_usage(month_logs, month_rollup)
        model_stats = model_logs if model_logs else model_rollup

        usage_payload: dict | None = None
        usage_response: dict = {}
        request_url = ""
        live_error = ""
        if base_url and api_key and usage_script.get("enabled") is not False:
            request_url = base_url.rstrip("/") + "/v1/usage"
            try:
                req = request.Request(request_url, method="GET")
                req.add_header("Authorization", f"Bearer {api_key}")
                req.add_header("Accept", "application/json")
                req.add_header("User-Agent", "cc-switch/1.0")
                with request.urlopen(req, timeout=20) as resp:
                    usage_response = json.loads(resp.read().decode("utf-8"))
                usage_payload = load_json_response_payload(usage_response)
            except (error.URLError, TimeoutError, json.JSONDecodeError, ValueError) as exc:
                live_error = f"{type(exc).__name__}: {exc}"

    limit = Decimal(str(usage_payload["limit_amount"])) if usage_payload else decimal_from_db(provider["limit_monthly_usd"])
    used = Decimal(str(usage_payload["used_amount"])) if usage_payload else Decimal(str(total_usage.get("actual_cost", 0.0)))
    remaining = Decimal(str(usage_payload["remaining_amount"])) if usage_payload else max(Decimal("0"), limit - used)
    if usage_payload:
        used_percent = Decimal(str(usage_payload["used_percent"]))
        status = str(usage_payload["status"])
        currency = str(usage_payload["currency"] or "USD")
    elif limit > 0:
        used_percent = min(Decimal("100"), (used * Decimal("100")) / limit)
        status = "ok"
        currency = "USD"
    elif used > 0 or total_usage.get("requests", 0) > 0 or total_usage.get("total_tokens", 0) > 0:
        used_percent = Decimal("0")
        status = "usage_only"
        currency = "USD"
    else:
        used_percent = Decimal("0")
        status = "not_synced"
        currency = "USD"

    latest_text = latest_log or latest_rollup
    note = f"ccswitch local usage {month_label}; provider={provider_name}; latest={latest_text}"
    if usage_payload and status == "balance_only":
        note += "; provider exposes live remaining balance only"
    elif live_error:
        note += f"; live usage unavailable, used local logs: {live_error[:120]}"

    return {
        "source": "ccswitch-local",
        "provider": provider_name,
        "account": f"{app_type}:{provider_id_value}",
        "plan": provider_type,
        "status": status,
        "currency": currency,
        "limit_amount": float(limit),
        "used_amount": float(used),
        "remaining_amount": float(remaining),
        "used_percent": float(used_percent),
        "reset_at": (usage_payload or {}).get("reset_at") or end_date,
        "note": note,
        "raw": {
            "app_type": app_type,
            "provider_id": provider_id_value,
            "provider_name": provider_name,
            "month": month_label,
            "month_start": start_date,
            "month_end": end_date,
            "latest_rollup_date": latest_rollup,
            "latest_log_time": latest_log,
            "usage_script_enabled": bool(usage_script.get("enabled") is not False),
            "usage_endpoint": request_url if request_url else "",
            "usage": {
                "today": today_usage,
                "total": total_usage,
                "rpm": 0,
                "tpm": 0,
                "average_duration_ms": total_usage.get("average_duration_ms", 0),
            },
            "model_stats": model_stats,
            "usage_response": {
                key: value
                for key, value in usage_response.items()
                if "key" not in str(key).lower() and "token" not in str(key).lower() and "secret" not in str(key).lower()
            },
        },
    }


def load_usage(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    raw = data if isinstance(data, dict) else {}

    found_limit, limit_amount = first_present_number(raw, QUOTA_FIELD_ALIASES["limit_amount"])
    found_used, used_amount = first_present_number(raw, QUOTA_FIELD_ALIASES["used_amount"])
    found_remaining, remaining_amount = first_present_number(raw, QUOTA_FIELD_ALIASES["remaining_amount"])
    found_percent, used_percent = first_present_number(raw, QUOTA_FIELD_ALIASES["used_percent"])

    if not any((found_limit, found_used, found_remaining, found_percent)):
        raise ValueError("usage JSON has no real quota fields")
    if found_percent is False and limit_amount > 0:
        used_percent = min(100.0, used_amount * 100.0 / limit_amount)
    if found_remaining is False and found_limit and found_used:
        remaining_amount = max(0.0, limit_amount - used_amount)

    status = str(raw.get("status") or "synced")
    return {
        "source": str(raw.get("source") or "ccswitch"),
        "provider": str(raw.get("provider") or ""),
        "account": str(raw.get("account") or ""),
        "plan": str(raw.get("plan") or ""),
        "status": status,
        "currency": str(raw.get("currency") or ""),
        "limit_amount": limit_amount,
        "used_amount": used_amount,
        "remaining_amount": remaining_amount,
        "used_percent": used_percent,
        "reset_at": str(raw.get("reset_at") or ""),
        "note": str(raw.get("note") or "synced from local ccswitch"),
        "raw": {
            key: value
            for key, value in raw.items()
            if "key" not in key.lower() and "token" not in key.lower() and "secret" not in key.lower()
        },
    }


def http_json(method: str, url: str, payload: dict | None = None, token: str | None = None) -> dict:
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    req = request.Request(url, data=data, method=method)
    req.add_header("Content-Type", "application/json")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    with request.urlopen(req, timeout=20) as resp:
        return json.loads(resp.read().decode("utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", default=os.environ.get("private OTA backend_BASE_URL", DEFAULT_BASE_URL))
    parser.add_argument("--username", default=os.environ.get("private OTA backend_USERNAME", DEFAULT_USERNAME))
    parser.add_argument("--password", default=os.environ.get("private OTA backend_PASSWORD", DEFAULT_PASSWORD))
    parser.add_argument("--json", type=Path, default=None)
    parser.add_argument("--ccswitch-db", type=Path, default=Path(os.environ.get("CCSWITCH_DB", DEFAULT_CCSWITCH_DB)))
    parser.add_argument("--app-type", default=os.environ.get("CCSWITCH_APP_TYPE", "codex"))
    parser.add_argument("--provider-id", default=os.environ.get("CCSWITCH_PROVIDER_ID"))
    args = parser.parse_args()

    base = args.base_url.rstrip("/")
    try:
        if args.json:
            usage = load_usage(args.json)
        else:
            usage = load_ccswitch_usage(args.ccswitch_db, args.app_type, args.provider_id)
        login = http_json(
            "POST",
            f"{base}/api/v1/auth/login",
            {"username": args.username, "password": args.password},
        )
        token = login.get("access_token") or ""
        if not token:
            print("Login did not return access_token", file=sys.stderr)
            return 3
        result = http_json("PUT", f"{base}/api/v1/api-usage-status", usage, token)
    except ValueError as exc:
        print(f"Usage JSON rejected: {exc}", file=sys.stderr)
        return 5
    except (error.URLError, TimeoutError, json.JSONDecodeError) as exc:
        print(f"Sync failed: {exc}", file=sys.stderr)
        return 4

    print(json.dumps({k: result.get(k) for k in ("source", "provider", "status", "used_amount", "remaining_amount", "used_percent", "updated_at")}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
