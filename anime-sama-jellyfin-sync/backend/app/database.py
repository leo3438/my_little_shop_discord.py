"""Initialisation de la base de données et session."""

from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path

from sqlmodel import Session, SQLModel, create_engine

from .config import get_settings

settings = get_settings()

# Pour SQLite, on autorise l'accès depuis plusieurs threads
# (les tâches de fond FastAPI tournent dans un threadpool).
_connect_args = (
    {"check_same_thread": False}
    if settings.database_url.startswith("sqlite")
    else {}
)

# S'assure que le dossier du fichier SQLite existe.
if settings.database_url.startswith("sqlite:///"):
    db_file = settings.database_url.replace("sqlite:///", "", 1)
    Path(db_file).expanduser().resolve().parent.mkdir(parents=True, exist_ok=True)

engine = create_engine(settings.database_url, echo=False, connect_args=_connect_args)


def init_db() -> None:
    """Crée les tables si elles n'existent pas."""
    SQLModel.metadata.create_all(engine)


def get_session() -> Iterator[Session]:
    """Dépendance FastAPI : fournit une session par requête."""
    with Session(engine) as session:
        yield session
