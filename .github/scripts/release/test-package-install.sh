#!/bin/bash
# Package install tests: pre (local JSON) | post (live gh-pages URLs)
# shellcheck disable=SC2181

RELEASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="$(cd "$RELEASE_DIR/.." && pwd)"
source "$RELEASE_DIR/common.sh"
source "${SCRIPTS_DIR}/mock_upload_lib.sh"
source "${SCRIPTS_DIR}/env.sh"

export MOCK_ESPTOOL_OVERRIDE="${MOCK_ESPTOOL_OVERRIDE:-1}"

MODE="${1:?Usage: test-package-install.sh pre|post}"
GITHUB_WORKSPACE="${GITHUB_WORKSPACE:-$(pwd)}"
RELEASE_PRE="${RELEASE_PRE:-false}"
RELEASE_TEST_MODE="$MODE"
SKETCH="$GITHUB_WORKSPACE/libraries/ESP32/examples/CI/CIBoardsTest/CIBoardsTest.ino"
# 115200 works on PTY mocks; default 921600 often fails on virtual serial ports.
MOCK_UPLOAD_FQBN="$("${SCRIPTS_DIR}/sketch_utils.sh" default_upload_test_fqbn esp32 esp32:esp32)"

declare -a TEST_RESULTS=()
TEST_FAILURES=0

if [[ "$OS_IS_WINDOWS" == "1" ]]; then export PATH="$HOME/bin:$PATH"
else export PATH="/home/runner/bin:$HOME/bin:$PATH"; fi
source "${SCRIPTS_DIR}/install-arduino-cli.sh"

# CI sets RELEASE_VERSION from the workflow input; local runs infer it from build artifacts.
EXPECTED_CORE_VERSION="$(resolve_release_core_version)" || {
    TEST_FAILURES=1
    exit 1
}
export EXPECTED_CORE_VERSION
export RELEASE_VERSION="$EXPECTED_CORE_VERSION"
echo "Expected core version: $EXPECTED_CORE_VERSION"

record_test() {
    TEST_RESULTS+=("$1|$2")
    [ "$1" = "FAIL" ] && TEST_FAILURES=$((TEST_FAILURES + 1))
}

run_test() {
    local name="$1" rc
    shift
    "$@"
    rc=$?
    case "$rc" in
        0) record_test PASS "$name" ;;
        2) record_test SKIP "$name" ;;
        *) record_test FAIL "$name" ;;
    esac
    return 0
}

print_test_summary() {
    local pass=0 skip=0 entry status name
    {
        echo ""
        echo "=== Test summary ($MODE-release, $(uname -s)) ==="
        for entry in "${TEST_RESULTS[@]}"; do
            status="${entry%%|*}"
            name="${entry#*|}"
            case "$status" in
                PASS)
                    pass=$((pass + 1))
                    echo "  ✓ PASS  $name"
                    ;;
                SKIP)
                    skip=$((skip + 1))
                    echo "  - SKIP  $name"
                    ;;
                FAIL)
                    echo "  ✗ FAIL  $name"
                    ;;
                *)
                    echo "  ✗ UNKNOWN STATUS $name"
                    ;;
            esac
        done
        echo ""
        if [ "$TEST_FAILURES" -eq 0 ]; then
            echo "Result: PASSED ($pass passed${skip:+, $skip skipped})"
        else
            echo "Result: FAILED ($TEST_FAILURES failed, $pass passed${skip:+, $skip skipped})"
        fi
    } >&2
    [ "$TEST_FAILURES" -eq 0 ]
}

finish_tests() {
    restore_mock_esptool_override
    print_test_summary
    exit $((TEST_FAILURES > 0 ? 1 : 0))
}
trap finish_tests EXIT

test_cli_url() {
    local url="$1" label="$2"
    echo ""; echo "=== Testing $label (compile + mock upload) ==="; echo "URL: $url"
    install_esp32_core_for_test "$url"
    install_mock_esptool_override || return 1
    verify_installed_version
    local build_dir
    build_dir="$(mock_upload_mktemp_build_dir)"
    start_mock_bootloader esp32
    mock_upload_twice_cli "$MOCK_UPLOAD_FQBN" "$SKETCH" "$build_dir"
    stop_mock_bootloader
    rm -rf "$build_dir"
    arduino-cli core uninstall esp32:esp32
    echo "✓ $label"
}

