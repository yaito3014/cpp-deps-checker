from collections.abc import Iterable, Sequence
from pathlib import Path

from cpp_deps_checker.model import Dependency, FileSet, Model, Target

ROOT = Path("/project")


def target(
    name: str,
    *,
    type: str = "INTERFACE_LIBRARY",
    imported: bool = False,
    source_dir: Path | None = None,
    sources: Sequence[Path] = (),
    file_sets: Sequence[FileSet] = (),
    links: Iterable[str] = (),
    verifies: str | None = None,
) -> Target:
    """A target with the given interface links and files; paths are absolute."""
    return Target(
        id=name,
        name=name,
        type=type,
        imported=imported,
        definition=None,
        link_libraries=(),
        compile_dependencies=(),
        interface_link_libraries=tuple(Dependency(t, None) for t in links),
        interface_compile_dependencies=(),
        source_dir=ROOT / name if source_dir is None else source_dir,
        sources=tuple(sources),
        file_sets=tuple(file_sets),
        verifies=verifies,
    )


def headers(
    visibility: str, *files: Path, base_directories: Sequence[Path] = ()
) -> FileSet:
    return FileSet(
        name="HEADERS",
        type="HEADERS",
        visibility=visibility,
        base_directories=tuple(base_directories),
        files=files,
    )


def model(*targets: Target) -> Model:
    return Model(targets={t.id: t for t in targets}, source_dir=ROOT)
