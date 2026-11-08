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
    external: frozenset[str]  # ids of targets whose code is not the project's

    def production(self) -> dict[str, Target]:
        """Targets the privacy and placement rules apply to, by id: not
        external, not tests, and not header set verification targets."""
        return {
            t.id: t
            for t in self.model.targets.values()
            if t.id not in self.external
            and t.id not in self.tests
            and t.verifies is None
        }


@dataclass(frozen=True)
class Violation:
    rule: str
    level: Level
    message: str
    location: Location | None
