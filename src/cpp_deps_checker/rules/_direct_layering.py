from collections.abc import Iterator

from cpp_deps_checker.rules._core import Context, Level, Violation


def _violations(ctx: Context) -> Iterator[Violation]:
    model = ctx.model
    for include in ctx.includes:
        includer = ctx.owners.of(include.includer)
        included = ctx.owners.of(include.included)
        if includer is None or included is None or includer.target == included.target:
            continue
        consumer = model.targets[includer.target]
        dependency = model.targets[included.target]
        compile_ids = {d.target for d in consumer.compile_dependencies}
        interface_ids = {d.target for d in consumer.interface_compile_dependencies}
        header = model.locate(include.included, None)
        if includer.public:
            if dependency.id in interface_ids:
                continue
            how = "links PRIVATE" if dependency.id in compile_ids else "does not link"
            message = (
                f"public header of {consumer.name} includes {header} of "
                f"{dependency.name}, which {consumer.name} {how}; link it PUBLIC"
            )
        else:
            if dependency.id in compile_ids:
                continue
            message = (
                f"{consumer.name} includes {header} of {dependency.name} without "
                f"linking {dependency.name} directly"
            )
        yield Violation(
            rule="direct-layering",
            level=Level.ERROR,
            message=message,
            location=model.locate(include.includer, include.line),
        )


def check(ctx: Context) -> list[Violation]:
    return list(_violations(ctx))
