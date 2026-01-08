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

    echo "==================================================="
    echo "Testing $package_json with arduino-builder (IDE v1)"
    echo "==================================================="

    echo "Parsing package JSON to get core download URL ..."
    local core_url
    local core_filename
    local core_checksum
    core_url=$(jq -r '.packages[0].platforms[0].url' "$package_json_path")
    core_filename=$(jq -r '.packages[0].platforms[0].archiveFileName' "$package_json_path")
    core_checksum=$(jq -r '.packages[0].platforms[0].checksum' "$package_json_path" | sed 's/^SHA-256://')

    echo "Core package URL: $core_url"
    echo "Core package file: $core_filename"

    # Download the core package
    echo "Downloading esp32 core package ..."
    local temp_dir="$HOME/.arduino/temp_install"
    mkdir -p "$temp_dir"

    if ! curl -fsSL -o "$temp_dir/$core_filename" "$core_url"; then
        echo "ERROR: Failed to download core package"
        rm -rf "$temp_dir"
        return 1
    fi

    # Verify checksum
    echo "Verifying package checksum ..."
    if [[ "$OSTYPE" == "darwin"* ]]; then
        downloaded_checksum=$(shasum -a 256 "$temp_dir/$core_filename" | cut -d' ' -f1)
    else
        downloaded_checksum=$(sha256sum "$temp_dir/$core_filename" | cut -d' ' -f1)
    fi

    if [ "$downloaded_checksum" != "$core_checksum" ]; then
        echo "ERROR: Checksum mismatch!"
        echo "Expected: $core_checksum"
        echo "Got: $downloaded_checksum"
        rm -rf "$temp_dir"
        return 1
    fi
    echo "✓ Checksum verified"

    # Extract to hardware folder (IDE v1 style)
    echo "Installing esp32 core to hardware folder ..."
    local install_dir="$ARDUINO_USR_PATH/hardware/espressif"
    mkdir -p "$install_dir"

    echo "Extracting core package ..."
    if [[ "$core_filename" == *.zip ]]; then
        unzip -q "$temp_dir/$core_filename" -d "$temp_dir"
    else
        echo "ERROR: Unsupported archive format: $core_filename"
        rm -rf "$temp_dir"
        return 1
    fi

    # Move extracted folder to esp32 (matching IDE v1 structure)
    local extracted_folder=""
    for item in "$temp_dir"/*; do
        if [ -d "$item" ] && [ "$(basename "$item")" != "$core_filename" ]; then
            extracted_folder=$(basename "$item")
            break
        fi
    done

    if [ -z "$extracted_folder" ]; then
        echo "ERROR: Could not find extracted folder"
        rm -rf "$temp_dir"
        return 1
    fi

    mv "$temp_dir/$extracted_folder" "$install_dir/esp32"

    echo "✓ Core installed to $install_dir/esp32"

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

    # Cleanup
    rm -rf "$BUILD_DIR"
    rm -rf "$temp_dir"
    rm -rf "$install_dir/esp32"

    if [ $builder_exit -ne 0 ]; then
        echo "ERROR: Failed to compile example with arduino-builder ($builder_exit)"
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
