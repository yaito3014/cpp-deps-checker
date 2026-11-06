import json
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path
from typing import Any

type _Json = dict[str, Any]
type _Site = tuple[Path, int, str]  # file, line, target name


class ReplyError(Exception):
    """The File API reply cannot be used."""


class MissingReplyError(ReplyError):
    """No configure has answered a codemodel-v2 query in the build directory."""


class StaleReplyError(ReplyError):
    """The last configure failed, so the File API reply describes an older run."""


@dataclass(frozen=True)
class Location:
    # As in the reply's backtraceGraph: relative to the top-level source
    # directory when inside it, absolute otherwise.
    file: Path
    line: int

    def __str__(self) -> str:
        return f"{self.file}:{self.line}"


@dataclass(frozen=True)
class Dependency:
    target: str  # id of the target depended on
    location: Location | None


@dataclass(frozen=True)
class Definition:
    order: int  # position in configure-time execution order; compare only
    location: Location


@dataclass(frozen=True)
class Target:
    """A target and the dependencies its own target_link_libraries declares.

    Each dependency field mirrors the File API field of the same name, minus
    non-target fragments and entries injected by another target's
    INTERFACE_LINK_LIBRARIES_DIRECT (marked fromDependency).
    """

    id: str
    name: str
    type: str
    imported: bool
    # None if no add_library/add_executable call in the trace matches, such
    # as for targets CMake creates internally.
    definition: Definition | None
    link_libraries: tuple[Dependency, ...]
    compile_dependencies: tuple[Dependency, ...]
    interface_link_libraries: tuple[Dependency, ...]
    interface_compile_dependencies: tuple[Dependency, ...]

    @property
    def declared_dependencies(self) -> tuple[Dependency, ...]:
        """One dependency per target across all four fields, first one kept."""
        first: dict[str, Dependency] = {}
        for dependency in (
            *self.link_libraries,
            *self.compile_dependencies,
            *self.interface_link_libraries,
            *self.interface_compile_dependencies,
        ):
            first.setdefault(dependency.target, dependency)
        return tuple(first.values())


@dataclass(frozen=True)
class Model:
    targets: Mapping[str, Target]  # by id


def _read(path: Path) -> _Json:
    return json.loads(path.read_text())


def _current_index(reply_dir: Path) -> Path:
    # cmake-file-api(7): the current index has the largest name once the
    # index- or error- prefix is stripped. A failed configure writes an error
    # index and leaves the previous index-*.json in place.
    candidates = [*reply_dir.glob("index-*.json"), *reply_dir.glob("error-*.json")]
    if not candidates:
        raise MissingReplyError(
            f"no File API reply in {reply_dir}; configure with a codemodel-v2 query"
        )
    current = max(candidates, key=lambda path: path.name.split("-", 1)[1])
    if current.name.startswith("error-"):
        raise StaleReplyError(f"the last configure failed ({current})")
    return current


def _location(graph: _Json, backtrace: int | None) -> Location | None:
    if backtrace is None:
        return None
    node = graph["nodes"][backtrace]
    return Location(Path(graph["files"][node["file"]]), node["line"])


def _dependencies(data: _Json, field: str) -> tuple[Dependency, ...]:
    graph = data["backtraceGraph"]
    return tuple(
        Dependency(entry["id"], _location(graph, entry.get("backtrace")))
        for entry in data.get(field, ())
        if "id" in entry and "fromDependency" not in entry
    )


def _trace_order(trace: Path) -> dict[_Site, int]:
    # The trace also holds try_compile projects, so a call counts only through
    # the definition site the File API reports. Command names are
    # case-insensitive and the trace keeps their spelling.
    order: dict[_Site, int] = {}
    with trace.open() as lines:
        for position, line in enumerate(lines):
            entry = json.loads(line)
            if entry.get("cmd", "").lower() in {"add_library", "add_executable"}:
                site = (Path(entry["file"]).resolve(), entry["line"], entry["args"][0])
                order.setdefault(site, position)
    return order


def _definition(
    data: _Json, source_dir: Path, trace_order: Mapping[_Site, int]
) -> Definition | None:
    location = _location(data["backtraceGraph"], data.get("backtrace"))
    if location is None:
        return None
    site = ((source_dir / location.file).resolve(), location.line, data["name"])
    order = trace_order.get(site)
    return None if order is None else Definition(order, location)


def _target(data: _Json, source_dir: Path, trace_order: Mapping[_Site, int]) -> Target:
    return Target(
        id=data["id"],
        name=data["name"],
        type=data["type"],
        imported=data.get("imported", False),
        definition=_definition(data, source_dir, trace_order),
        link_libraries=_dependencies(data, "linkLibraries"),
        compile_dependencies=_dependencies(data, "compileDependencies"),
        interface_link_libraries=_dependencies(data, "interfaceLinkLibraries"),
        interface_compile_dependencies=_dependencies(
            data, "interfaceCompileDependencies"
        ),
    )


def load(build_dir: Path, trace: Path) -> Model:
    """Load the File API reply in build_dir, ordered by the JSON trace."""
    reply_dir = build_dir / ".cmake" / "api" / "v1" / "reply"
    index = _read(_current_index(reply_dir))
    codemodel = _read(reply_dir / index["reply"]["codemodel-v2"]["jsonFile"])
    # Single-config generators only; which configuration a multi-config
    # generator should be checked in is an open decision in docs/design.md.
    (configuration,) = codemodel["configurations"]
    entries = [*configuration["targets"], *configuration["abstractTargets"]]
    source_dir = Path(codemodel["paths"]["source"])
    trace_order = _trace_order(trace)
    targets = (
        _target(_read(reply_dir / entry["jsonFile"]), source_dir, trace_order)
        for entry in entries
    )
    return Model(targets={target.id: target for target in targets})
