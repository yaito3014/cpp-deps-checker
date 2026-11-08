from pathlib import Path

from cpp_deps_checker import report
from cpp_deps_checker.model import Location
from cpp_deps_checker.rules import Level, Violation

SOURCE = Path("/repo/project")
ROOT = Path("/repo")


def _violation(
    location: Location | None, message: str = "a links b", level: Level = Level.ERROR
) -> Violation:
    return Violation("no-cycles", level, message, location)


def test_annotation_path_is_relative_to_root() -> None:
    violation = _violation(Location(Path("a/CMakeLists.txt"), 3))

    assert report.annotation(violation, SOURCE, ROOT) == (
        "::error file=project/a/CMakeLists.txt,line=3,title=no-cycles::a links b"
    )


def test_annotation_without_line_or_location() -> None:
    no_line = _violation(Location(Path("a.hpp"), None), level=Level.WARNING)

    assert report.annotation(no_line, SOURCE, ROOT) == (
        "::warning file=project/a.hpp,title=no-cycles::a links b"
    )
    assert report.annotation(_violation(None), SOURCE, ROOT) == (
        "::error title=no-cycles::a links b"
    )


def test_annotation_outside_root_puts_location_in_message() -> None:
    violation = _violation(Location(Path("/usr/include/x.hpp"), 7))

    assert report.annotation(violation, SOURCE, ROOT) == (
        "::error title=no-cycles::/usr/include/x.hpp:7: a links b"
    )


def test_annotation_escapes_data_and_properties() -> None:
    violation = _violation(Location(Path("a,b:c%.hpp"), 1), "50% done\r\nnext")

    assert report.annotation(violation, SOURCE, ROOT) == (
        "::error file=project/a%2Cb%3Ac%25.hpp,line=1,title=no-cycles"
        "::50%25 done%0D%0Anext"
    )
