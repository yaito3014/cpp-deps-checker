from pathlib import Path

from cpp_deps_checker.classify import Owner, classify
from tests.factories import ROOT, headers, model, target


def test_listed_source_is_private() -> None:
    a = target("a", type="STATIC_LIBRARY", sources=[ROOT / "a/a.cpp"])
    assert classify(model(a)).of(ROOT / "a/a.cpp") == Owner("a", public=False)


def test_file_set_visibility_decides() -> None:
    a = target(
        "a",
        type="STATIC_LIBRARY",
        file_sets=[
            headers("PUBLIC", ROOT / "a/include/a.hpp"),
            headers("INTERFACE", ROOT / "a/include/i.hpp"),
            headers("PRIVATE", ROOT / "a/src/p.hpp"),
        ],
    )
    owners = classify(model(a))

    assert owners.of(ROOT / "a/include/a.hpp") == Owner("a", public=True)
    assert owners.of(ROOT / "a/include/i.hpp") == Owner("a", public=True)
    assert owners.of(ROOT / "a/src/p.hpp") == Owner("a", public=False)


def test_unlisted_file_under_source_dir_is_private() -> None:
    a = target("a", type="STATIC_LIBRARY")
    assert classify(model(a)).of(ROOT / "a/x.hpp") == Owner("a", public=False)


def test_interface_library_files_are_public() -> None:
    i = target("i", sources=[ROOT / "i/listed.hpp"])
    owners = classify(model(i))

    assert owners.of(ROOT / "i/listed.hpp") == Owner("i", public=True)
    assert owners.of(ROOT / "i/unlisted.hpp") == Owner("i", public=True)


def test_base_directory_of_file_set_without_files() -> None:
    c = target(
        "c",
        source_dir=ROOT / "src/c",
        file_sets=[headers("INTERFACE", base_directories=[ROOT / "include"])],
    )
    assert classify(model(c)).of(ROOT / "include/c.hpp") == Owner("c", public=True)


def test_deepest_source_dir_wins() -> None:
    a = target("a", type="STATIC_LIBRARY", source_dir=ROOT / "a")
    b = target("b", type="STATIC_LIBRARY", source_dir=ROOT / "a/b")
    assert classify(model(a, b)).of(ROOT / "a/b/x.hpp") == Owner("b", public=False)


def test_listed_file_beats_directory() -> None:
    a = target("a", type="STATIC_LIBRARY", sources=[ROOT / "a/b/x.hpp"])
    b = target("b", type="STATIC_LIBRARY", source_dir=ROOT / "a/b")
    assert classify(model(a, b)).of(ROOT / "a/b/x.hpp") == Owner("a", public=False)


def test_shared_directory_is_ambiguous() -> None:
    a = target("a", type="STATIC_LIBRARY", source_dir=ROOT / "ab")
    b = target("b", type="STATIC_LIBRARY", source_dir=ROOT / "ab")
    assert classify(model(a, b)).of(ROOT / "ab/x.hpp") is None


def test_imported_and_utility_targets_own_nothing() -> None:
    ext = target("ext", imported=True, sources=[ROOT / "ext/e.hpp"])
    util = target("util", type="UTILITY", sources=[ROOT / "util/u.txt"])
    owners = classify(model(ext, util))

    assert owners.of(ROOT / "ext/e.hpp") is None
    assert owners.of(ROOT / "util/u.txt") is None


def test_file_outside_every_target_is_unowned() -> None:
    a = target("a", type="STATIC_LIBRARY")
    assert classify(model(a)).of(Path("/usr/include/vector")) is None


def test_header_set_verification_target_owns_nothing() -> None:
    a = target("a", type="STATIC_LIBRARY")
    verification = target(
        "a_verify_interface_header_sets",
        type="OBJECT_LIBRARY",
        source_dir=ROOT / "a",
        sources=[ROOT / "build/a/a.hpp.cxx"],
        verifies="a",
    )
    owners = classify(model(a, verification))

    assert owners.of(ROOT / "a/x.hpp") == Owner("a", public=False)
    assert owners.of(ROOT / "build/a/a.hpp.cxx") is None
