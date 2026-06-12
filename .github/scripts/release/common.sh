#!/bin/bash
# Shared helpers for release build, GitHub API, and package tests.
# shellcheck disable=SC2181

RELEASE_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="$(cd "$RELEASE_LIB_DIR/.." && pwd)"
PACKAGE_JSON_MERGE="${SCRIPTS_DIR}/merge_packages.py"

function init_release_output_dir {
    if [ -n "${OUTPUT_DIR:-}" ]; then
        :
    elif [ -n "${GITHUB_ACTIONS:-}" ]; then
        local ws="${GITHUB_WORKSPACE:-$(git rev-parse --show-toplevel 2>/dev/null || pwd)}"
        OUTPUT_DIR="$ws/build"
    else
        OUTPUT_DIR="$RELEASE_LIB_DIR/tmp"
    fi
    export OUTPUT_DIR
    mkdir -p "$OUTPUT_DIR"
}

function empty_release_output_dir {
    init_release_output_dir
    echo "Cleaning release output directory: $OUTPUT_DIR"
    if [ -d "$OUTPUT_DIR" ]; then
        find "$OUTPUT_DIR" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
    fi
    mkdir -p "$OUTPUT_DIR"
}

function clean_release_test_staging_files {
    init_release_output_dir
    rm -f "$OUTPUT_DIR"/*.local.json
    rm -f "$OUTPUT_DIR"/*.raw
    rm -f "$OUTPUT_DIR/package-json-work.json"
    find "$OUTPUT_DIR" -maxdepth 1 -type d -name 'esp32-core-*' -exec rm -rf {} + 2>/dev/null || true
}

function normalize_release_tag {
    echo "${1#v}"
}

function version_bump_file_paths {
    local root="${1:?root required}" scripts_dir rel
    scripts_dir="${2:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
    root="$(cd "$root" && pwd)"
    while IFS= read -r rel; do
        [ -n "$rel" ] && echo "$root/$rel"
    done < <(cd "$root" && "$scripts_dir/update-version.sh" --print-files)
}

function snapshot_version_bump_files {
    local root="$1" snap_dir="$2" f rel dir
    root="$(cd "$root" && pwd)"
    mkdir -p "$snap_dir"
    while IFS= read -r f; do
        [ -f "$f" ] || continue
        rel="${f#"$root"/}"
        dir="$snap_dir/$(dirname "$rel")"
        mkdir -p "$dir"
        cp -p "$f" "$snap_dir/$rel"
    done < <(version_bump_file_paths "$root")
}

function restore_version_bump_from_snapshot {
    local root="$1" snap_dir="$2" rel snap
    root="$(cd "$root" && pwd)"
    [ -d "$snap_dir" ] || return 1
    while IFS= read -r f; do
        rel="${f#"$root"/}"
        snap="$snap_dir/$rel"
        [ -f "$snap" ] || continue
        mkdir -p "$root/$(dirname "$rel")"
        cp -p "$snap" "$root/$rel"
    done < <(version_bump_file_paths "$root")
}

function snapshot_hosted_dir {
    local root="$1" snap_dir="$2" hosted
    root="$(cd "$root" && pwd)"
    hosted="$root/hosted"
    local meta="$snap_dir/_hosted_snapshot"
    mkdir -p "$meta"
    if [ -d "$hosted" ]; then
        echo "present" >"$meta/state"
        if [ -n "$(ls -A "$hosted" 2>/dev/null)" ]; then
            mkdir -p "$meta/files"
            cp -p "$hosted"/* "$meta/files/" 2>/dev/null || true
        fi
    else
        echo "absent" >"$meta/state"
    fi
}

function restore_hosted_from_snapshot {
    local root="$1" snap_dir="$2" hosted
    root="$(cd "$root" && pwd)"
    hosted="$root/hosted"
    local meta="$snap_dir/_hosted_snapshot"
    [ -f "$meta/state" ] || return 0
    local state
    state=$(<"$meta/state")
    if [ "$state" = "absent" ]; then
        rm -rf "$hosted"
        echo "Removed generated hosted/ directory"
        return 0
    fi
    rm -rf "$hosted"
    if [ -d "$meta/files" ] && [ -n "$(ls -A "$meta/files" 2>/dev/null)" ]; then
        mkdir -p "$hosted"
        cp -p "$meta/files"/* "$hosted/"
        echo "Restored hosted/ from snapshot"
    fi
}

function commit_version_bump_only {
    local version="$1" root="${2:-.}" f
    local -a files=()
    version=$(normalize_release_tag "$version")
    root="$(cd "$root" && pwd)"
    while IFS= read -r f; do
        [ -f "$f" ] && files+=("$f")
    done < <(version_bump_file_paths "$root")
    [ "${#files[@]}" -gt 0 ] || return 1
    git -C "$root" add "${files[@]}"
    if git -C "$root" diff --cached --quiet; then
        echo "Version files already at $version"
        git -C "$root" reset HEAD -- "${files[@]}" 2>/dev/null || true
        return 0
    fi
    git -C "$root" commit -m "change(version): Update core version to $version"
}

function commit_version_and_tag {
    local version="${1:?version required}"
    local scripts_dir="${2:?scripts_dir required}"
    version=$(normalize_release_tag "$version")

    echo "Committing version bump and tagging $version ..."
    "${scripts_dir}/update-version.sh" "$version"
    if [ -n "${GITHUB_ACTIONS:-}" ] || [ -n "${CI:-}" ]; then
        git config user.name "github-actions[bot]"
        git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
    fi
    git add .
    if git diff --cached --quiet; then
        echo "Version files already at $version"
    else
        git commit -m "change(version): Update core version to $version"
    fi
    git push
    git tag -f "$version" "$(git rev-parse HEAD)"
    git push --force origin "$version"
    echo "Tagged $version at $(git rev-parse --short HEAD)"
}

function next_patch_version {
    local latest
    latest=$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//') || return 1
    local major minor patch
    major=$(echo "$latest" | cut -d. -f1)
    minor=$(echo "$latest" | cut -d. -f2)
    patch=$(echo "$latest" | cut -d. -f3 | sed 's/[^0-9].*//')
    echo "${major}.${minor}.$((patch + 1))"
}

