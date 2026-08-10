"""
The one exception base every Taguchi error inherits from.

WHY THIS EXISTS
---------------
This package grew two independent `TaguchiError` classes -- `core.TaguchiError`
for the original layer, `errors.TaguchiError` for the "enhanced" one -- and
they were unrelated types. `__init__.py` tried to paper over it by exporting

    class BackwardCompatibleTaguchiError(TaguchiError, _OriginalTaguchiError)

but inheritance runs the wrong way for catching: a SUBCLASS of both catches
neither. The documented, exported name could not catch anything the library
actually raised:

    from taguchi import TaguchiError
    try:
        Experiment().generate()          # raises errors.TaguchiError
    except TaguchiError:                 # ...which this does not catch
        ...

Both concrete classes derive from the base here, so the exported name is a
genuine ancestor of everything raised and `except TaguchiError` works from
either layer. Kept in its own module because core.py and errors.py must both
import it without importing each other.
"""


class TaguchiErrorBase(Exception):
    """Ancestor of every error this package raises.

    Exported as `taguchi.TaguchiError`. Catch this to catch anything from
    either layer; catch a more specific class when you mean one.
    """
    pass
