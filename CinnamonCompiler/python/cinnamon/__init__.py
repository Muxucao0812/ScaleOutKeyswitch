# Copyright (c) Siddharth Jayashankar. All rights reserved.
# Licensed under the MIT license.
try:
    from ._cinnamon import *
except ImportError as exc:
    import sys
    msg = """
Importing the Cinnamon Compiler C-extensions failed. This error can happen for
many reasons, often due to issues with your setup or how the Cinnamon Compiler was
installed.

Please note and check the following:

  * The Python version is: Python%d.%d from "%s"

and make sure that they are the versions you expect.
Original error was: %s
""" % (sys.version_info[0], sys.version_info[1], sys.executable,
        exc)
    raise ImportError(msg)
