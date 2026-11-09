#!/bin/sh
# LLVM 24 from apt.llvm.org's development snapshot, which carries it until its
# release; GitHub-hosted Ubuntu runners only.
#   llvm.sh version          add the repository, print the snapshot's version
#   llvm.sh install PKG...   install packages from it
set -eu

case ${1-} in
version)
  # shellcheck source=/dev/null # the runner's, not this repository's
  . /etc/os-release
  curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key |
    sudo tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc >/dev/null
  echo "deb https://apt.llvm.org/$VERSION_CODENAME/ llvm-toolchain-$VERSION_CODENAME main" |
    sudo tee /etc/apt/sources.list.d/llvm.list >/dev/null
  sudo apt-get update -q >&2
  # A cache key: the snapshot moves, and its libraries' ABI with it.
  apt-cache policy libclang-cpp24 | sed -n 's/^ *Candidate: //p' | tr -c 'A-Za-z0-9.\n' '-'
  ;;
install)
  shift
  sudo apt-get install -y -q "$@" >&2
  ;;
*)
  echo "usage: llvm.sh version | llvm.sh install PACKAGE..." >&2
  exit 2
  ;;
esac
