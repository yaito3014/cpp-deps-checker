from collections.abc import Iterator

from cpp_deps_checker.rules._core import Context, Level, Violation


def _violations(ctx: Context) -> Iterator[Violation]:
    model = ctx.model
    for consumer in model.targets.values():
        if consumer.id in ctx.external or consumer.definition is None:
            continue
        for dependency in consumer.declared_dependencies:
            target = model.targets[dependency.target]
            if target.id in ctx.external or target.definition is None:
                continue
            if target.definition.order > consumer.definition.order:
                yield Violation(
                    rule="definition-order",
                    level=Level.ERROR,
                    message=(
                        f"{consumer.name} links {target.name}, which is defined "
                        f"later at {target.definition.location}; define "
                        f"{target.name} before {consumer.name}"
                    ),
                    location=dependency.location,
                )


def check(ctx: Context) -> list[Violation]:
    return list(_violations(ctx))
