from collections.abc import Iterable, Sequence
from pathlib import Path

from cpp_deps_checker.classify import classify, find_external, find_tests
from cpp_deps_checker.model import Dependency, FileSet, Model, Target
from cpp_deps_checker.rules import Context, check

ROOT = Path("/project")
BUILD = Path("/build")


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


def model(*targets: Target, build_dir: Path = BUILD) -> Model:
    return Model(
        targets={t.id: t for t in targets}, source_dir=ROOT, build_dir=build_dir
    )


def placed(
    name: str,
    directory: str,
    *links: str,
    type: str = "INTERFACE_LIBRARY",
    imported: bool = False,
    verifies: str | None = None,
) -> Target:
    """A target defined in ROOT / directory with the given interface links."""
    return target(
        name,
        type=type,
        source_dir=ROOT / directory,
        links=links,
        imported=imported,
        verifies=verifies,
    )


def messages(rule: str, *targets: Target) -> list[str]:
    """Messages of one rule's violations in a model of the given targets,
    classified with the default test patterns and no include edges."""
    m = model(*targets)
    ctx = Context(
        model=m,
        includes=(),
        failures=(),
        owners=classify(m),
        tests=find_tests(m),
        external=find_external(m),
    )
    return [v.message for v in check(ctx) if v.rule == rule]
