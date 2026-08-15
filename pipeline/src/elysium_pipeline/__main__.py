"""Module entrypoint: ``python -m elysium_pipeline`` runs the ``elysium`` CLI.

A long-lived process must be started this way rather than through the
``.venv/Scripts/elysium.exe`` console script. Windows locks a running image, and
every dependency sync reinstalls the editable package, rewriting that script — so
a held console script makes any later ``uv run elysium ...`` fail with a
permission error. Holding ``python.exe`` instead pins nothing a sync replaces.
"""

from elysium_pipeline.cli import app

if __name__ == "__main__":
    app()
