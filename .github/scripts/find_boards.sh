#!/bin/bash

# Unified script to find boards for testing
# Usage:
#   ./find_boards.sh all                    # Find all boards
#   ./find_boards.sh new <owner> <base_ref> # Find only new/modified boards

# Function to create JSON matrix and set environment variables
create_json_matrix_and_set_env() {
    local message_type=$1  # "Boards" or "New boards"
    shift  # Remove the first argument (message_type) from the array
    local boards_array=("$@")

    # Sort the boards array alphabetically
    mapfile -t sorted_boards < <(printf '%s\n' "${boards_array[@]}" | sort)

    local board_count=${#sorted_boards[@]}

    echo "${message_type} found: $board_count"
    echo "BOARD-COUNT=$board_count" >> "$GITHUB_ENV"

    if [ "$board_count" -gt 0 ]; then
        json_matrix='['
        local temp_count=$board_count
        for board in "${sorted_boards[@]}"; do
            json_matrix+='"'$board'"'
            if [ "$temp_count" -gt 1 ]; then
                json_matrix+=","
            fi
            temp_count=$((temp_count - 1))
        done
        json_matrix+=']'

        echo "$json_matrix"
        echo "FQBNS=${json_matrix}" >> "$GITHUB_ENV"
    else
        echo "FQBNS=" >> "$GITHUB_ENV"
    fi
}

# Function to process board name and add to array
process_board() {
    local board_name=$1
    local boards_array_ref=$2

    # skip esp32c2 as we dont build libs for it
    if [ "$board_name" == "esp32c2" ]; then
        echo "Skipping 'espressif:esp32:$board_name'"
        return
    fi

    eval "$boards_array_ref+=(\"espressif:esp32:$board_name\")"
    echo "Added 'espressif:esp32:$board_name' to array"
}

# Function to get all boards
get_all_boards() {
    local boards_array=()
    local boards_list
    boards_list=$(grep '.tarch=' boards.txt)

    while read -r line; do
        local board_name
        board_name=$(echo "$line" | cut -d '.' -f1 | cut -d '#' -f1)
        process_board "$board_name" "boards_array"
    done <<< "$boards_list"

    create_json_matrix_and_set_env "Boards" "${boards_array[@]}"
}

# Function to get new/modified boards
get_new_boards() {
    local owner_repository=$1
    local base_ref=$2

    if [ -z "$owner_repository" ] || [ -z "$base_ref" ]; then
        echo "Error: For 'new' mode, owner_repository and base_ref are required"
        echo "Usage: ./find_boards.sh new <owner_repository> <base_ref>"
        exit 1
    fi

    # Download the boards.txt file from the base branch
    curl -L -o boards_base.txt https://raw.githubusercontent.com/"$owner_repository"/"$base_ref"/boards.txt

    # Compare boards.txt file in the repo with the modified file from PR
    local diff
    diff=$(diff -u boards_base.txt boards.txt)

    # Check if the diff is empty
    if [ -z "$diff" ]; then
        echo "No changes in boards.txt file"
        echo "FQBNS="
        exit 0
    fi

    # Extract added or modified lines (lines starting with '+' or '-')
    local modified_lines
    modified_lines=$(echo "$diff" | grep -E '^[+-][^+-]')

    # Print the modified lines for debugging
    echo "Modified lines:"
    echo "$modified_lines"

    local boards_array=()
    local previous_board=""

    # Extract board names from the modified lines, and add them to the boards_array
    while read -r line; do
        local board_name
        board_name=$(echo "$line" | cut -d '.' -f1 | cut -d '#' -f1)
        # remove + or - from the board name at the beginning
        board_name=${board_name#[-+]}
        if [ "$board_name" != "" ] && [ "$board_name" != "+" ] && [ "$board_name" != "-" ] && [ "$board_name" != "esp32_family" ]; then
            if [ "$board_name" != "$previous_board" ]; then
                process_board "$board_name" "boards_array"
                previous_board="$board_name"
            fi
        fi
    done <<< "$modified_lines"

    create_json_matrix_and_set_env "New boards" "${boards_array[@]}"
}

# Main script logic
mode=$1

if [ "$mode" = "all" ]; then
    get_all_boards
elif [ "$mode" = "new" ]; then
    get_new_boards "$2" "$3"
else
    echo "Error: Invalid mode. Use 'all' or 'new'"
    echo "Usage:"
    echo "  ./find_boards.sh all"
    echo "  ./find_boards.sh new <owner_repository> <base_ref>"
    exit 1
fi
