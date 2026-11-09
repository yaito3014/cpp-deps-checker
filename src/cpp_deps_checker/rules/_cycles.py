from collections import deque
from collections.abc import Iterator

from cpp_deps_checker.model import Dependency, Model
from cpp_deps_checker.rules._core import Context, Level, Violation

type _Edge = tuple[str, Dependency]  # source id, dependency


def _components(model: Model) -> list[list[str]]:
    """Tarjan's strongly connected components, each in model order.

    Iterative, so that a deep dependency chain does not hit Python's
    recursion limit.
    """
    index: dict[str, int] = {}
    low: dict[str, int] = {}
    stack: list[str] = []
    on_stack: set[str] = set()
    components: list[list[str]] = []

    def enter(source: str) -> tuple[str, Iterator[str]]:
        index[source] = low[source] = len(index)
        stack.append(source)
        on_stack.add(source)
        dependencies = model.targets[source].declared_dependencies
        return source, (d.target for d in dependencies)

    for root in model.targets:
        if root in index:
            continue
        work = [enter(root)]
        while work:
            source, targets = work[-1]
            for target in targets:
                if target not in index:
                    work.append(enter(target))
                    break
                if target in on_stack:
                    low[source] = min(low[source], index[target])
            else:
                work.pop()
                if work:
                    parent = work[-1][0]
                    low[parent] = min(low[parent], low[source])
                if low[source] == index[source]:
                    members: set[str] = set()
                    while not members or source not in members:
                        member = stack.pop()
                        on_stack.discard(member)
                        members.add(member)
                    components.append([t for t in model.targets if t in members])
    return components


def _shortest_cycle(model: Model, start: str, members: set[str]) -> list[_Edge]:
    """Shortest path of edges from start back to start, within members."""
    parent: dict[str, _Edge] = {}
    queue = deque([start])
    while queue:
        source = queue.popleft()
        for dependency in model.targets[source].declared_dependencies:
            if dependency.target == start:
                path = [(source, dependency)]
                while path[-1][0] != start:
                    path.append(parent[path[-1][0]])
                return path[::-1]
            if dependency.target in members and dependency.target not in parent:
                parent[dependency.target] = (source, dependency)
                queue.append(dependency.target)
    raise AssertionError(f"{start} is in a strongly connected component")


def _violations(model: Model) -> Iterator[Violation]:
    # CMake rejects or drops a target linking itself, so a cycle always spans
    # more than one target.
    for component in _components(model):
        if len(component) < 2:
            continue
        start = component[0]
        path = _shortest_cycle(model, start, set(component))
        names = [model.targets[source].name for source, _ in path]
        message = f"dependency cycle: {' -> '.join([*names, names[0]])}"
        others = [model.targets[t].name for t in component if t not in dict(path)]
        if others:
            message += f" (also strongly connected: {', '.join(others)})"
        yield Violation(
            rule="no-cycles",
            level=Level.ERROR,
            message=message,
            location=path[0][1].location,
        )


def check(ctx: Context) -> list[Violation]:
    return list(_violations(ctx.model))
