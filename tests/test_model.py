import subprocess
from collections.abc import Iterable
from pathlib import Path

import pytest

from cpp_deps_checker.model import (
    Dependency,
    Location,
    MissingReplyError,
    Model,
    StaleReplyError,
    Target,
    UnsupportedReplyError,
    load,
)
from tests.harness import CONFIGURE, FIXTURES, configure


def _target(model: Model, name: str) -> Target:
    (target,) = (t for t in model.targets.values() if t.name == name)
    return target


def _names(model: Model, dependencies: Iterable[Dependency]) -> set[str]:
    return {model.targets[d.target].name for d in dependencies}


@pytest.fixture(scope="module")
def basics(tmp_path_factory: pytest.TempPathFactory) -> Model:
    build = configure(FIXTURES / "model_basics", tmp_path_factory.mktemp("build"))
    return load(build.dir, build.trace)


def test_targets_and_abstract_targets_are_loaded(basics: Model) -> None:
    declared = {"a", "b", "c", "d", "e"}
    # Header set verification, which the harness enables, adds one target per
    # declared target and two aggregates.
    assert {t.name for t in basics.targets.values()} == (
        declared
        | {f"{name}_verify_interface_header_sets" for name in declared}
        | {"all_verify_header_sets", "all_verify_interface_header_sets"}
    )
    assert _target(basics, "b").type == "INTERFACE_LIBRARY"


def test_link_libraries_hold_direct_edges_only(basics: Model) -> None:
    a = _target(basics, "a")
    assert _names(basics, a.link_libraries) == {"b", "c", "e"}
    assert _names(basics, a.compile_dependencies) == {"b", "c", "e"}


def test_interface_fields_of_static_library(basics: Model) -> None:
    a = _target(basics, "a")
    assert _names(basics, a.interface_link_libraries) == {"b", "c", "e"}
    assert _names(basics, a.interface_compile_dependencies) == {"c"}


def test_interface_library_edges_appear_only_in_interface_fields(
    basics: Model,
) -> None:
    b = _target(basics, "b")
    assert b.link_libraries == ()
    assert b.compile_dependencies == ()
    assert _names(basics, b.interface_link_libraries) == {"d"}
    assert _names(basics, b.interface_compile_dependencies) == {"d"}


def test_declared_dependencies_join_all_four_fields(basics: Model) -> None:
    assert _names(basics, _target(basics, "a").declared_dependencies) == {
        "b",
        "c",
        "e",
    }
    assert _names(basics, _target(basics, "b").declared_dependencies) == {"d"}


def test_dependency_location_is_the_link_call(basics: Model) -> None:
    a = _target(basics, "a")
    assert {d.location for d in a.link_libraries} == {
        Location(Path("CMakeLists.txt"), 8)
    }


def test_injected_entries_and_fragments_are_not_dependencies(
    tmp_path: Path,
) -> None:
    build = configure(FIXTURES / "model_link_items", tmp_path / "build")
    model = load(build.dir, build.trace)

    consumer = _target(model, "consumer")
    assert _names(model, consumer.link_libraries) == {"injector"}
    assert _names(model, consumer.compile_dependencies) == {"injector"}


def test_error_index_newer_than_index_is_stale(tmp_path: Path) -> None:
    build_dir = tmp_path / "build"
    build = configure(FIXTURES / "model_stale", build_dir)
    with pytest.raises(subprocess.CalledProcessError):
        configure(FIXTURES / "model_stale", build_dir, "-DFAIL=ON")

    with pytest.raises(StaleReplyError):
        load(build.dir, build.trace)


@pytest.fixture(scope="module")
def definitions(tmp_path_factory: pytest.TempPathFactory) -> Model:
    build = configure(FIXTURES / "model_definitions", tmp_path_factory.mktemp("build"))
    return load(build.dir, build.trace)


def test_definitions_follow_trace_order_of_this_project_only(
    definitions: Model,
) -> None:
    first, second, third = (
        _target(definitions, name).definition for name in ("first", "second", "third")
    )
    assert first is not None
    assert second is not None
    assert third is not None
    assert first.order < second.order < third.order
    assert first.location == Location(Path("CMakeLists.txt"), 6)
    assert second.location == Location(Path("CMakeLists.txt"), 7)
    assert third.location == Location(Path("CMakeLists.txt"), 9)


