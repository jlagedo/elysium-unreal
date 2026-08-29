"""Import the add-on's core the way Blender does: as top-level `core`, not a subpackage.

The add-on directory is itself a package, so pytest would otherwise put `tools/` on the path
and the tests' `from core import ...` would not resolve. Putting the add-on root on the path
is what the Blender extension loader does too.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