function replace_literal_skip_n {
    local skip_n="$1" from_literal="$2" to_literal="$3" infile="$4" outfile="$5"
    SKIP="$skip_n" FROM="$from_literal" TO="$to_literal" \
        perl -pe 'BEGIN{$s=$ENV{SKIP}+0;$from=$ENV{FROM};$to=$ENV{TO};$i=0;} s/\Q$from\E/($i++<$s)?$&:$to/ge' \
        "$infile" > "$outfile"
}

if [ -z "${RELEASE_OUTPUT_DIR_INIT:-}" ]; then
    RELEASE_OUTPUT_DIR_INIT=1
    init_release_output_dir
fi

function merge_package_json {
    local jsonLink=$1 jsonOut=$2
    local output_dir="${OUTPUT_DIR:?}"
    local old_json="$output_dir/oldJson.json"
    local merged_json="$output_dir/mergedJson.json"

    curl -L -o "$old_json" "https://github.com/$GITHUB_REPOSITORY/releases/download/$jsonLink?access_token=$GITHUB_TOKEN" 2>/dev/null
    set +e
    stdbuf -oL python "$PACKAGE_JSON_MERGE" "$jsonOut" "$old_json" > "$merged_json"
    set -e
    if [ ! -s "$merged_json" ]; then
        rm -f "$merged_json"
    else
        rm -f "$jsonOut"
        mv "$merged_json" "$jsonOut"
    fi
    rm -f "$old_json"
}

function sha256_file {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | cut -f1 -d' '
    fi
}

function file_size_bytes {
    if [[ "$OSTYPE" == "darwin"* ]]; then stat -f%z "$1"; else stat -c%s "$1"; fi
}

function get_file_size {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        eval "$(stat -s "$1")"; echo "${st_size:?}"
    else
        stat --printf="%s" "$1"
    fi
}

function git_upload_asset {
    curl -X POST -sH "Authorization: token $GITHUB_TOKEN" -H "Content-Type: application/octet-stream" \
        --data-binary @"$1" "https://uploads.github.com/repos/$GITHUB_REPOSITORY/releases/$2/assets?name=$(basename "$1")"
}

