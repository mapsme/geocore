#!/bin/bash
set -u -e

OPT_DEBUG=
OPT_RELEASE=
OPT_CLEAN=
OPT_GCC=
OPT_TARGET=
OPT_PATH=
OPT_COMPILE_DATABASE=
while getopts ":cdrgjp:" opt; do
  case $opt in
    d)
      OPT_DEBUG=1
      ;;
    r)
      OPT_RELEASE=1
      ;;
    c)
      OPT_CLEAN=1
      ;;
    g)
      OPT_GCC=1
      ;;
    j)
      OPT_COMPILE_DATABASE=1
      CMAKE_CONFIG="${CMAKE_CONFIG:-} -DCMAKE_EXPORT_COMPILE_COMMANDS=YES"
      ;;
    p)
      OPT_PATH="$OPTARG"
      ;;
    *)
      echo "This tool builds geocore"
      echo "Usage: $0 [-d] [-r] [-c] [-s] [-t] [-a] [-g] [-j] [-p PATH] [target1 target2 ...]"
      echo
      echo -e "-d\tBuild geocore-debug"
      echo -e "-r\tBuild geocore-release"
      echo -e "-c\tClean before building"
      echo -e "-g\tForce use GCC (only for MacOS X platform)"
      echo -e "-p\tDirectory for built binaries"
      echo -e "-j\tGenerate compile_commands.json"
      echo "By default both configurations is built."
      exit 1
      ;;
  esac
done


OPT_TARGET=${@:$OPTIND}

# By default build everything
if [ -z "$OPT_DEBUG$OPT_RELEASE" ]; then
  OPT_DEBUG=1
  OPT_RELEASE=1
fi

GEOCORE_PATH="$(cd "${GEOCORE_PATH:-$(dirname "$0")/../..}"; pwd)"

DEVTOOLSET_PATH=/opt/rh/devtoolset-7
if [ -d "$DEVTOOLSET_PATH" ]; then
  export MANPATH=
  source "$DEVTOOLSET_PATH/enable"
else
  DEVTOOLSET_PATH=
fi

function find_cmake() {
  # If CMAKE variable is set, use it
  [ -n "${CMAKE-}" -a -x "${CMAKE-}" ] && return 0

  # Find cmake, prefer cmake3
  for name in cmake3 cmake; do
    if command -v "$name" > /dev/null; then
      CMAKE="$name"
      return 0
    fi
  done

  echo 'Error: cmake is not installed.' >&2
  exit 1
}

find_cmake

# OS-specific parameters
if [ "$(uname -s)" == "Darwin" ]; then
  PROCESSES=$(sysctl -n hw.ncpu)

  if [ -n "$OPT_GCC" ]; then
    GCC="$(ls /usr/local/bin | grep '^gcc-[6-9][0-9]\?' -m 1)" || true
    GPP="$(ls /usr/local/bin | grep '^g++-[6-9][0-9]\?' -m 1)" || true
    [ -z "$GCC" -o -z "$GPP" ] \
    && echo "Either gcc or g++ is not found. Note, minimal supported gcc version is 6." \
    && exit 2
    CMAKE_CONFIG="${CMAKE_CONFIG:-} -DCMAKE_C_COMPILER=/usr/local/bin/$GCC \
                                    -DCMAKE_CXX_COMPILER=/usr/local/bin/$GPP"
  fi
fi

build()
{
  CONF=$1
  if [ -n "$OPT_PATH" ]; then
    DIRNAME="$OPT_PATH/geocore-build-$(echo "$CONF" | tr '[:upper:]' '[:lower:]')"
  else
    DIRNAME="$GEOCORE_PATH/../geocore-build-$(echo "$CONF" | tr '[:upper:]' '[:lower:]')"
  fi
  [ -d "$DIRNAME" -a -n "$OPT_CLEAN" ] && rm -r "$DIRNAME"
  if [ ! -d "$DIRNAME" ]; then
    mkdir -p "$DIRNAME"
    ln -s "$GEOCORE_PATH/data" "$DIRNAME/data"
  fi
  cd "$DIRNAME"
  TMP_FILE="build_error.log"
  PROCESSES=$(nproc)
  "$CMAKE" "$GEOCORE_PATH" -DCMAKE_BUILD_TYPE="$CONF" ${CMAKE_CONFIG:-}
  echo ""
  if ! make $OPT_TARGET -j $PROCESSES 2> "$TMP_FILE"; then
    echo '--------------------'
    cat "$TMP_FILE"
    exit 1
  fi

  if [ -n "$OPT_COMPILE_DATABASE" ]; then
    cp "$DIRNAME/compile_commands.json" "$GEOCORE_PATH"
  fi
}

[ -n "$OPT_DEBUG" ]   && build Debug
[ -n "$OPT_RELEASE" ] && build Release
exit 0
