#!/bin/bash
# Test package JSON installation and compilation
# Disable shellcheck warning about $? uses.
# shellcheck disable=SC2181

set -e

SCRIPTS_DIR="./.github/scripts"
OUTPUT_DIR="$GITHUB_WORKSPACE/build"
PACKAGE_JSON_DEV="package_esp32_dev_index.json"
PACKAGE_JSON_REL="package_esp32_index.json"

# Get release info
RELEASE_PRE="${RELEASE_PRE:-false}"

# Convert path to absolute and handle Windows paths for file:// URLs
function get_file_url {
    local file_path="$1"

    # Get absolute path
    if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
        # On Windows, convert to absolute path and use forward slashes
        abs_path=$(cd "$(dirname "$file_path")" && pwd -W)/$(basename "$file_path") 2>/dev/null || echo "$file_path"
        # Ensure forward slashes and proper file:// format for Windows
        abs_path="${abs_path//\\//}"
        echo "file:///$abs_path"
    else
        # On Unix systems, just ensure absolute path
        if [[ "$file_path" = /* ]]; then
            echo "file://$file_path"
        else
            echo "file://$(cd "$(dirname "$file_path")" && pwd)/$(basename "$file_path")"
        fi
    fi
}

echo "Installing arduino-cli (for IDE v2 testing) ..."
# Set up PATH based on OS
if [[ "$OSTYPE" == "msys" || "$OSTYPE" == "win32" ]]; then
    export PATH="$HOME/bin:$PATH"
else
    export PATH="/home/runner/bin:$HOME/bin:$PATH"
fi

source "${SCRIPTS_DIR}/install-arduino-cli.sh"

echo "Installing Arduino IDE v1 (for arduino-builder testing) ..."
source "${SCRIPTS_DIR}/install-arduino-ide.sh"

# For the Chinese mirror, we can't test the package JSONs as the Chinese mirror might not be updated yet.
# So we only test the main package JSON files.

function test_package_json_with_cli {
    local package_json="$1"
    local package_json_path="$OUTPUT_DIR/$package_json"
    local package_json_url
    package_json_url=$(get_file_url "$package_json_path")

    echo "==============================================="
    echo "Testing $package_json with arduino-cli (IDE v2)"
    echo "==============================================="

    echo "Installing esp32 core ..."
    echo "Package JSON URL: $package_json_url"
    arduino-cli core install esp32:esp32 --additional-urls "$package_json_url"
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to install esp32 ($?)"
        return 1
    fi

    echo "Compiling example with arduino-cli ..."
    arduino-cli compile --fqbn esp32:esp32:esp32 "$GITHUB_WORKSPACE"/libraries/ESP32/examples/CI/CIBoardsTest/CIBoardsTest.ino
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to compile example with arduino-cli ($?)"
        return 1
    fi

    echo "Uninstalling esp32 core ..."
    arduino-cli core uninstall esp32:esp32
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to uninstall esp32 ($?)"
        return 1
    fi

    echo "✓ Arduino CLI test successful for $package_json"
    return 0
}

function test_package_json_with_builder {
    local package_json="$1"
    local package_json_path="$OUTPUT_DIR/$package_json"
    local package_json_url
    package_json_url=$(get_file_url "$package_json_path")

    echo "==================================================="
    echo "Testing $package_json with arduino-builder (IDE v1)"
    echo "==================================================="

    echo "Installing esp32 core ..."
    echo "Package JSON URL: $package_json_url"
    arduino-cli core install esp32:esp32 --additional-urls "$package_json_url"
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to install esp32 ($?)"
        return 1
    fi

    # Prepare build directories
    BUILD_DIR="$HOME/.arduino/tests/build_test"
    CACHE_DIR="$HOME/.arduino/cache"
    mkdir -p "$BUILD_DIR"
    mkdir -p "$CACHE_DIR"

    echo "Compiling example with arduino-builder ..."
    "$ARDUINO_IDE_PATH"/arduino-builder -compile -logger=human -core-api-version=10810 \
        -fqbn="esp32:esp32:esp32" \
        -warnings="default" \
        -tools "$ARDUINO_IDE_PATH/tools-builder" \
        -hardware "$ARDUINO_USR_PATH/hardware" \
        -libraries "$ARDUINO_USR_PATH/libraries" \
        -build-cache "$CACHE_DIR" \
        -build-path "$BUILD_DIR" \
        "$GITHUB_WORKSPACE/libraries/ESP32/examples/CI/CIBoardsTest/CIBoardsTest.ino"

    builder_exit=$?

    # Cleanup build directory
    rm -rf "$BUILD_DIR"

    if [ $builder_exit -ne 0 ]; then
        echo "ERROR: Failed to compile example with arduino-builder ($builder_exit)"
        return 1
    fi

    echo "Uninstalling esp32 core ..."
    arduino-cli core uninstall esp32:esp32
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to uninstall esp32 ($?)"
        return 1
    fi

    echo "✓ Arduino Builder test successful for $package_json"
    return 0
}

echo ""
echo "==========================================="
echo "Testing $PACKAGE_JSON_DEV"
echo "==========================================="

# Test with arduino-cli (IDE v2)
test_package_json_with_cli "$PACKAGE_JSON_DEV"

# Test with arduino-builder (IDE v1)
test_package_json_with_builder "$PACKAGE_JSON_DEV"

if [ "$RELEASE_PRE" == "false" ]; then
    echo ""
    echo "==========================================="
    echo "Testing $PACKAGE_JSON_REL"
    echo "==========================================="

    # Test with arduino-cli (IDE v2)
    test_package_json_with_cli "$PACKAGE_JSON_REL"

    # Test with arduino-builder (IDE v1)
    test_package_json_with_builder "$PACKAGE_JSON_REL"
fi

echo ""
echo "==========================================="
echo "✓ All tests passed on $(uname -s)!"
echo "==========================================="