def test_imported_targets_are_marked(definitions: Model) -> None:
    assert _target(definitions, "ext::x").imported
    assert not _target(definitions, "first").imported


def test_top_level_directories(tmp_path: Path) -> None:
    build = configure(FIXTURES / "minimal", tmp_path / "build")

    model = load(build.dir, build.trace)

    assert model.source_dir == (FIXTURES / "minimal").resolve()
    assert model.build_dir == build.dir.resolve()


def test_missing_reply_is_reported(tmp_path: Path) -> None:
    with pytest.raises(MissingReplyError, match="no File API reply"):
        load(tmp_path, tmp_path / "trace.json")


def test_reply_without_a_codemodel_is_reported(tmp_path: Path) -> None:
    query = tmp_path / ".cmake/api/v1/query"
    query.mkdir(parents=True)
    (query / "cache-v2").touch()
    subprocess.run(
        ["cmake", "-S", FIXTURES / "minimal", "-B", tmp_path, "-G", "Ninja"],
        check=True,
    )

    with pytest.raises(MissingReplyError, match="codemodel-v2"):
        load(tmp_path, tmp_path / "trace.json")


def test_multi_config_generator_is_reported(tmp_path: Path) -> None:
    subprocess.run(
        [CONFIGURE, FIXTURES / "minimal", tmp_path, "-G", "Ninja Multi-Config"],
        check=True,
    )

    with pytest.raises(UnsupportedReplyError, match="multi-config"):
        load(tmp_path, tmp_path / "trace.json")


@pytest.fixture(scope="module")
def public_private(tmp_path_factory: pytest.TempPathFactory) -> Model:
    build = configure(
        FIXTURES / "direct_layering_public_private", tmp_path_factory.mktemp("build")
    )
    return load(build.dir, build.trace)


def test_files_of_a_built_target(public_private: Model) -> None:
    root = (FIXTURES / "direct_layering_public_private").resolve()
    a = _target(public_private, "a")

    assert public_private.source_dir == root
    assert a.source_dir == root / "a"
    assert a.sources == (root / "a" / "a.cpp",)
    (headers,) = a.file_sets
    assert (headers.type, headers.visibility) == ("HEADERS", "PUBLIC")
    assert headers.base_directories == (root / "a" / "include",)
    assert headers.files == (root / "a" / "include" / "a" / "a.hpp",)


def test_abstract_interface_library_lists_no_files(public_private: Model) -> None:
    root = (FIXTURES / "direct_layering_public_private").resolve()
    c = _target(public_private, "c")

    assert c.sources == ()
    (headers,) = c.file_sets
    assert headers.visibility == "INTERFACE"
    assert headers.base_directories == (root / "a" / "c" / "include",)
    assert headers.files == ()


def test_locate_is_relative_to_the_source_dir(public_private: Model) -> None:
    root = (FIXTURES / "direct_layering_public_private").resolve()

    inside = public_private.locate(root / "a" / "a.cpp", 2)
    assert inside == Location(Path("a/a.cpp"), 2)
    assert public_private.locate(Path("/usr/include/x.h"), None) == Location(
        Path("/usr/include/x.h"), None
    )


def test_header_set_verification_targets_are_marked(public_private: Model) -> None:
    a = _target(public_private, "a")
    verification = _target(public_private, "a_verify_interface_header_sets")

    # CMake creates it with a file-level backtrace only, so no definition.
    assert verification.definition is None
    assert verification.verifies == a.id
    assert a.verifies is None


def test_verified_target_of_a_verification_source(public_private: Model) -> None:
    a = _target(public_private, "a")
    (source,) = _target(public_private, "a_verify_interface_header_sets").sources

    assert public_private.verified_target(source) == a
    assert public_private.verified_target(a.sources[0]) is None


def test_declared_target_with_a_verification_name_is_not_marked(
    tmp_path: Path,
) -> None:
    build = configure(FIXTURES / "model_verification_name", tmp_path / "build")
    model = load(build.dir, build.trace)

    assert _target(model, "other_verify_interface_header_sets").verifies is None
