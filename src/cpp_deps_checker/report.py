from cpp_deps_checker.rules import Violation


def text(violation: Violation) -> str:
    """One compiler-style line: file:line: level: message [rule]."""
    prefix = f"{violation.location}: " if violation.location else ""
    return f"{prefix}{violation.level}: {violation.message} [{violation.rule}]"