test_ide_v1_url() {
    local url="$1" label="$2" rc
    local -a ide_args

    source "${SCRIPTS_DIR}/install-arduino-ide.sh"
    if [ ! -d "$ARDUINO_IDE_PATH" ]; then
        echo "WARNING: IDE v1 not installed, skipping"
        return 2
    fi
    ide_v1_install_boards "$url" ":$EXPECTED_CORE_VERSION" || return 1
    install_mock_esptool_override || return 1
    verify_installed_version || return 1

    local build_dir
    build_dir="$(mock_upload_mktemp_build_dir)"
    echo "IDE v1: compile + mock upload (twice) $label ..."
    start_mock_bootloader esp32 || return 1
    mock_upload_twice_ide_v1 "$MOCK_UPLOAD_FQBN" "$SKETCH" "$build_dir" \
        --pref "boardsmanager.additional.urls=$url" || rc=$?
    stop_mock_bootloader
    rm -rf "$build_dir"
    [ "$rc" -eq 0 ] || { echo "ERROR: IDE v1 failed (exit $rc)" >&2; return 1; }
    # Arduino IDE 1.8 macOS is x86_64 (Rosetta on Apple Silicon) and installs x86_64 toolchains.
    purge_stale_esp32_toolchains
    echo "✓ IDE v1 $label"
}

run_pre_tests() {
    local DEV=package_esp32_dev_index.json REL=package_esp32_index.json
    local LOCAL_DEV="$OUTPUT_DIR/${DEV}.local.json"
    local LOCAL_REL="$OUTPUT_DIR/${REL}.local.json"

    clean_release_test_staging_files

    [ -f "$OUTPUT_DIR/$DEV" ] || {
        echo "ERROR: $OUTPUT_DIR/$DEV not found" >&2
        TEST_FAILURES=1
        return 1
    }
    start_local_package_server "$OUTPUT_DIR"
    trap 'stop_local_package_server; finish_tests' EXIT

    rewrite_json_to_local "$OUTPUT_DIR/$DEV" "$LOCAL_DEV" "$OUTPUT_DIR"
    run_test "arduino-cli: $DEV (compile + mock upload)" \
        test_cli_url "$(get_file_url "$LOCAL_DEV")" "$DEV"

    if [ "$RELEASE_PRE" = "false" ] && [ -f "$OUTPUT_DIR/$REL" ]; then
        rewrite_json_to_local "$OUTPUT_DIR/$REL" "$LOCAL_REL" "$OUTPUT_DIR"
        run_test "arduino-cli: $REL (compile + mock upload)" \
            test_cli_url "$(get_file_url "$LOCAL_REL")" "$REL"
    elif [ "$RELEASE_PRE" = "true" ]; then
        record_test SKIP "arduino-cli: $REL (prerelease)"
    fi

    echo ""
    echo "=== IDE v1 pre-release ==="
    prepare_ide_v1_package_test
    run_test "IDE v1: $DEV (compile + mock upload)" \
        test_ide_v1_url "${LOCAL_PACKAGE_SERVER_URL}/$(basename "$LOCAL_DEV")" "$DEV"
    if [ "$RELEASE_PRE" = "false" ] && [ -f "$LOCAL_REL" ]; then
        run_test "IDE v1: $REL (compile + mock upload)" \
            test_ide_v1_url "${LOCAL_PACKAGE_SERVER_URL}/$(basename "$LOCAL_REL")" "$REL"
    elif [ "$RELEASE_PRE" = "true" ]; then
        record_test SKIP "IDE v1: $REL (prerelease)"
    fi
}

run_post_tests() {
    local REPO="${GITHUB_REPOSITORY:-espressif/arduino-esp32}"
    local DEV_URL="https://raw.githubusercontent.com/${REPO}/gh-pages/package_esp32_dev_index.json"
    local REL_URL="https://raw.githubusercontent.com/${REPO}/gh-pages/package_esp32_index.json"

    run_test "arduino-cli: package_esp32_dev_index.json (compile + mock upload)" \
        test_cli_url "$DEV_URL" "package_esp32_dev_index.json"
    if [ "$RELEASE_PRE" = "false" ]; then
        run_test "arduino-cli: package_esp32_index.json (compile + mock upload)" \
            test_cli_url "$REL_URL" "package_esp32_index.json"
    else
        record_test SKIP "arduino-cli: package_esp32_index.json (prerelease)"
    fi

    echo ""
    echo "=== IDE v1 post-release ==="
    prepare_ide_v1_package_test
    run_test "IDE v1: dev JSON (compile + mock upload)" \
        test_ide_v1_url "$DEV_URL" "dev JSON"
    if [ "$RELEASE_PRE" = "false" ]; then
        run_test "IDE v1: release JSON (compile + mock upload)" \
            test_ide_v1_url "$REL_URL" "release JSON"
    else
        record_test SKIP "IDE v1: release JSON (prerelease)"
    fi
}

case "$MODE" in
    pre) run_pre_tests ;;
    post) run_post_tests ;;
    *)
        echo "Unknown mode: $MODE" >&2
        TEST_FAILURES=1
        ;;
esac
