from dataclasses import dataclass
from enum import StrEnum

from cpp_deps_checker.classify import Owners
from cpp_deps_checker.includes import Failure, Include
from cpp_deps_checker.model import Location, Model, Target


class Level(StrEnum):
    ERROR = "error"
    WARNING = "warning"


@dataclass(frozen=True)
class Context:
    model: Model
    includes: tuple[Include, ...]  # only those whose includer has an owner
    # Only header set verification translation units; any other failure makes
    # the input unusable.
    failures: tuple[Failure, ...]
    owners: Owners
    tests: frozenset[str]  # ids of test targets

    def production(self) -> dict[str, Target]:
        """Targets the privacy and placement rules apply to, by id: not
        imported, not tests, and not header set verification targets."""
        return {
            t.id: t
            for t in self.model.targets.values()
            if not t.imported and t.id not in self.tests and t.verifies is None
        }


@dataclass(frozen=True)
class Violation:
    rule: str
    level: Level
    message: str
    location: Location | None
