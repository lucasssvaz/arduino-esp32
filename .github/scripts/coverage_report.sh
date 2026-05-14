#!/bin/bash
#
# coverage_report.sh - Generate code coverage reports for Arduino-ESP32 source files.
#
# This script uses gcovr together with the ESP toolchain gcov binaries installed by
# get.py to process .gcno/.gcda files produced by a --coverage build and generate
# HTML, XML (Cobertura) and/or JSON reports filtered to only Arduino source paths
# (cores/ and libraries/).  IDF prebuilt blobs never contain .gcda files because
# they are shipped as pre-compiled archives and therefore require no filtering.
#
# Usage:
#   coverage_report.sh -t <target> [options]
#
# Required:
#   -t <target>      Target chip name (e.g. esp32, esp32c3, esp32s3).
#                    Used to select the correct toolchain gcov binary.
#
# Options:
#   -b <build_dir>   Directory tree that contains the .gcda/.gcno files produced
#                    by the coverage build.  Defaults to
#                    ~/.arduino/tests/<target>.
#   -o <output_dir>  Directory for the HTML report.
#                    Defaults to coverage_report/.
#   -j <json_file>   Write a gcovr JSON tracefile that can later be merged with
#                    --add-tracefile (-a) from additional runs.
#   -x <xml_file>    Write a Cobertura XML report (useful for CI integrations).
#   -a <tracefile>   Add an existing gcovr JSON tracefile instead of scanning for
#                    .gcda files.  Can be repeated for multiple tracefiles.
#                    When -a is used, no build directory is scanned.
#   -h               Show this help and exit.
#
# Environment:
#   REPO_ROOT        Repository root.  Detected automatically when not set.
#
# Requirements:
#   gcovr >= 6.0  (pip install gcovr)
#   The ESP toolchain must be present at:
#     <REPO_ROOT>/tools/xtensa-esp-elf/  (for xtensa targets)
#     <REPO_ROOT>/tools/riscv32-esp-elf/ (for riscv32 targets)
#   These are installed by running  python tools/get.py  from the repo root.

set -euo pipefail

SCRIPTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${SCRIPTS_DIR}/socs_config.sh"

# ---------------------------------------------------------------------------
# Resolve repository root
# ---------------------------------------------------------------------------
if [ -z "${REPO_ROOT:-}" ]; then
    if [ -d "${GITHUB_WORKSPACE:-}/tools/esp32-arduino-libs" ]; then
        REPO_ROOT="${GITHUB_WORKSPACE}"
    elif [ -d "${ARDUINO_ESP32_PATH:-}/tools/esp32-arduino-libs" ]; then
        REPO_ROOT="${ARDUINO_ESP32_PATH}"
    else
        REPO_ROOT="$(cd "${SCRIPTS_DIR}/../.." && pwd)"
    fi
fi

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
target=""
build_dir=""
output_dir="coverage_report"
json_file=""
xml_file=""
add_tracefiles=()

function print_usage {
    sed -n '/^# Usage:/,/^[^#]/{ /^#/{ s/^# \{0,1\}//; p }; /^[^#]/q }' "${BASH_SOURCE[0]}"
}

while [ $# -gt 0 ]; do
    case "$1" in
    -t )  shift; target="$1"           ;;
    -b )  shift; build_dir="$1"        ;;
    -o )  shift; output_dir="$1"       ;;
    -j )  shift; json_file="$1"        ;;
    -x )  shift; xml_file="$1"         ;;
    -a )  shift; add_tracefiles+=("$1") ;;
    -h )  print_usage; exit 0          ;;
    * )   echo "ERROR: Unknown argument: $1"; print_usage; exit 1 ;;
    esac
    shift
done

if [ -z "$target" ]; then
    echo "ERROR: -t <target> is required"
    print_usage
    exit 1
fi

# ---------------------------------------------------------------------------
# Derive architecture and gcov tool path
# ---------------------------------------------------------------------------
arch=$(get_arch "$target")
gcov_tool="${REPO_ROOT}/tools/${arch}-esp-elf/bin/${arch}-esp-elf-gcov"

if [ ! -x "$gcov_tool" ]; then
    echo "ERROR: gcov tool not found or not executable: ${gcov_tool}"
    echo "       Run  python tools/get.py  from the repository root to install the toolchain."
    exit 1
fi

echo "Target  : ${target}"
echo "Arch    : ${arch}"
echo "gcov    : ${gcov_tool}"

# ---------------------------------------------------------------------------
# Check gcovr availability
# ---------------------------------------------------------------------------
if ! command -v gcovr &> /dev/null; then
    echo "ERROR: gcovr is not installed. Install it with:  pip install gcovr"
    exit 1
fi

echo "gcovr   : $(gcovr --version | head -1)"

# ---------------------------------------------------------------------------
# Build the gcovr invocation
# ---------------------------------------------------------------------------

# Common filters: only report on Arduino source files, never on IDF paths.
# IDF prebuilt blobs don't have .gcda files anyway, but these filters also
# exclude any IDF source headers that may be mentioned in .gcno debug info.
ARDUINO_FILTERS=(
    --filter "${REPO_ROOT}/cores/"
    --filter "${REPO_ROOT}/libraries/"
)

gcovr_cmd=(
    gcovr
    --gcov-executable "${gcov_tool}"
    --root "${REPO_ROOT}"
    "${ARDUINO_FILTERS[@]}"
)

# Source of coverage data: either pre-built tracefiles or a scanned build dir
if [ ${#add_tracefiles[@]} -gt 0 ]; then
    for tf in "${add_tracefiles[@]}"; do
        gcovr_cmd+=(--add-tracefile "${tf}")
    done
else
    if [ -z "$build_dir" ]; then
        build_dir="${HOME}/.arduino/tests/${target}"
    fi
    if [ ! -d "$build_dir" ]; then
        echo "ERROR: Build directory not found: ${build_dir}"
        exit 1
    fi
    echo "Build dir: ${build_dir}"
    gcovr_cmd+=(--search-path "${build_dir}")
fi

# Output: JSON tracefile (always written when requested; also used for merging)
if [ -n "$json_file" ]; then
    mkdir -p "$(dirname "$json_file")"
    gcovr_cmd+=(--json "${json_file}")
fi

# Output: Cobertura XML
if [ -n "$xml_file" ]; then
    mkdir -p "$(dirname "$xml_file")"
    gcovr_cmd+=(--xml "${xml_file}")
fi

# Output: HTML (always generated unless suppressed by only using -j / -x with
# no -o; we still generate HTML by default as it is the most human-readable)
if [ -n "$output_dir" ]; then
    mkdir -p "${output_dir}"
    gcovr_cmd+=(--html-details "${output_dir}/index.html")
fi

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
echo ""
echo "Running: ${gcovr_cmd[*]}"
echo ""
"${gcovr_cmd[@]}"

echo ""
echo "Coverage report complete."
if [ -n "$output_dir" ] && [ -f "${output_dir}/index.html" ]; then
    echo "HTML report : ${output_dir}/index.html"
fi
if [ -n "$xml_file" ] && [ -f "$xml_file" ]; then
    echo "XML report  : ${xml_file}"
fi
if [ -n "$json_file" ] && [ -f "$json_file" ]; then
    echo "JSON trace  : ${json_file}"
fi