function git_safe_upload_asset {
    local file="$1" release_id="$2" upload_res size up_size
    size=$(get_file_size "$file")
    upload_res=$(git_upload_asset "$file" "$release_id")
    up_size=$(echo "$upload_res" | jq -r '.size')
    [ "$up_size" -eq "$size" ] || return 1
    echo "$upload_res" | jq -r '.browser_download_url'
}

function git_upload_to_pages {
    local path=$1 src=$2 info type message sha="" content="" data
    info=$(curl -s -H "Authorization: token $GITHUB_TOKEN" -H "Accept: application/vnd.github.v3.object+json" \
        -X GET "https://api.github.com/repos/$GITHUB_REPOSITORY/contents/$path?ref=gh-pages")
    type=$(echo "$info" | jq -r '.type')
    message=$(basename "$path")
    if [ "$type" == "file" ]; then
        sha=$(echo "$info" | jq -r '.sha'); sha=",\"sha\":\"$sha\""; message="Updating $message"
    fi
    content=$(base64 -i "$src")
    data="{\"branch\":\"gh-pages\",\"message\":\"$message\",\"content\":\"$content\"$sha}"
    echo "$data" | curl -s -H "Authorization: token $GITHUB_TOKEN" -H "Accept: application/vnd.github.v3.raw+json" \
        -X PUT --data @- "https://api.github.com/repos/$GITHUB_REPOSITORY/contents/$path"
}

function git_safe_upload_to_pages {
    local path=$1 file="$2" upload_res size up_size
    size=$(get_file_size "$file")
    upload_res=$(git_upload_to_pages "$path" "$file")
    up_size=$(echo "$upload_res" | jq -r '.content.size')
    [ "$up_size" -eq "$size" ] || return 1
    echo "$upload_res" | jq -r '.content.download_url'
}

function git_create_draft_release {
    curl -s -X POST -H "Authorization: token $GITHUB_TOKEN" -H "Accept: application/vnd.github+json" \
        "https://api.github.com/repos/$GITHUB_REPOSITORY/releases" \
        -d "$(jq -n --arg tag "$1" --arg target "$2" --arg name "${4:-Release $1}" \
            --argjson pre "$(if [ "$3" = "true" ]; then echo true; else echo false; fi)" \
            '{tag_name: $tag, target_commitish: $target, name: $name, draft: true, prerelease: $pre, generate_release_notes: false}')"
}

function git_publish_release {
    curl -s -X PATCH -H "Authorization: token $GITHUB_TOKEN" -H "Accept: application/vnd.github+json" \
        "https://api.github.com/repos/$GITHUB_REPOSITORY/releases/$1" -d '{"draft": false}'
}

function git_delete_release {
    curl -s -X DELETE -H "Authorization: token $GITHUB_TOKEN" -H "Accept: application/vnd.github+json" \
        "https://api.github.com/repos/$GITHUB_REPOSITORY/releases/$1"
}

function git_get_release_assets {
    curl -s -H "Authorization: token $GITHUB_TOKEN" -H "Accept: application/vnd.github+json" \
        "https://api.github.com/repos/$GITHUB_REPOSITORY/releases/$1/assets"
}

