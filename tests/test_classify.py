from pathlib import Path

from cpp_deps_checker.classify import Owner, classify, find_tests
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


def test_file_listed_by_two_targets_is_ambiguous() -> None:
    a = target("a", type="STATIC_LIBRARY", sources=[ROOT / "shared.hpp"])
    b = target("b", type="STATIC_LIBRARY", sources=[ROOT / "shared.hpp"])
    assert classify(model(a, b)).of(ROOT / "shared.hpp") is None


def test_file_in_a_public_and_a_private_file_set_is_public() -> None:
    a = target(
        "a",
        type="STATIC_LIBRARY",
        file_sets=[
            headers("PRIVATE", ROOT / "a/a.hpp"),
            headers("PUBLIC", ROOT / "a/a.hpp"),
        ],
    )
    assert classify(model(a)).of(ROOT / "a/a.hpp") == Owner("a", public=True)


def test_base_directory_of_a_public_and_a_private_file_set_is_public() -> None:
    c = target(
        "c",
        type="STATIC_LIBRARY",
        file_sets=[
            headers("PRIVATE", base_directories=[ROOT / "c/include"]),
            headers("PUBLIC", base_directories=[ROOT / "c/include"]),
        ],
    )
    assert classify(model(c)).of(ROOT / "c/include/c.hpp") == Owner("c", public=True)


def test_build_directory_inside_the_source_tree_is_claimed_only_by_listing() -> None:
    top = target(
        "top",
        type="STATIC_LIBRARY",
        source_dir=ROOT,
        sources=[ROOT / "build/generated.cpp"],
    )
    owners = classify(model(top, build_dir=ROOT / "build"))

    assert owners.of(ROOT / "build/lib/verify/a.hpp.cxx") is None
    assert owners.of(ROOT / "build/generated.cpp") == Owner("top", public=False)
    assert owners.of(ROOT / "top.hpp") == Owner("top", public=False)


def test_build_directory_inside_a_file_set_base_directory_is_unclaimed() -> None:
    a = target(
        "a",
        source_dir=ROOT,
        file_sets=[headers("INTERFACE", base_directories=[ROOT])],
    )
    g = target(
        "g",
        source_dir=ROOT / "g",
        file_sets=[headers("INTERFACE", base_directories=[ROOT / "build/g/include"])],
    )
    owners = classify(model(a, g, build_dir=ROOT / "build"))

    assert owners.of(ROOT / "build/b/verify/b.hpp.cxx") is None
    assert owners.of(ROOT / "build/g/include/g.hpp") == Owner("g", public=True)
    assert owners.of(ROOT / "a.hpp") == Owner("a", public=True)


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


def test_detail_below_the_file_set_base_directory() -> None:
    a = target(
        "a",
        type="STATIC_LIBRARY",
        source_dir=ROOT / "detail/a",
        file_sets=[
            headers(
                "PUBLIC",
                ROOT / "detail/a/include/a/a.hpp",
                ROOT / "detail/a/include/a/detail/impl.hpp",
                base_directories=[ROOT / "detail/a/include"],
            ),
            headers(
                "PRIVATE",
                ROOT / "detail/a/src/detail/p.hpp",
                base_directories=[ROOT / "detail/a/src"],
            ),
        ],
    )
    owners = classify(model(a))

    # The detail/ above the base directory does not count.
    assert owners.of(ROOT / "detail/a/include/a/a.hpp") == Owner("a", public=True)
    assert owners.of(ROOT / "detail/a/include/a/detail/impl.hpp") == Owner(
        "a", public=True, detail=True
    )
    # Only public headers are detail headers; a private one stays private.
    assert owners.of(ROOT / "detail/a/src/detail/p.hpp") == Owner("a", public=False)


