from collections.abc import Iterator

from cpp_deps_checker.rules._core import Context, Level, Violation


def _violations(ctx: Context) -> Iterator[Violation]:
    model = ctx.model
    for failure in ctx.failures:
        verified = model.verified_target(failure.source)
        if verified is None:
            continue
        error = failure.error
        if error is None:
            message = (
                f"header set verification of {verified.name} failed to preprocess "
                f"{failure.source}"
            )
            location = None
        else:
            message = (
                f"header of {verified.name} does not preprocess on its own: "
                f"{error.message}"
            )
            location = model.locate(error.file, error.line)
        yield Violation(
            rule="self-contained",
            level=Level.ERROR,
            message=message,
            location=location,
        )


def check(ctx: Context) -> list[Violation]:
    return list(_violations(ctx))
