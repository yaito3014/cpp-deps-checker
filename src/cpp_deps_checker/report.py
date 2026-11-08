from pathlib import Path

from cpp_deps_checker.model import Location
from cpp_deps_checker.rules import Violation


def text(violation: Violation) -> str:
    """One compiler-style line: file:line: level: message [rule]."""
    prefix = f"{violation.location}: " if violation.location else ""
    return f"{prefix}{violation.level}: {violation.message} [{violation.rule}]"


def _from_root(location: Location, source_dir: Path, root: Path) -> Location:
    """location with its file relative to root if inside it, absolute if not."""
    path = source_dir / location.file
    file = path.relative_to(root) if path.is_relative_to(root) else path
    return Location(file, location.line)


# Workflow command escaping, as in @actions/core's command.ts.
def _escape_data(value: str) -> str:
    return value.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def _escape_property(value: str) -> str:
    return _escape_data(value).replace(":", "%3A").replace(",", "%2C")


def annotation(violation: Violation, source_dir: Path, root: Path) -> str:
    """A GitHub Actions ::error or ::warning workflow command.

    GitHub attaches an annotation to a file by its path relative to the
    repository root, which root names. A location outside root goes into the
    message instead.
    """
    properties: list[tuple[str, str]] = []
    message = violation.message
    if violation.location is not None:
        location = _from_root(violation.location, source_dir, root)
        if location.file.is_absolute():
            message = f"{location}: {message}"
        else:
            properties.append(("file", str(location.file)))
            if location.line is not None:
                properties.append(("line", str(location.line)))
    properties.append(("title", violation.rule))
    joined = ",".join(f"{k}={_escape_property(v)}" for k, v in properties)
    return f"::{violation.level} {joined}::{_escape_data(message)}"