def test_detail_below_a_base_directory_without_files() -> None:
    c = target(
        "c",
        source_dir=ROOT / "detail/c",
        file_sets=[headers("INTERFACE", base_directories=[ROOT / "detail/c/include"])],
    )
    owners = classify(model(c))

    assert owners.of(ROOT / "detail/c/include/c.hpp") == Owner("c", public=True)
    assert owners.of(ROOT / "detail/c/include/detail/x.hpp") == Owner(
        "c", public=True, detail=True
    )


def test_detail_below_the_source_directory() -> None:
    i = target(
        "i",
        source_dir=ROOT / "detail/i",
        sources=[ROOT / "detail/i/i.hpp", ROOT / "detail/i/detail/listed.hpp"],
    )
    owners = classify(model(i))

    assert owners.of(ROOT / "detail/i/i.hpp") == Owner("i", public=True)
    assert owners.of(ROOT / "detail/i/detail/listed.hpp") == Owner(
        "i", public=True, detail=True
    )
    assert owners.of(ROOT / "detail/i/detail/x.hpp") == Owner(
        "i", public=True, detail=True
    )
    # A file named detail is not in a detail/ directory.
    assert owners.of(ROOT / "detail/i/detail") == Owner("i", public=True)


def test_detail_counts_from_the_deepest_base_directory() -> None:
    a = target(
        "a",
        type="STATIC_LIBRARY",
        file_sets=[
            headers(
                "PUBLIC",
                ROOT / "a/detail/include/a/a.hpp",
                base_directories=[ROOT / "a", ROOT / "a/detail/include"],
            )
        ],
    )
    owners = classify(model(a))

    assert owners.of(ROOT / "a/detail/include/a/a.hpp") == Owner("a", public=True)


def test_detail_only_if_every_public_file_set_says_so() -> None:
    a = target(
        "a",
        type="STATIC_LIBRARY",
        file_sets=[
            headers(
                "PUBLIC",
                ROOT / "a/include/detail/x.hpp",
                base_directories=[ROOT / "a/include"],
            ),
            headers(
                "PUBLIC",
                ROOT / "a/include/detail/x.hpp",
                base_directories=[ROOT / "a/include/detail"],
            ),
            headers(
                "PRIVATE",
                ROOT / "a/include/detail/y.hpp",
                base_directories=[ROOT / "a/include"],
            ),
            headers(
                "PUBLIC",
                ROOT / "a/include/detail/y.hpp",
                base_directories=[ROOT / "a/include"],
            ),
        ],
    )
    owners = classify(model(a))

    assert owners.of(ROOT / "a/include/detail/x.hpp") == Owner("a", public=True)
    assert owners.of(ROOT / "a/include/detail/y.hpp") == Owner(
        "a", public=True, detail=True
    )


def test_tests_by_defining_directory() -> None:
    names = {
        "top": ROOT / "tests",
        "nested": ROOT / "tests/unit",
        "singular": ROOT / "test",
        "deeper": ROOT / "src/tests",
        "lookalike": ROOT / "testsuite",
        "production": ROOT / "src",
    }
    m = model(*(target(n, source_dir=d) for n, d in names.items()))

    assert find_tests(m) == {"top", "nested", "singular"}


def test_tests_by_name_pattern() -> None:
    m = model(target("a_test", source_dir=ROOT / "src"), target("a"))
    assert find_tests(m, directories=(), names=("*_test",)) == {"a_test"}


def test_imported_targets_are_never_tests() -> None:
    m = model(target("ext", imported=True, source_dir=ROOT / "tests"))
    assert find_tests(m) == set()


def test_verification_target_follows_the_target_it_verifies() -> None:
    m = model(
        target("a_test", source_dir=ROOT / "src"),
        target(
            "a_test_verify_interface_header_sets",
            source_dir=ROOT / "src",
            verifies="a_test",
        ),
        target("b"),
        target("b_verify_interface_header_sets", verifies="b"),
    )

    assert find_tests(m, directories=(), names=("*_test",)) == {
        "a_test",
        "a_test_verify_interface_header_sets",
    }
