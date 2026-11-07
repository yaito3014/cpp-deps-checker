from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from pathlib import Path

from cpp_deps_checker.model import Model, Target


@dataclass(frozen=True)
class Owner:
    target: str  # id
    public: bool


type _Index = Mapping[Path, Owner | None]  # None: claimed by several owners


@dataclass(frozen=True)
class Owners:
    """Which target owns a file, by listed file, then file set base directory
    (for file sets that list no files), then source directory; within each
    directory tier the deepest directory wins."""

    listed: _Index
    base_directories: _Index
    source_directories: _Index

    def of(self, path: Path) -> Owner | None:
        """Owner of an absolute, resolved path; None if unowned or ambiguous."""
        if path in self.listed:
            return self.listed[path]
        for index in (self.base_directories, self.source_directories):
            for directory in path.parents:
                if directory in index:
                    return index[directory]
        return None


def _public(target: Target, visibility: str | None) -> bool:
    # An INTERFACE library has no sources of its own: everything it owns is
    # there for its consumers.
    return target.type == "INTERFACE_LIBRARY" or visibility in {"PUBLIC", "INTERFACE"}


def _index(claims: Iterable[tuple[Path, Owner]]) -> dict[Path, Owner | None]:
    owners: dict[Path, set[Owner]] = {}
    for path, owner in claims:
        owners.setdefault(path, set()).add(owner)
    return {
        path: next(iter(found)) if len(found) == 1 else None
        for path, found in owners.items()
    }


def classify(model: Model) -> Owners:
    # Imported targets' files are an open decision (docs/design.md); custom
    # targets are not code.
    owning = [
        t for t in model.targets.values() if not t.imported and t.type != "UTILITY"
    ]
    listed = [
        (path, Owner(t.id, _public(t, None))) for t in owning for path in t.sources
    ] + [
        (path, Owner(t.id, _public(t, file_set.visibility)))
        for t in owning
        for file_set in t.file_sets
        for path in file_set.files
    ]
    base_directories = [
        (directory, Owner(t.id, _public(t, file_set.visibility)))
        for t in owning
        for file_set in t.file_sets
        if not file_set.files
        for directory in file_set.base_directories
    ]
    source_directories = [(t.source_dir, Owner(t.id, _public(t, None))) for t in owning]
    return Owners(
        listed=_index(listed),
        base_directories=_index(base_directories),
        source_directories=_index(source_directories),
    )
