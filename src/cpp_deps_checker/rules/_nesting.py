from collections.abc import Iterator
from pathlib import Path

from cpp_deps_checker.rules._core import Context, Level, Violation


def _private_to(directory: Path, defining: set[Path]) -> Path | None:
    """The nearest proper ancestor of directory that defines a target.

    The top-level directory may be it, which constrains no consumer.
    """
    return next((a for a in directory.parents if a in defining), None)


def _violations(ctx: Context) -> Iterator[Violation]:
    model = ctx.model
    production = ctx.production()
    # A directory's targets share one subtree, so the parent is a directory:
    # this holds whether or not it defines several targets.
    defining = {t.source_dir for t in production.values() if t.type != "UTILITY"}
    for consumer in production.values():
        for dependency in consumer.declared_dependencies:
            target = production.get(dependency.target)
            if target is None:
                continue
            parent = _private_to(target.source_dir, defining)
            if parent is None or consumer.source_dir.is_relative_to(parent):
                continue
            yield Violation(
                rule="nesting",
                level=Level.ERROR,
                message=(
                    f"{consumer.name} links {target.name}, which is private to "
                    f"{model.locate(parent, None)}/; hoist {target.name}: a "
                    "private detail is becoming shared API"
                ),
                location=dependency.location,
            )


def check(ctx: Context) -> list[Violation]:
    return list(_violations(ctx))
