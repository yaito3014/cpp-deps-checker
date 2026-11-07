from dataclasses import dataclass
from enum import StrEnum

from cpp_deps_checker.classify import Owners
from cpp_deps_checker.includes import Include
from cpp_deps_checker.model import Location, Model


class Level(StrEnum):
    ERROR = "error"
    WARNING = "warning"


@dataclass(frozen=True)
class Context:
    model: Model
    includes: tuple[Include, ...]  # only those whose includer has an owner
    owners: Owners


@dataclass(frozen=True)
class Violation:
    rule: str
    level: Level
    message: str
    location: Location | None
