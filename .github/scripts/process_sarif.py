#!/usr/bin/env python3

# This script is used to process the SARIF file generated by CodeQL and
# to rename files back to .ino and adjust line numbers to match the original .ino files.

import json
import sys
import os

def process_artifact_location(artifact_location, renamed_files):
    """
    Process a single artifact location to rename .cpp files back to .ino
    """
    if 'uri' in artifact_location:
        uri = artifact_location['uri']
        if uri in renamed_files:
            print(f"Renaming file: {uri} -> {renamed_files[uri]}")
            artifact_location['uri'] = renamed_files[uri]
            return True
    return False

def process_region(region):
    """
    Adjust line numbers in a region by decreasing them by 1
    """
    if 'startLine' in region:
        region['startLine'] = max(1, region['startLine'] - 1)
    if 'endLine' in region:
        region['endLine'] = max(1, region['endLine'] - 1)

def process_physical_location(physical_location, renamed_files):
    """
    Process a physical location to rename files and adjust line numbers
    """
    file_renamed = False

    if 'artifactLocation' in physical_location:
        if process_artifact_location(physical_location['artifactLocation'], renamed_files):
            file_renamed = True

    # Adjust line numbers if the file was renamed
    if file_renamed and 'region' in physical_location:
        process_region(physical_location['region'])

    return file_renamed


def process_sarif_file(sarif_file, renamed_files_file):
    """
    Process SARIF file to rename files back to .ino and adjust line numbers
    """
    # Read the renamed files mapping
    with open(renamed_files_file, 'r') as f:
        renamed_files = json.load(f)

    print(f"Loaded {len(renamed_files)} file mappings:")
    for cpp_file, ino_file in renamed_files.items():
        print(f"  {cpp_file} -> {ino_file}")


    # Read the SARIF file
    with open(sarif_file, 'r') as f:
        sarif_data = json.load(f)

    files_processed = 0

    # Process each run
    if 'runs' in sarif_data:
        for run in sarif_data['runs']:
            # Process results
            if 'results' in run:
                for result in run['results']:
                    # Process all locations in the result
                    if 'locations' in result:
                        for location in result['locations']:
                            if 'physicalLocation' in location:
                                if process_physical_location(location['physicalLocation'], renamed_files):
                                    files_processed += 1

                    # Process related locations if they exist
                    if 'relatedLocations' in result:
                        for location in result['relatedLocations']:
                            if 'physicalLocation' in location:
                                if process_physical_location(location['physicalLocation'], renamed_files):
                                    files_processed += 1

            # Process artifacts if they exist
            if 'artifacts' in run:
                for artifact in run['artifacts']:
                    if 'location' in artifact and 'uri' in artifact['location']:
                        uri = artifact['location']['uri']
                        if uri in renamed_files:
                            artifact['location']['uri'] = renamed_files[uri]
                            files_processed += 1

    print(f"Processed {files_processed} file references")

    # Write the processed SARIF file
    with open(sarif_file, 'w') as f:
        json.dump(sarif_data, f, indent=2)

def main():
    if len(sys.argv) != 3:
        print("Usage: python3 sarif_nobuild.py <sarif_file> <renamed_files_file>")
        sys.exit(1)

    sarif_file = sys.argv[1]
    renamed_files_file = sys.argv[2]

    # Check if files exist
    if not os.path.exists(sarif_file):
        print(f"SARIF file not found: {sarif_file}")
        sys.exit(1)

    if not os.path.exists(renamed_files_file):
        print(f"Renamed files mapping not found: {renamed_files_file}")
        sys.exit(1)

    try:
        process_sarif_file(sarif_file, renamed_files_file)
        print("SARIF file processed successfully")
    except Exception as e:
        print(f"Error processing SARIF file: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

if __name__ == "__main__":
    main()
