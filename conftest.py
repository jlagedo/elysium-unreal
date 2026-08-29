"""Give every pytest run the local roots the decoders resolve at import time.

`elysium_pipeline.formats.install` reads `ELYSIUM_VTMB_ROOT` when it is imported, so a test
module on that chain fails during collection on a shell that has no roots exported. The
`elysium` CLI reads `.elysium.local.env`; `uv run pytest` does not, so the same resolver runs
here instead. Nothing is required: a checkout with no local env file still collects, and the
roots simply stay unset.
"""

from elysium_pipeline.config import ProjectConfig

ProjectConfig.resolve(None, None, None).apply_environment()
