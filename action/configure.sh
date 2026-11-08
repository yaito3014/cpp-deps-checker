#!/bin/sh
# Usage: configure.sh SOURCE_DIR BUILD_DIR [CMAKE_ARG...]
# Configures a CMake project with the inputs cpp-deps-checker reads: a File
# API codemodel-v2 reply, a JSON trace at BUILD_DIR/trace.json, and a
# compilation database with header set verification translation units.
set -eu

source_dir=$1
build_dir=$2
shift 2

query=$build_dir/.cmake/api/v1/query
mkdir -p "$query"
: >"$query/codemodel-v2"
exec cmake -S "$source_dir" -B "$build_dir" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=ON \
  -DCMAKE_OPTIMIZE_DEPENDENCIES:BOOL=OFF \
  -DCMAKE_VERIFY_INTERFACE_HEADER_SETS:BOOL=ON \
  --trace-format=json-v1 --trace-expand \
  "--trace-redirect=$build_dir/trace.json" \
  "$@"
