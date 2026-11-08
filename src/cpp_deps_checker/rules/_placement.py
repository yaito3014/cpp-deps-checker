from collections.abc import Iterable, Iterator, Mapping, Sequence

from cpp_deps_checker.rules._core import Context, Level, Violation

type _Node = str | None  # a target id, or None for the root above all roots


def _postorder(
    roots: Iterable[str], successors: Mapping[str, Sequence[str]]
) -> list[_Node]:
    """Nodes reachable from the virtual root, in depth-first postorder."""
    order: list[_Node] = []
    seen: set[_Node] = {None}
    stack: list[tuple[_Node, Iterator[str]]] = [(None, iter(roots))]
    while stack:
        node, children = stack[-1]
        for child in children:
            if child not in seen:
                seen.add(child)
                stack.append((child, iter(successors[child])))
                break
        else:
            stack.pop()
            order.append(node)
    return order


def _immediate_dominators(
    roots: Sequence[str], successors: Mapping[str, Sequence[str]]
) -> dict[_Node, _Node]:
    """Cooper, Harvey, and Kennedy, "A Simple, Fast Dominance Algorithm"."""
    order = _postorder(roots, successors)
    number = {node: i for i, node in enumerate(order)}
    predecessors: dict[_Node, list[_Node]] = {node: [] for node in order}
    for root in roots:
        predecessors[root].append(None)
    for node in order:
        if node is not None:
            for successor in successors[node]:
                predecessors[successor].append(node)

    idom: dict[_Node, _Node] = {None: None}

    def intersect(a: _Node, b: _Node) -> _Node:
        while a != b:
            while number[a] < number[b]:
                a = idom[a]
            while number[b] < number[a]:
                b = idom[b]
        return a

    changed = True
    while changed:
        changed = False
        for node in reversed(order[:-1]):  # the virtual root comes last
            processed = [p for p in predecessors[node] if p in idom]
            new = processed[0]
            for predecessor in processed[1:]:
                new = intersect(predecessor, new)
            if node not in idom or idom[node] != new:
                idom[node] = new
                changed = True
    return idom


def _violations(ctx: Context) -> Iterator[Violation]:
    model = ctx.model
    production = ctx.production()
    successors = {
        t.id: [d.target for d in t.declared_dependencies if d.target in production]
        for t in production.values()
    }
    consumed = {d for ds in successors.values() for d in ds}
    # Until the consumer config names the composition roots, every production
    # target that no other production target links is one.
    roots = [t for t in production if t not in consumed]
    for node, dominator in _immediate_dominators(roots, successors).items():
        if node is None or dominator is None:
            continue
        target, owner = production[node], production[dominator]
        if target.source_dir.is_relative_to(owner.source_dir):
            continue
        yield Violation(
            rule="placement",
            level=Level.WARNING,
            message=(
                f"every path to {target.name} goes through {owner.name}; "
                f"move {target.name} under {model.locate(owner.source_dir, None)}/"
            ),
            location=None if target.definition is None else target.definition.location,
        )


def check(ctx: Context) -> list[Violation]:
    return list(_violations(ctx))
