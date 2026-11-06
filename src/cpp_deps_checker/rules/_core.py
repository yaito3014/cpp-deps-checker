from dataclasses import dataclass
from enum import StrEnum

from cpp_deps_checker.model import Location, Model


class Level(StrEnum):
    ERROR = "error"
    WARNING = "warning"


@dataclass(frozen=True)
class Context:
    model: Model


@dataclass(frozen=True)
class Violation:
    rule: str
    level: Level
    message: str
    location: Location | None
