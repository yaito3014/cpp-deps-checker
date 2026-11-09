from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass
from fnmatch import fnmatchcase
from pathlib import Path
from typing import Protocol

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


# None: claimed by several targets, or by an external target.
type _Index = Mapping[Path, _Claim | None]


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


class _Claimant(Protocol):
    @property
    def target(self) -> str: ...


def _index[T: _Claimant](
    claims: Iterable[tuple[Path, T | None]], merge: Callable[[Sequence[T]], T]
) -> dict[Path, T | None]:
    """Each path's claims merged into one, or None if they come from several
    targets or one is None. A target claims a file several times when the
    file is in several of its file sets, which CMake allows."""
    found: dict[Path, list[T | None]] = {}
    for path, claim in claims:
        found.setdefault(path, []).append(claim)

    def resolve(claims: list[T | None]) -> T | None:
        known = [c for c in claims if c is not None]
        if len(known) < len(claims) or len({c.target for c in known}) > 1:
            return None
        return merge(known)

    return {path: resolve(claims) for path, claims in found.items()}


def _merge_claims(claims: Sequence[_Claim]) -> _Claim:
    return _Claim(claims[0].target, any(c.public for c in claims))


def _merge_owners(owners: Sequence[Owner]) -> Owner:
    """Public if any file set exposes the file; a detail header only if every
    file set that exposes it puts it below a detail/ directory, since one
    that does not makes it reachable as an entry point."""
    public = [o for o in owners if o.public]
    return Owner(
        owners[0].target, bool(public), bool(public) and all(o.detail for o in public)
    )


def _relative(path: Path, directories: Iterable[Path]) -> Path | None:
    """path below the deepest of directories that contains it."""
    containing = [d for d in directories if path.is_relative_to(d)]
    if not containing:
        return None
    return path.relative_to(max(containing, key=lambda d: len(d.parts)))


def _nested_build_dir(model: Model) -> Path | None:
    """The build directory if it is strictly inside the source directory."""
    source, build = model.source_dir, model.build_dir
    return build if build != source and build.is_relative_to(source) else None


def find_external(model: Model) -> frozenset[str]:
    """Ids of the targets whose code is not the project's: imported targets,
    and targets defined outside the top-level source directory or inside a
    build directory nested in it, where FetchContent puts what it fetches."""
    build = _nested_build_dir(model)

    def external(target: Target) -> bool:
        return (
            target.imported
            or not target.source_dir.is_relative_to(model.source_dir)
            or (build is not None and target.source_dir.is_relative_to(build))
        )

    return frozenset(t.id for t in model.targets.values() if external(t))


def classify(model: Model) -> Owners:
    # External targets' files are an open decision (docs/design.md); custom
    # targets are not code; a header set verification target shares the
    # source directory of the target it verifies and compiles only generated
    # files.
    external = find_external(model)
    owning = [
        t
        for t in model.targets.values()
        if t.id not in external and t.type != "UTILITY" and t.verifies is None
    ]
    # The files of other external targets belong to no target, even inside a
    # directory a project target claims (a build directory in the source
    # tree). An imported target's source directory is only where it is
    # declared.
    shadowing = [
        t for t in model.targets.values() if t.id in external and not t.imported
    ]
    listed: list[tuple[Path, Owner | None]] = [
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
    listed += [
        (path, None)
        for t in shadowing
        for path in (*t.sources, *(p for f in t.file_sets for p in f.files))
    ]
    base_directories: list[tuple[Path, _Claim | None]] = [
        (directory, _Claim(t.id, _public(t, file_set.visibility)))
        for t in owning
        for file_set in t.file_sets
        if not file_set.files
        for directory in file_set.base_directories
    ]
    base_directories += [
        (directory, None)
        for t in shadowing
        for file_set in t.file_sets
        if not file_set.files
        for directory in file_set.base_directories
    ]
    source_directories: list[tuple[Path, _Claim | None]] = [
        (t.source_dir, _Claim(t.id, _public(t, None))) for t in owning
    ]
    source_directories += [(t.source_dir, None) for t in shadowing]
    # Generated files, such as header set verification sources, belong only
    # to a target that lists them, not to the target whose source directory
    # holds the build directory.
    build = _nested_build_dir(model)
    if build is not None:
        source_directories.append((build, None))
    return Owners(
        listed=_index(listed, _merge_owners),
        base_directories=_index(base_directories, _merge_claims),
        source_directories=_index(source_directories, _merge_claims),
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

    External targets are never test targets, and a header set verification
    target is one exactly when the target it verifies is one.
    """

    external = find_external(model)

    def matches(target: Target) -> bool:
        if target.id in external or target.verifies is not None:
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
