import dataclasses
import shutil
from pathlib import Path

from cpp_deps_checker.classify import classify, find_external, find_tests
from cpp_deps_checker.cli import context
from cpp_deps_checker.includes import Failure
from cpp_deps_checker.model import Definition, Location, Target
from cpp_deps_checker.rules import Context, check
from tests import factories
from tests.factories import BUILD, ROOT, headers, model, placed, target
from tests.harness import FIXTURES, check_fixture, configure


def test_fetched_dependency_is_exempt(tmp_path: Path) -> None:
    assert check_fixture("external_fetched", tmp_path) == []


def test_fetched_into_a_build_directory_inside_the_source_tree(
    tmp_path: Path,
) -> None:
    source = tmp_path / "source"
    shutil.copytree(FIXTURES / "external_fetched", source)
    build = configure(source, source / "build")

    ctx = context(build.dir, build.trace, build.compile_commands)

    assert {ctx.model.targets[t].name for t in ctx.external} == {"dep", "dep_main"}
    assert check(ctx) == []


def test_external_targets() -> None:
    m = model(
        target("imported", imported=True),
        target("outside", source_dir=Path("/elsewhere/dep")),
        target("fetched", source_dir=ROOT / "build/_deps/dep-src"),
        target("project", source_dir=ROOT / "src"),
        target("top", source_dir=ROOT),
        build_dir=ROOT / "build",
    )

    assert find_external(m) == {"imported", "outside", "fetched"}


def test_build_directory_that_is_not_strictly_inside_the_source_tree() -> None:
    project = target("project", source_dir=ROOT / "src")

    assert find_external(model(project, build_dir=ROOT)) == set()
    assert find_external(model(project, build_dir=ROOT.parent)) == set()
    assert find_external(model(project, build_dir=BUILD)) == set()


def test_external_targets_own_nothing_and_shadow_enclosing_owners() -> None:
    fetched = ROOT / "build/_deps/dep-src"
    m = model(
        target("top", source_dir=ROOT),
        target(
            "dep",
            type="STATIC_LIBRARY",
            source_dir=fetched,
            sources=[fetched / "dep.cpp"],
            file_sets=[headers("PUBLIC", base_directories=[fetched / "include"])],
        ),
        build_dir=ROOT / "build",
    )
    owners = classify(m)

    assert owners.of(fetched / "dep.cpp") is None
    assert owners.of(fetched / "include/dep/dep.hpp") is None
    assert owners.of(fetched / "src/impl.hpp") is None
    assert owners.of(ROOT / "top.hpp") is not None


def test_listed_file_of_an_external_target_is_unowned() -> None:
    # Listed in a directory that a project target claims.
    m = model(
        target("top", source_dir=ROOT),
        target(
            "vendored",
            source_dir=Path("/elsewhere/vendored"),
            file_sets=[headers("INTERFACE", ROOT / "vendor/ext.hpp")],
        ),
    )

    assert classify(m).of(ROOT / "vendor/ext.hpp") is None


def test_external_base_directory_inside_a_project_base_directory() -> None:
    vendor = ROOT / "include/vendor"
    m = model(
        target(
            "lib", file_sets=[headers("PUBLIC", base_directories=[ROOT / "include"])]
        ),
        target(
            "vendored",
            source_dir=Path("/elsewhere/vendored"),
            file_sets=[headers("PUBLIC", base_directories=[vendor])],
        ),
    )
    owners = classify(m)

    assert owners.of(vendor / "v.hpp") is None
    assert owners.of(ROOT / "include/lib.hpp") is not None


def test_imported_target_does_not_shadow_the_directory_declaring_it() -> None:
    # An imported target's source directory is where it is declared, not
    # where its files are.
    m = model(
        target("top", source_dir=ROOT), target("ext", imported=True, source_dir=ROOT)
    )

    assert classify(m).of(ROOT / "top.hpp") is not None


def test_external_targets_are_neither_tests_nor_production() -> None:
    m = model(
        target("fetched", source_dir=ROOT / "build/_deps/dep-src"),
        target("outside", source_dir=Path("/elsewhere/tests")),
        target("a_test", source_dir=ROOT / "tests"),
        target("a", source_dir=ROOT / "a"),
        build_dir=ROOT / "build",
    )
    ctx = Context(
        model=m,
        includes=(),
        failures=(),
        owners=classify(m),
        tests=find_tests(m),
        external=find_external(m),
    )

    assert ctx.tests == {"a_test"}
    assert set(ctx.production()) == {"a"}


def _defined(t: Target, order: int) -> Target:
    location = Location(Path("CMakeLists.txt"), order)
    return dataclasses.replace(t, definition=Definition(order, location))


def test_external_targets_are_exempt_from_definition_order() -> None:
    fetched = Path("/elsewhere")
    consumer = _defined(placed("a", "a", "late_dep"), 1)
    late_dep = _defined(target("late_dep", source_dir=fetched / "dep-src"), 2)
    external_consumer = _defined(
        target("dep_user", source_dir=fetched / "user-src", links=["late"]), 3
    )
    late = _defined(placed("late", "late"), 4)

    assert (
        factories.messages(
            "definition-order", consumer, late_dep, external_consumer, late
        )
        == []
    )


def test_header_sets_of_external_targets_need_not_be_self_contained() -> None:
    def verifying(name: str, source_dir: Path) -> list[Target]:
        verify = target(
            f"{name}_verify_interface_header_sets",
            type="OBJECT_LIBRARY",
            source_dir=source_dir,
            sources=[BUILD / f"{name}.cxx"],
            verifies=name,
        )
        return [target(name, source_dir=source_dir), verify]

    m = model(
        *verifying("vendored", Path("/elsewhere/vendored")),
        *verifying("a", ROOT / "a"),
    )
    ctx = Context(
        model=m,
        includes=(),
        failures=tuple(
            Failure(BUILD / f"{name}.cxx", None, "") for name in ("vendored", "a")
        ),
        owners=classify(m),
        tests=find_tests(m),
        external=find_external(m),
    )

    assert [v.message for v in check(ctx) if v.rule == "self-contained"] == [
        f"header set verification of a failed to preprocess {BUILD / 'a.cxx'}"
    ]
