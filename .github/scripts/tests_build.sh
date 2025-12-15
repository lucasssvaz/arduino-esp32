#!/bin/bash

# Source centralized SoC configuration
SCRIPTS_DIR_CONFIG="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPTS_DIR_CONFIG}/socs_config.sh"

USAGE="
USAGE:
    ${0} -c -type <test_type> <chunk_build_opts>
        Example: ${0} -c -type validation -t esp32 -i 0 -m 15
    ${0} -s sketch_name <build_opts>
        Example: ${0} -s hello_world -t esp32
    ${0} -clean
        Remove build and test generated files
"

function clean {
    rm -rf tests/.pytest_cache
    find tests/ -type d -name 'build*' -exec rm -rf "{}" \+
    find tests/ -type d -name '__pycache__' -exec rm -rf "{}" \+
    find tests/ -name '*.xml' -exec rm -rf "{}" \+
    find tests/ -name 'result_*.json' -exec rm -rf "{}" \+
}

SCRIPTS_DIR="./.github/scripts"
BUILD_CMD=""

chunk_build=0

while [ -n "$1" ]; do
    case $1 in
    -c )
        chunk_build=1
        ;;
    -s )
        shift
        sketch=$1
        ;;
    -t )
        shift
        target=$1
        ;;
    -h )
        echo "$USAGE"
        exit 0
        ;;
    -type )
        shift
        test_type=$1
        ;;
    -clean )
        clean
        exit 0
        ;;
    * )
        break
        ;;
    esac
    shift
done

set -e
source "${SCRIPTS_DIR}/install-arduino-cli.sh"
source "${SCRIPTS_DIR}/install-arduino-core-esp32.sh"
set +e

args=("-ai" "$ARDUINO_IDE_PATH" "-au" "$ARDUINO_USR_PATH")

# Handle default target and sketch logic
if [ -z "$target" ] && [ -z "$sketch" ]; then
    # No target or sketch specified - build all sketches for all targets
    echo "No target or sketch specified, building all sketches for all targets"
    chunk_build=1
    targets_to_build=("${BUILD_TEST_TARGETS[@]}")
elif [ -z "$target" ]; then
    # No target specified, but sketch is specified - build sketch for all targets
    echo "No target specified, building sketch '$sketch' for all targets"
    targets_to_build=("${BUILD_TEST_TARGETS[@]}")
elif [ -z "$sketch" ]; then
    # No sketch specified, but target is specified - build all sketches for target
    echo "No sketch specified, building all sketches for target '$target'"
    chunk_build=1
    targets_to_build=("$target")
else
    # Both target and sketch specified - build single sketch for single target
    targets_to_build=("$target")
fi

if [[ $test_type == "all" ]] || [[ -z $test_type ]]; then
    if [ -n "$sketch" ]; then
        tmp_sketch_path=$(find tests -name "$sketch".ino)
        test_type=$(basename "$(dirname "$(dirname "$tmp_sketch_path")")")
        echo "Sketch $sketch test type: $test_type"
        test_folder="$PWD/tests/$test_type"
    else
        test_folder="$PWD/tests"
    fi
else
    test_folder="$PWD/tests/$test_type"
fi

# Loop through all targets to build
for current_target in "${targets_to_build[@]}"; do
    echo "Building for target: $current_target"
    local_args=("${args[@]}")

    if [ $chunk_build -eq 1 ]; then
        BUILD_CMD="${SCRIPTS_DIR}/sketch_utils.sh chunk_build"
        local_args+=("-p" "$test_folder" "-i" "0" "-m" "1" "-t" "$current_target")
    else
        BUILD_CMD="${SCRIPTS_DIR}/sketch_utils.sh build"
        local_args+=("-s" "$test_folder/$sketch" "-t" "$current_target")
    fi

    ${BUILD_CMD} "${local_args[@]}" "$@"
done
