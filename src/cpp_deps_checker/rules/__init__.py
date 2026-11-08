from cpp_deps_checker.rules import (
    _cycles,
    _definition_order,
    _direct_layering,
    _entry_point,
    _self_contained,
    _test_isolation,
)
from cpp_deps_checker.rules._core import Context, Level, Violation

__all__ = ["Context", "Level", "Violation", "check"]

_RULES = (
    _cycles.check,
    _definition_order.check,
    _direct_layering.check,
    _self_contained.check,
    _entry_point.check,
    _test_isolation.check,
)


def check(ctx: Context) -> list[Violation]:
    return [violation for rule in _RULES for violation in rule(ctx)]
