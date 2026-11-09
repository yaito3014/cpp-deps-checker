#!/bin/sh
# The Action's check step, run from the repository root with cpp-deps-checker
# on PATH. Configures SOURCE_DIR into WORK_DIR, writes WORK_DIR/graph.md, and
# checks, printing annotations and appending the job summary to SUMMARY_FILE.
# With BASE_REF set, SOURCE_DIR at the tip of that branch is configured too,
# and its graph is the baseline for the new-edge list. CMAKE_ARGS is split on
# whitespace. Exits with the check's status.
set -eu
set -f # CMAKE_ARGS is split, but not globbed

here=$(dirname "$0")

# Succeeds when WORK_DIR/base.md was written. set -e does not apply inside a
# function called as a condition, hence the || return.
baseline() {
  if [ "$(git rev-parse --is-shallow-repository)" = true ]; then
    # Deepening a full clone would make it shallow.
    git fetch --quiet --no-tags --depth=1 origin "$BASE_REF" || return
  else
    git fetch --quiet --no-tags origin "$BASE_REF" || return
  fi
  git worktree add --quiet --detach "$WORK_DIR/base-source" FETCH_HEAD || return
  # shellcheck disable=SC2086 # split on purpose
  "$here/configure.sh" "$WORK_DIR/base-source/$SOURCE_DIR" "$WORK_DIR/base" \
    $CMAKE_ARGS || return
  cpp-deps-checker graph "$WORK_DIR/base" --output "$WORK_DIR/base.md"
}

echo "::group::Configure $SOURCE_DIR"
# shellcheck disable=SC2086 # split on purpose
"$here/configure.sh" "$SOURCE_DIR" "$WORK_DIR/head" $CMAKE_ARGS
echo "::endgroup::"
cpp-deps-checker graph "$WORK_DIR/head" --output "$WORK_DIR/graph.md"

if [ -n "$BASE_REF" ]; then
  echo "::group::Configure $SOURCE_DIR at $BASE_REF"
  if baseline; then
    set -- --baseline "$WORK_DIR/base.md"
  fi
  echo "::endgroup::"
  if [ $# -eq 0 ]; then
    echo "::warning title=cpp-deps-checker::could not configure $BASE_REF; new edges are not listed"
  fi
  if [ -d "$WORK_DIR/base-source" ]; then
    git worktree remove --force "$WORK_DIR/base-source"
  fi
fi

exec cpp-deps-checker check "$WORK_DIR/head" --annotations \
  --summary "$SUMMARY_FILE" "$@"
