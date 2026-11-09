from collections.abc import Iterator

from cpp_deps_checker.rules._core import Context, Level, Violation


def _violations(ctx: Context) -> Iterator[Violation]:
    model = ctx.model
    for include in ctx.includes:
        includer = ctx.owners.of(include.includer)
        included = ctx.owners.of(include.included)
        if includer is None or included is None or includer.target == included.target:
            continue
        consumer = model.targets[includer.target].name
        owner = model.targets[included.target].name
        header = model.locate(include.included, None)
        if not included.public:
            message = (
                f"{consumer} includes {header}, a private header of {owner}; "
                f"only {owner} may include it"
            )
        elif included.detail and includer.target not in ctx.tests:
            message = (
                f"{consumer} includes {header}, a detail header of {owner}; "
                f"only {owner} and test targets may include it"
            )
        else:
            continue
        yield Violation(
            rule="entry-point",
            level=Level.ERROR,
            message=message,
            location=model.locate(include.includer, include.line),
        )


def check(ctx: Context) -> list[Violation]:
    return list(_violations(ctx))
