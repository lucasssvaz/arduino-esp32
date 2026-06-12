#!/bin/bash
# GitHub release operations: draft | tag | publish | finalize | delete
# shellcheck disable=SC2181

set -e

RELEASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="$(cd "$RELEASE_DIR/.." && pwd)"
source "$RELEASE_DIR/common.sh"

GITHUB_WORKSPACE="${GITHUB_WORKSPACE:-$(pwd)}"
ACTION="${1:?Usage: github-release.sh draft|tag|publish|finalize|delete}"

RELEASE_PRE="${RELEASE_PRE:-false}"
BUILD_REF="${BUILD_REF:-HEAD}"

if [ "$ACTION" != "delete" ]; then
    RELEASE_TAG="${RELEASE_TAG:?RELEASE_TAG required}"
    RELEASE_TAG=$(normalize_release_tag "$RELEASE_TAG")
fi

cmd_draft() {
    MANIFEST="$OUTPUT_DIR/manifest.json"
    [ -f "$MANIFEST" ] || { echo "ERROR: manifest.json not found"; exit 1; }
    [ -n "${GITHUB_TOKEN:-}" ] && [ -n "${GITHUB_REPOSITORY:-}" ] || { echo "ERROR: GITHUB_TOKEN and GITHUB_REPOSITORY required"; exit 1; }

    local draft_tag="${DRAFT_RELEASE_TAG:-$RELEASE_TAG}"
    local release_res RELEASE_ID assets_json='{}'
    release_res=$(git_create_draft_release "$draft_tag" "$BUILD_REF" "$RELEASE_PRE")
    RELEASE_ID=$(echo "$release_res" | jq -r '.id')
    [ -n "$RELEASE_ID" ] && [ "$RELEASE_ID" != "null" ] || { echo "$release_res"; exit 1; }

    upload_record() {
        local fn="$1" url
        [ -n "$fn" ] && [ "$fn" != "null" ] || return 0
        [ -f "$OUTPUT_DIR/$fn" ] || { echo "ERROR: missing $fn"; exit 1; }
        url=$(git_safe_upload_asset "$OUTPUT_DIR/$fn" "$RELEASE_ID")
        assets_json=$(echo "$assets_json" | jq --arg k "$fn" --arg v "$url" '. + {($k): $v}')
        echo "Uploaded $fn"
    }

    upload_record "$(jq -r '.core.zip.filename' "$MANIFEST")"
    upload_record "$(jq -r '.core.xz.filename // empty' "$MANIFEST")"
    upload_record "$(jq -r '.libs_xz.filename // empty' "$MANIFEST")"
    while IFS= read -r soc_file; do upload_record "$soc_file"; done < <(jq -r '.soc_libs[].filename' "$MANIFEST")

    jq -n --argjson id "$RELEASE_ID" --arg tag "$draft_tag" --argjson assets "$assets_json" \
        '{release_id: $id, tag_name: $tag, assets: $assets}' > "$OUTPUT_DIR/draft-assets.json"
    echo "Draft release $RELEASE_ID ready"
}

cmd_publish() {
    DRAFT_ASSETS="$OUTPUT_DIR/draft-assets.json"
    [ -f "$DRAFT_ASSETS" ] || { echo "ERROR: draft-assets.json not found"; exit 1; }
    local RELEASE_ID result draft assets assets_map='{}'
    RELEASE_ID=$(jq -r '.release_id' "$DRAFT_ASSETS")
    result=$(git_publish_release "$RELEASE_ID")
    draft=$(echo "$result" | jq -r '.draft')
    [ "$draft" = "false" ] || { echo "$result"; exit 1; }
    assets=$(git_get_release_assets "$RELEASE_ID")
    while IFS= read -r row; do
        assets_map=$(echo "$assets_map" | jq --arg n "$(echo "$row" | jq -r '.name')" \
            --arg u "$(echo "$row" | jq -r '.browser_download_url')" '. + {($n): $u}')
    done < <(echo "$assets" | jq -c '.[]')
    jq --argjson assets "$assets_map" '.assets = $assets' "$DRAFT_ASSETS" > "$DRAFT_ASSETS.tmp"
    mv "$DRAFT_ASSETS.tmp" "$DRAFT_ASSETS"
    echo "Published release $RELEASE_TAG"
}

cmd_tag() {
    [ -n "${GITHUB_TOKEN:-}" ] || { echo "ERROR: GITHUB_TOKEN required"; exit 1; }
    commit_version_and_tag "$RELEASE_TAG" "$SCRIPTS_DIR"
}

cmd_finalize() {
    DRAFT_ASSETS="$OUTPUT_DIR/draft-assets.json"
    RELEASE_ID=$(jq -r '.release_id' "$DRAFT_ASSETS")
    local dev=package_esp32_dev_index.json rel=package_esp32_index.json
    local dev_cn=package_esp32_dev_index_cn.json rel_cn=package_esp32_index_cn.json

    [ -f "$OUTPUT_DIR/$dev" ] || {
        echo "ERROR: $OUTPUT_DIR/$dev not found (generate with JSON_MODE=final before finalize)" >&2
        exit 1
    }

    git_safe_upload_asset "$OUTPUT_DIR/$dev" "$RELEASE_ID"
    git_safe_upload_to_pages "$dev" "$OUTPUT_DIR/$dev"
    git_safe_upload_asset "$OUTPUT_DIR/$dev_cn" "$RELEASE_ID"
    git_safe_upload_to_pages "$dev_cn" "$OUTPUT_DIR/$dev_cn"

    if [ "$RELEASE_PRE" = "false" ]; then
        git_safe_upload_asset "$OUTPUT_DIR/$rel" "$RELEASE_ID"
        git_safe_upload_to_pages "$rel" "$OUTPUT_DIR/$rel"
        git_safe_upload_asset "$OUTPUT_DIR/$rel_cn" "$RELEASE_ID"
        git_safe_upload_to_pages "$rel_cn" "$OUTPUT_DIR/$rel_cn"
    fi

    echo "Finalize complete"
}

cmd_delete() {
    local id="${RELEASE_ID:-$(jq -r '.release_id' "$OUTPUT_DIR/draft-assets.json" 2>/dev/null)}"
    [ -n "$id" ] && [ "$id" != "null" ] || exit 0
    git_delete_release "$id"
    echo "Deleted draft release $id"
}

case "$ACTION" in
    draft) cmd_draft ;;
    tag) cmd_tag ;;
    publish) cmd_publish ;;
    finalize) cmd_finalize ;;
    delete) cmd_delete ;;
    *) echo "Unknown action: $ACTION"; exit 1 ;;
esac
