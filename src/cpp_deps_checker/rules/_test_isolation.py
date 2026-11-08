from collections.abc import Iterator

from cpp_deps_checker.rules._core import Context, Level, Violation


def _violations(ctx: Context) -> Iterator[Violation]:
    for consumer in ctx.model.targets.values():
        if consumer.id in ctx.tests:
            continue
        for dependency in consumer.declared_dependencies:
            if dependency.target not in ctx.tests:
                continue
            name = ctx.model.targets[dependency.target].name
            yield Violation(
                rule="test-isolation",
                level=Level.ERROR,
                message=(
                    f"{consumer.name} links test target {name}; "
                    "only test targets may link it"
                ),
                location=dependency.location,
            )


def check(ctx: Context) -> list[Violation]:
    return list(_violations(ctx))
