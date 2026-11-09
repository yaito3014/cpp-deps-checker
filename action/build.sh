#!/bin/sh
# The cpp-deps-checker executable, built from the Action's checkout.
#   build.sh hash SOURCE_DIR               print a hash of the sources it is built from
#   build.sh build SOURCE_DIR BIN_DIR WORK_DIR
#                                          build it into BIN_DIR, with vcpkg at the
#                                          manifest's builtin baseline in WORK_DIR
set -eu

case ${1-} in
hash)
  cd "$2"
  find core clang cli CMakeLists.txt CMakePresets.json vcpkg.json -type f |
    LC_ALL=C sort | xargs sha256sum | sha256sum | cut -c1-16
  ;;
build)
  source_dir=$2 bin_dir=$3 work_dir=$4
  baseline=$(sed -n 's/.*"builtin-baseline": *"\([0-9a-f]*\)".*/\1/p' "$source_dir/vcpkg.json")
  vcpkg=$work_dir/vcpkg
  git init --quiet "$vcpkg"
  git -C "$vcpkg" fetch --quiet --depth=1 https://github.com/microsoft/vcpkg "$baseline"
  git -C "$vcpkg" checkout --quiet FETCH_HEAD
  "$vcpkg/bootstrap-vcpkg.sh" -disableMetrics
  cd "$source_dir"
  VCPKG_ROOT=$vcpkg cmake --preset default -B "$work_dir/build" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
  cmake --build "$work_dir/build" --target cpp_deps_checker_main
  mkdir -p "$bin_dir"
  cp "$work_dir/build/cli/cpp-deps-checker" "$bin_dir/"
  ;;
*)
  echo "usage: build.sh hash SOURCE_DIR | build.sh build SOURCE_DIR BIN_DIR WORK_DIR" >&2
  exit 2
  ;;
esac
