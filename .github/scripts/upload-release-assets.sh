#!/bin/bash
# Upload package JSONs and push version commit
# Disable shellcheck warning about using 'cat' to read a file.
# Disable shellcheck warning about $? uses.
# shellcheck disable=SC2002,SC2181

set -e

SCRIPTS_DIR="./.github/scripts"

# Source common GitHub release functions
source "$SCRIPTS_DIR/lib-github-release.sh"

EVENT_JSON=$(cat "$GITHUB_EVENT_PATH")

RELEASE_PRE=$(echo "$EVENT_JSON" | jq -r '.release.prerelease')
RELEASE_TAG=$(echo "$EVENT_JSON" | jq -r '.release.tag_name')
RELEASE_ID=$(echo "$EVENT_JSON" | jq -r '.release.id')

OUTPUT_DIR="$GITHUB_WORKSPACE/build"
PACKAGE_JSON_DEV="package_esp32_dev_index.json"
PACKAGE_JSON_REL="package_esp32_index.json"
PACKAGE_JSON_DEV_CN="package_esp32_dev_index_cn.json"
PACKAGE_JSON_REL_CN="package_esp32_index_cn.json"

echo "Uploading package JSONs ..."

echo "Uploading $PACKAGE_JSON_DEV ..."
echo "Download URL: $(git_safe_upload_asset "$OUTPUT_DIR/$PACKAGE_JSON_DEV")"
echo "Pages URL: $(git_safe_upload_to_pages "$PACKAGE_JSON_DEV" "$OUTPUT_DIR/$PACKAGE_JSON_DEV")"
echo "Download CN URL: $(git_safe_upload_asset "$OUTPUT_DIR/$PACKAGE_JSON_DEV_CN")"
echo "Pages CN URL: $(git_safe_upload_to_pages "$PACKAGE_JSON_DEV_CN" "$OUTPUT_DIR/$PACKAGE_JSON_DEV_CN")"
echo

if [ "$RELEASE_PRE" == "false" ]; then
    echo "Uploading $PACKAGE_JSON_REL ..."
    echo "Download URL: $(git_safe_upload_asset "$OUTPUT_DIR/$PACKAGE_JSON_REL")"
    echo "Pages URL: $(git_safe_upload_to_pages "$PACKAGE_JSON_REL" "$OUTPUT_DIR/$PACKAGE_JSON_REL")"
    echo "Download CN URL: $(git_safe_upload_asset "$OUTPUT_DIR/$PACKAGE_JSON_REL_CN")"
    echo "Pages CN URL: $(git_safe_upload_to_pages "$PACKAGE_JSON_REL_CN" "$OUTPUT_DIR/$PACKAGE_JSON_REL_CN")"
    echo
fi

# Check if we need to push the version update commit
# This value is passed from the build job
NEED_UPDATE_COMMIT="${NEED_UPDATE_COMMIT:-false}"

if [ "$NEED_UPDATE_COMMIT" == "true" ]; then
    echo "Checking out target branch..."
    git fetch origin
    git checkout "$GITHUB_REF_NAME"

    echo "Pushing version update commit..."
    git config user.name "github-actions[bot]"
    git config user.email "41898282+github-actions[bot]@users.noreply.github.com"

    git push
    new_tag_commit=$(git rev-parse HEAD)
    echo "New commit: $new_tag_commit"

    echo "Moving tag $RELEASE_TAG to $new_tag_commit..."
    git tag -f "$RELEASE_TAG" "$new_tag_commit"
    git push --force origin "$RELEASE_TAG"

    echo "Version commit pushed successfully!"
else
    echo "No version update commit needed."
fi

echo "Upload complete!"