function get_file_url {
    local file_path="$1"
    if [[ "${OS_IS_WINDOWS:-0}" == "1" ]]; then
        echo "file:///$(cygpath -m "$(realpath "$file_path" 2>/dev/null || echo "$file_path")")"
    elif [[ "$file_path" = /* ]]; then
        echo "file://$file_path"
    else
        echo "file://$(cd "$(dirname "$file_path")" && pwd)/$(basename "$file_path")"
    fi
}

function resolve_release_core_version {
    local version json_file repo url ws
    if [ -n "${RELEASE_VERSION:-}" ]; then
        normalize_release_tag "$RELEASE_VERSION"
        return 0
    fi
    if [ -f "${OUTPUT_DIR}/manifest.json" ]; then
        version=$(jq -r '.release_tag // empty' "${OUTPUT_DIR}/manifest.json")
        if [ -n "$version" ] && [ "$version" != "null" ]; then
            echo "$version"
            return 0
        fi
    fi
    for json_file in \
        "${OUTPUT_DIR}/package_esp32_dev_index.json" \
        "${OUTPUT_DIR}/package_esp32_index.json"; do
        if [ -f "$json_file" ]; then
            version=$(jq -r '.packages[0].platforms[0].version // empty' "$json_file")
            if [ -n "$version" ] && [ "$version" != "null" ]; then
                echo "$version"
                return 0
            fi
        fi
    done
    ws="${GITHUB_WORKSPACE:-}"
    if [ -n "$ws" ] && [ -f "$ws/platform.txt" ]; then
        version=$(grep -m1 '^version=' "$ws/platform.txt" | cut -d= -f2- | tr -d '\r')
        if [ -n "$version" ]; then
            echo "$version"
            return 0
        fi
    fi
    if [ "${RELEASE_TEST_MODE:-}" = "post" ]; then
        repo="${GITHUB_REPOSITORY:-espressif/arduino-esp32}"
        url="https://raw.githubusercontent.com/${repo}/gh-pages/package_esp32_dev_index.json"
        version=$(curl -fsSL "$url" | jq -r '.packages[0].platforms[0].version // empty')
        if [ -n "$version" ] && [ "$version" != "null" ]; then
            echo "$version"
            return 0
        fi
    fi
    echo "ERROR: could not determine expected core version (run build-packages first)" >&2
    return 1
}

function installed_esp32_core_version {
    local hw_dir installed expected
    expected="${EXPECTED_CORE_VERSION:-}"
    hw_dir="$(arduino_packages_dir)/packages/esp32/hardware/esp32"
    [ -d "$hw_dir" ] || return 1
    if [ -n "$expected" ] && [ -d "$hw_dir/$expected" ]; then
        echo "$expected"
        return 0
    fi
    installed=$(find "$hw_dir" -mindepth 1 -maxdepth 1 -type d -exec basename {} \; 2>/dev/null | sort -V | tail -1)
    [ -n "$installed" ] || return 1
    echo "$installed"
}

function verify_installed_version {
    local installed expected="${EXPECTED_CORE_VERSION:?EXPECTED_CORE_VERSION required — call resolve_release_core_version first}"
    installed=$(arduino-cli core list 2>/dev/null | awk '/^esp32:esp32[[:space:]]/{print $2}')
    if [ -z "$installed" ]; then
        installed=$(installed_esp32_core_version 2>/dev/null || true)
    fi
    if [ -z "$installed" ]; then
        echo "ERROR: esp32:esp32 not installed (expected $expected)" >&2
        return 1
    fi
    if [ "$installed" != "$expected" ]; then
        echo "ERROR: version mismatch (expected $expected, got $installed)" >&2
        return 1
    fi
    echo "Installed version OK: $installed"
}

function arduino_packages_dir {
    if [[ "${OS_IS_MACOS:-0}" == "1" ]]; then
        echo "$HOME/Library/Arduino15"
    elif [[ "${OS_IS_WINDOWS:-0}" == "1" ]]; then
        echo "${LOCALAPPDATA:-$HOME/AppData/Local}/Arduino15"
    else
        echo "$HOME/.arduino15"
    fi
}

function arduino_cli_host {
    if [[ "${OS_IS_MACOS:-0}" == "1" ]]; then
        case "$(uname -m)" in
            arm64) echo "arm64-apple-darwin" ;;
            *) echo "x86_64-apple-darwin" ;;
        esac
    elif [[ "${OS_IS_WINDOWS:-0}" == "1" ]]; then
        echo "x86_64-mingw32"
    else
        case "$(uname -m)" in
            aarch64|arm64) echo "aarch64-linux-gnu" ;;
            *) echo "x86_64-linux-gnu" ;;
        esac
    fi
}

function esp32_toolchain_host_matches {
    local pkg_json="$1"
    local expected host system
    expected="$(arduino_cli_host)"
    [ -f "$pkg_json" ] || return 1
    host=$(python3 -c "import json; print(json.load(open('$pkg_json')).get('host', ''))" 2>/dev/null || true)
    system=$(python3 -c "import json; print(json.load(open('$pkg_json')).get('system', ''))" 2>/dev/null || true)
    case "$expected" in
        arm64-apple-darwin)
            [[ "$host" == "arm64-apple-darwin" || "$host" == "aarch64-apple-darwin" || "$system" == "darwin_arm64" ]] && return 0
            ;;
        x86_64-apple-darwin)
            [[ "$host" == "x86_64-apple-darwin" || "$system" == "darwin_x86_64" ]] && return 0
            ;;
        *)
            [[ -n "$host" && "$host" == "$expected" ]] && return 0
            [[ -n "$system" && "$system" == "$expected" ]] && return 0
            ;;
    esac
    return 1
}

function purge_stale_esp32_toolchains {
    local tools_dir tool pkg_json reason removed=""
    tools_dir="$(arduino_packages_dir)/packages/esp32/tools"
    for tool in esp-x32 esp-rv32; do
        [ -d "$tools_dir/$tool" ] || continue
        pkg_json=$(find "$tools_dir/$tool" -maxdepth 2 -name package.json 2>/dev/null | head -1)
        reason=""
        if ! esp32_toolchain_host_matches "$pkg_json"; then
            reason="wrong host for $(arduino_cli_host)"
        elif ! find "$tools_dir/$tool" -name '*g++' -type f -perm -111 2>/dev/null | grep -q .; then
            reason="incomplete"
        fi
        if [ -n "$reason" ]; then
            removed="${removed:+$removed, }$tool ($reason)"
            rm -rf "$tools_dir/$tool"
        fi
    done
    if [ -n "$removed" ]; then
        echo "Cleaned stale esp32 toolchains: $removed"
    fi
}

function verify_core_toolchain {
    local tools_dir gpp pkg_json
    tools_dir="$(arduino_packages_dir)/packages/esp32/tools"
    pkg_json=$(find "$tools_dir/esp-x32" -maxdepth 2 -name package.json 2>/dev/null | head -1)
    esp32_toolchain_host_matches "$pkg_json" || return 1
    gpp=$(find "$tools_dir/esp-x32" -name '*g++' -type f 2>/dev/null | head -1)
    if [ -n "$gpp" ] && [ -x "$gpp" ]; then
        echo "Toolchain OK: $gpp"
        return 0
    fi
    echo "ERROR: esp-x32 toolchain incomplete (no g++ under $tools_dir/esp-x32)" >&2
    return 1
}

function ide_v1_expected_toolchain_host {
    # Arduino IDE 1.8.x macOS bundle is x86_64; on Apple Silicon it runs under Rosetta.
    if [[ "${OS_IS_MACOS:-0}" == "1" ]] && [[ "$(uname -m)" == "arm64" ]]; then
        echo "x86_64-apple-darwin"
    else
        arduino_cli_host
    fi
}

function ide_v1_toolchain_host_matches {
    local pkg_json="$1" expected host system
    expected="$(ide_v1_expected_toolchain_host)"
    [ -f "$pkg_json" ] || return 1
    host=$(python3 -c "import json; print(json.load(open('$pkg_json')).get('host', ''))" 2>/dev/null || true)
    system=$(python3 -c "import json; print(json.load(open('$pkg_json')).get('system', ''))" 2>/dev/null || true)
    case "$expected" in
        arm64-apple-darwin)
            [[ "$host" == "arm64-apple-darwin" || "$host" == "aarch64-apple-darwin" || "$system" == "darwin_arm64" ]] && return 0
            ;;
        x86_64-apple-darwin)
            [[ "$host" == "x86_64-apple-darwin" || "$system" == "darwin_x86_64" ]] && return 0
            ;;
        *)
            [[ -n "$host" && "$host" == "$expected" ]] && return 0
            [[ -n "$system" && "$system" == "$expected" ]] && return 0
            ;;
    esac
    return 1
}

function ide_v1_toolchain_ready {
    local tools_dir pkg_json gpp
    tools_dir="$(arduino_packages_dir)/packages/esp32/tools"
    pkg_json=$(find "$tools_dir/esp-x32" -maxdepth 2 -name package.json 2>/dev/null | head -1)
    ide_v1_toolchain_host_matches "$pkg_json" || return 1
    gpp=$(find "$tools_dir/esp-x32" -name '*g++' -type f 2>/dev/null | head -1)
    [ -n "$gpp" ] && [ -x "$gpp" ]
}

function prepare_ide_v1_package_test {
    local tools_dir tool pkg_json
    # arduino-cli tests may leave wrong-arch toolchains; IDE v1 needs its own arch.
    purge_stale_esp32_toolchains
    if [[ "${OS_IS_MACOS:-0}" != "1" ]] || [[ "$(uname -m)" != "arm64" ]]; then
        return 0
    fi
    tools_dir="$(arduino_packages_dir)/packages/esp32/tools"
    for tool in esp-x32 esp-rv32; do
        [ -d "$tools_dir/$tool" ] || continue
        pkg_json=$(find "$tools_dir/$tool" -maxdepth 2 -name package.json 2>/dev/null | head -1)
        if ! ide_v1_toolchain_host_matches "$pkg_json"; then
            echo "IDE v1 prep: removing $tool (wrong arch for IDE v1 under Rosetta)"
            rm -rf "$tools_dir/$tool"
        fi
    done
    if [ -d "$(arduino_packages_dir)/packages/esp32/hardware/esp32" ] && ! ide_v1_toolchain_ready; then
        echo "IDE v1 prep: removing esp32 package (platform without IDE v1 toolchains)"
        rm -rf "$(arduino_packages_dir)/packages/esp32"
    fi
}

function _update_arduino_esp32_path {
    local version="${EXPECTED_CORE_VERSION:-}" pkg_path
    [ -n "$version" ] || return 0
    pkg_path="$(arduino_packages_dir)/packages/esp32/hardware/esp32/$version"
    if [ -d "$pkg_path" ]; then
        export ARDUINO_ESP32_PATH="$pkg_path"
        echo "ARDUINO_ESP32_PATH set to $ARDUINO_ESP32_PATH"
    fi
}

function install_esp32_core_for_test {
    local url="$1"
    local spec="esp32:esp32@${EXPECTED_CORE_VERSION:?EXPECTED_CORE_VERSION required — call resolve_release_core_version first}"

    purge_stale_esp32_toolchains
    arduino-cli core uninstall esp32:esp32 2>/dev/null || true
    arduino-cli core install "$spec" --additional-urls "$url"
    if verify_core_toolchain; then
        _update_arduino_esp32_path
        return 0
    fi

    echo "Forcing toolchain reinstall ..."
    purge_stale_esp32_toolchains
    arduino-cli core uninstall esp32:esp32 2>/dev/null || true
    arduino-cli core install "$spec" --additional-urls "$url"
    verify_core_toolchain && _update_arduino_esp32_path
}

function start_local_package_server {
    local out_dir="$1"
    out_dir="$(cd "$out_dir" && pwd)"
    [ -d "$out_dir" ] || { echo "ERROR: package output directory not found: $out_dir" >&2; return 1; }

    local port_file
    port_file="$(mktemp)"
    python3 - "$out_dir" "$port_file" "${LOCAL_PACKAGE_SERVER_PORT:-0}" <<'PY' &
import http.server, socketserver, sys, os
out_dir, port_file, port_arg = sys.argv[1:4]
port = int(port_arg)
class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=out_dir, **kwargs)
    def log_message(self, *args): pass
bind = ("127.0.0.1", port if port else 0)
with socketserver.TCPServer(bind, Handler) as httpd:
    with open(port_file, "w") as f:
        f.write(str(httpd.server_address[1]))
    httpd.serve_forever()
PY
    LOCAL_PACKAGE_SERVER_PID=$!
    export LOCAL_PACKAGE_SERVER_PID

    local port="" i
    for i in $(seq 1 30); do
        if [ -s "$port_file" ]; then
            port="$(<"$port_file")"
            break
        fi
        if ! kill -0 "$LOCAL_PACKAGE_SERVER_PID" 2>/dev/null; then
            wait "$LOCAL_PACKAGE_SERVER_PID" 2>/dev/null || true
            rm -f "$port_file"
            echo "ERROR: local package server exited before binding a port" >&2
            return 1
        fi
        sleep 0.1
    done
    rm -f "$port_file"
    [ -n "$port" ] || { echo "ERROR: local package server failed to bind a port" >&2; return 1; }

    LOCAL_PACKAGE_SERVER_URL="http://127.0.0.1:${port}"
    export LOCAL_PACKAGE_SERVER_URL

    for i in $(seq 1 30); do
        if python3 -c "import urllib.request; urllib.request.urlopen('$LOCAL_PACKAGE_SERVER_URL/', timeout=2)" >/dev/null 2>&1; then
            return 0
        fi
        if ! kill -0 "$LOCAL_PACKAGE_SERVER_PID" 2>/dev/null; then
            echo "ERROR: local package server exited during startup (port $port)" >&2
            return 1
        fi
        sleep 0.1
    done
    echo "ERROR: local package server not reachable at $LOCAL_PACKAGE_SERVER_URL" >&2
    return 1
}

function stop_local_package_server {
    if [ -n "${LOCAL_PACKAGE_SERVER_PID:-}" ]; then
        kill "$LOCAL_PACKAGE_SERVER_PID" 2>/dev/null || true
        wait "$LOCAL_PACKAGE_SERVER_PID" 2>/dev/null || true
        unset LOCAL_PACKAGE_SERVER_PID LOCAL_PACKAGE_SERVER_URL
    fi
}

function rewrite_json_to_local {
    local src="$1" dst="$2" out_dir="$3"
    local base_url="${LOCAL_PACKAGE_SERVER_URL:?LOCAL_PACKAGE_SERVER_URL required — start local package server first}"
    python3 - "$src" "$dst" "$out_dir" "$base_url" <<'PY'
import json, sys, os
src, dst, out_dir, base_url = sys.argv[1:5]
base_url = base_url.rstrip('/')
def archive_url(filename):
    return f"{base_url}/{filename}"
with open(src) as f:
    data = json.load(f)
def fix_systems(systems):
    for s in systems:
        fn = s.get('archiveFileName', '')
        local = os.path.join(out_dir, fn)
        if fn and os.path.isfile(local):
            s['url'] = archive_url(fn)
    return systems
for pkg in data.get('packages', []):
    for plat in pkg.get('platforms', []):
        fn = plat.get('archiveFileName', '')
        local = os.path.join(out_dir, fn)
        if fn and os.path.isfile(local):
            plat['url'] = archive_url(fn)
    for tool in pkg.get('tools', []):
        tool['systems'] = fix_systems(tool.get('systems', []))
with open(dst, 'w') as f:
    json.dump(data, f, indent=2)
PY
}


function ide_v1_install_boards {
    local url="$1" version_suffix="$2" log rc attempt
    for attempt in 1 2; do
        if [ "$attempt" -eq 2 ]; then
            echo "IDE v1: retrying board install after removing incomplete package ..."
            rm -rf "$(arduino_packages_dir)/packages/esp32"
        fi
        log="$(mktemp)"
        echo "IDE v1: installing esp32:esp32${version_suffix} (may take several minutes) ..."
        run_with_timeout 1800 run_arduino_ide_v1 \
            --pref "boardsmanager.additional.urls=$url" \
            --install-boards "esp32:esp32${version_suffix}" 2>&1 | tee "$log"
        rc=${PIPESTATUS[0]}
        if grep -q "Platform is already installed" "$log"; then
            if verify_installed_version && ide_v1_toolchain_ready; then
                rm -f "$log"
                _update_arduino_esp32_path
                return 0
            fi
            echo "IDE v1: platform already installed but wrong version or incomplete toolchains"
            rm -f "$log"
            continue
        fi
        if [ "$rc" -ne 0 ]; then
            echo "ERROR: IDE v1 board install failed (exit $rc)" >&2
            rm -f "$log"
            return 1
        fi
        if grep -qiE '^Error:|Exception in thread|java\.lang\.' "$log"; then
            echo "ERROR: IDE v1 board install log contains errors" >&2
            rm -f "$log"
            return 1
        fi
        rm -f "$log"
        if verify_installed_version && ide_v1_toolchain_ready; then
            _update_arduino_esp32_path
            return 0
        fi
        echo "IDE v1: install finished but version or toolchains are wrong/incomplete"
    done
    echo "ERROR: IDE v1 toolchains not ready after install" >&2
    return 1
}
source "${SCRIPTS_DIR}/arduino_headless.sh"
