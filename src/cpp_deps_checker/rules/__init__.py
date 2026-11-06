from cpp_deps_checker.rules import _definition_order
from cpp_deps_checker.rules._core import Context, Level, Violation

__all__ = ["Context", "Level", "Violation", "check"]

_RULES = (_definition_order.check,)


def check(ctx: Context) -> list[Violation]:
    return [violation for rule in _RULES for violation in rule(ctx)]
