from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from fnmatch import fnmatchcase
from pathlib import Path

from cpp_deps_checker.model import Model, Target

DEFAULT_TEST_DIRECTORIES = ("tests/**", "test/**")


@dataclass(frozen=True)
class Owner:
    target: str  # id
    public: bool
    # A public header below a detail/ directory, counted from the directory
    # that gave the file its owner (file set base directory or source
    # directory), so that a target defined inside some detail/ directory
    # does not make all its headers detail headers.
    detail: bool = False


@dataclass(frozen=True)
class _Claim:
    target: str
    public: bool

    def owner(self, relative: Path | None) -> Owner:
        detail = (
            self.public and relative is not None and "detail" in relative.parent.parts
        )
        return Owner(self.target, self.public, detail)


type _Index = Mapping[Path, _Claim | None]  # None: claimed by several owners


@dataclass(frozen=True)
class Owners:
    """Which target owns a file, by listed file, then file set base directory
    (for file sets that list no files), then source directory; within each
    directory tier the deepest directory wins."""

    listed: Mapping[Path, Owner | None]
    base_directories: _Index
    source_directories: _Index

    def of(self, path: Path) -> Owner | None:
        """Owner of an absolute, resolved path; None if unowned or ambiguous."""
        if path in self.listed:
            return self.listed[path]
        for index in (self.base_directories, self.source_directories):
            for directory in path.parents:
                if directory in index:
                    claim = index[directory]
                    return (
                        None
                        if claim is None
                        else claim.owner(path.relative_to(directory))
                    )
        return None


def _public(target: Target, visibility: str | None) -> bool:
    # An INTERFACE library has no sources of its own: everything it owns is
    # there for its consumers.
    return target.type == "INTERFACE_LIBRARY" or visibility in {"PUBLIC", "INTERFACE"}


def _index[T](claims: Iterable[tuple[Path, T]]) -> dict[Path, T | None]:
    owners: dict[Path, set[T]] = {}
    for path, owner in claims:
        owners.setdefault(path, set()).add(owner)
    return {
        path: next(iter(found)) if len(found) == 1 else None
        for path, found in owners.items()
    }


def _relative(path: Path, directories: Iterable[Path]) -> Path | None:
    """path below the deepest of directories that contains it."""
    containing = [d for d in directories if path.is_relative_to(d)]
    if not containing:
        return None
    return path.relative_to(max(containing, key=lambda d: len(d.parts)))


def classify(model: Model) -> Owners:
    # Imported targets' files are an open decision (docs/design.md); custom
    # targets are not code; a header set verification target shares the
    # source directory of the target it verifies and compiles only generated
    # files.
    owning = [
        t
        for t in model.targets.values()
        if not t.imported and t.type != "UTILITY" and t.verifies is None
    ]
    listed = [
        (path, _Claim(t.id, _public(t, None)).owner(_relative(path, [t.source_dir])))
        for t in owning
        for path in t.sources
    ] + [
        (
            path,
            _Claim(t.id, _public(t, file_set.visibility)).owner(
                _relative(path, file_set.base_directories)
            ),
        )
        for t in owning
        for file_set in t.file_sets
        for path in file_set.files
    ]
    base_directories = [
        (directory, _Claim(t.id, _public(t, file_set.visibility)))
        for t in owning
        for file_set in t.file_sets
        if not file_set.files
        for directory in file_set.base_directories
    ]
    source_directories = [
        (t.source_dir, _Claim(t.id, _public(t, None))) for t in owning
    ]
    return Owners(
        listed=_index(listed),
        base_directories=_index(base_directories),
        source_directories=_index(source_directories),
    )


def find_tests(
    model: Model,
    *,
    directories: Sequence[str] = DEFAULT_TEST_DIRECTORIES,
    names: Sequence[str] = (),
) -> frozenset[str]:
    """Ids of the test targets: those defined by a CMakeLists.txt whose path,
    relative to the top-level source directory, matches one of directories,
    or whose name matches one of names (fnmatch patterns).

    A header set verification target is a test target exactly when the
    target it verifies is one.
    """

    def matches(target: Target) -> bool:
        if target.imported or target.verifies is not None:
            return False
        lists_file = target.source_dir.relative_to(model.source_dir) / "CMakeLists.txt"
        return any(lists_file.full_match(d) for d in directories) or any(
            fnmatchcase(target.name, n) for n in names
        )

    tests = {t.id for t in model.targets.values() if matches(t)}
    return frozenset(
        tests
        | {
            t.id
            for t in model.targets.values()
            if t.verifies is not None and t.verifies in tests
        }
    )
