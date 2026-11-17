#!/usr/bin/env python3
"""Offline VELTOR staff provisioning. Stop the server before use."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sqlite3
import sys

NAME_RE = re.compile(r"[A-Za-z0-9_]{3,24}\Z")


def set_staff(db_path: Path, username: str, grant: bool) -> str:
    if NAME_RE.fullmatch(username) is None:
        raise ValueError("Имя должно содержать 3–24 символа A-Z, 0-9, _.")
    if not db_path.is_file():
        raise ValueError(f"База не найдена: {db_path}. Сначала создайте аккаунт в игре.")
    with sqlite3.connect(f"file:{db_path.resolve()}?mode=rw", uri=True, timeout=3) as connection:
        connection.execute("PRAGMA busy_timeout=3000")
        row = connection.execute(
            "SELECT name FROM accounts WHERE name=? COLLATE NOCASE LIMIT 1", (username,)
        ).fetchone()
        if not row:
            raise ValueError("Такого аккаунта нет. Зарегистрируйтесь в игре перед выдачей прав.")
        stored_name = row[0]
        connection.execute(
            "CREATE TABLE IF NOT EXISTS staff (name TEXT PRIMARY KEY COLLATE NOCASE)"
        )
        if grant:
            connection.execute("INSERT OR IGNORE INTO staff(name) VALUES(?)", (stored_name,))
        else:
            connection.execute("DELETE FROM staff WHERE name=? COLLATE NOCASE", (stored_name,))
        connection.commit()
    return stored_name


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Управление сотрудниками VELTOR (только на остановленном сервере)"
    )
    parser.add_argument("action", choices=("grant", "revoke"), help="Выдать или снять права")
    parser.add_argument("username", help="Зарегистрированный ник")
    default_db = Path(__file__).resolve().parents[1] / "server" / "scriptfiles" / "veltor.db"
    parser.add_argument("--db", type=Path, default=default_db)
    args = parser.parse_args(argv)
    try:
        name = set_staff(args.db, args.username, args.action == "grant")
    except (ValueError, sqlite3.Error, OSError) as error:
        print(f"Ошибка: {error}", file=sys.stderr)
        return 1
    print(f"Права {'выданы' if args.action == 'grant' else 'сняты'}: {name}. "
          "Изменение вступит в силу при следующем входе игрока.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
