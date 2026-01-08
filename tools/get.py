#!/usr/bin/env python

"""Script to download and extract tools

This script will download and extract required tools into the current directory.
Tools list is obtained from package/package_esp32_index.template.json file.
"""

from __future__ import print_function

__author__ = "Ivan Grokhotkov"
__version__ = "2015"

import os
import shutil
import errno
import os.path
import hashlib
import json
import platform
import sys
import tarfile
import zipfile
import re
import time
import argparse

# Initialize start_time globally
start_time = -1

if sys.version_info[0] == 3:
    from urllib.request import urlretrieve
    from urllib.request import urlopen

    unicode = lambda s: str(s)  # noqa: E731
else:
    # Not Python 3 - today, it is most likely to be Python 2
    from urllib import urlretrieve
    from urllib import urlopen

if "Windows" in platform.system():
    import requests

# determine if application is a script file or frozen exe
if getattr(sys, "frozen", False):
    current_dir = os.path.dirname(os.path.realpath(unicode(sys.executable)))
elif __file__:
    current_dir = os.path.dirname(os.path.realpath(unicode(__file__)))

dist_dir = current_dir + "/dist/"


def is_safe_archive_path(path):
    # Check for absolute paths (both Unix and Windows style)
    if path.startswith("/") or (len(path) > 1 and path[1] == ":" and path[2] in "\\/"):
        raise ValueError(f"Absolute path not allowed: {path}")

    # Normalize the path to handle any path separators
    normalized_path = os.path.normpath(path)

    # Check for directory traversal attempts using normalized path
    if ".." in normalized_path.split(os.sep):
        raise ValueError(f"Directory traversal not allowed: {path}")

    # Additional check for paths that would escape the target directory
    if normalized_path.startswith(".."):
        raise ValueError(f"Path would escape target directory: {path}")

    # Check for any remaining directory traversal patterns in the original path
    # This catches cases that might not be normalized properly
    path_parts = path.replace("\\", "/").split("/")
    if ".." in path_parts:
        raise ValueError(f"Directory traversal not allowed: {path}")

    return True


def safe_tar_extract(tar_file, destination):
    # Validate all paths before extraction
    for member in tar_file.getmembers():
        is_safe_archive_path(member.name)

    # If all paths are safe, proceed with extraction
    tar_file.extractall(destination, filter="tar")


def safe_zip_extract(zip_file, destination):
    # Validate all paths before extraction
    for name in zip_file.namelist():
        is_safe_archive_path(name)

    # If all paths are safe, proceed with extraction
    zip_file.extractall(destination)


def sha256sum(filename, blocksize=65536):
    hash = hashlib.sha256()
    with open(filename, "rb") as f:
        for block in iter(lambda: f.read(blocksize), b""):
            hash.update(block)
    return hash.hexdigest()


def mkdir_p(path):
    try:
        os.makedirs(path)
    except OSError as exc:
        if exc.errno != errno.EEXIST or not os.path.isdir(path):
            raise


def format_time(seconds):
    minutes, seconds = divmod(seconds, 60)
    return "{:02}:{:05.2f}".format(int(minutes), seconds)


def report_progress(block_count, block_size, total_size, start_time):
    downloaded_size = block_count * block_size
    time_elapsed = time.time() - start_time
    current_speed = downloaded_size / (time_elapsed)

    if sys.stdout.isatty():
        if total_size > 0:
            percent_complete = min((downloaded_size / total_size) * 100, 100)
            sys.stdout.write(
                f"\rDownloading... {percent_complete:.2f}% - {downloaded_size / 1024 / 1024:.2f} MB downloaded - Elapsed Time: {format_time(time_elapsed)} - Speed: {current_speed / 1024 / 1024:.2f} MB/s"  # noqa: E501
            )
        else:
            sys.stdout.write(
                f"\rDownloading... {downloaded_size / 1024 / 1024:.2f} MB downloaded - Elapsed Time: {format_time(time_elapsed)} - Speed: {current_speed / 1024 / 1024:.2f} MB/s"  # noqa: E501
            )
        sys.stdout.flush()


def print_verification_progress(total_files, i, t1):
    if sys.stdout.isatty():
        sys.stdout.write(f"\rElapsed time {format_time(time.time() - t1)}")
        sys.stdout.flush()


def verify_files(filename, destination, rename_to):
    # Set the path of the extracted directory
    extracted_dir_path = destination
    if filename.endswith(".zip"):
        try:
            archive = zipfile.ZipFile(filename, "r")
            file_list = archive.namelist()
        except zipfile.BadZipFile:
            if verbose:
                print("Verification failed: Bad zip file")
            return False
    elif filename.endswith(".tar.gz"):
        try:
            archive = tarfile.open(filename, "r:gz")
            file_list = archive.getnames()
        except tarfile.ReadError:
            if verbose:
                print("Verification failed: Bad tar.gz file")
            return False
    elif filename.endswith(".tar.xz"):
        try:
            archive = tarfile.open(filename, "r:xz")
            file_list = archive.getnames()
        except tarfile.ReadError:
            if verbose:
                print("Verification failed: Bad tar.xz file")
            return False
    else:
        raise NotImplementedError("Unsupported archive type")

    try:
        first_dir = file_list[0].split("/")[0]
        for zipped_file in file_list:
            local_path = os.path.join(extracted_dir_path, zipped_file.replace(first_dir, rename_to, 1))
            if not os.path.exists(local_path):
                if verbose:
                    print(f"\nMissing {zipped_file} on location: {extracted_dir_path}")
                return False
    except Exception as e:
        print(f"\nError: {e}")
        return False

    return True


def _soc_base_name(soc_dir_name):
    # Variant folders follow pattern like esp32p4_es -> base is esp32p4
    return soc_dir_name.split("_", 1)[0]


def _archive_file_list(cfile):
    # zipfile.ZipFile has namelist(); tarfile has getnames()
    return cfile.namelist() if hasattr(cfile, "namelist") else cfile.getnames()


def _get_soc_info_from_archive(file_list):
    """
    Given a list of paths from the archive, infer:
      - SoC base names (grouping variants like esp32p4_es -> esp32p4)
      - top-level files under the archive root (without hardcoding filenames or extensions)
    """
    if not file_list:
        return [], []

    root = file_list[0].split("/")[0]

    # Names that appear as directories under <root>/ (detected by <root>/<name>/...)
    dir_names = set()
    # Names that appear as files directly under <root>/ (detected by exactly <root>/<name>)
    top_level_files = set()

    for p in file_list:
        parts = p.split("/")
        if len(parts) < 2 or parts[0] != root:
            continue

        second = parts[1]
        if not second:
            continue

        if len(parts) >= 3:
            dir_names.add(second)
        elif len(parts) == 2:
            top_level_files.add(second)

    # Libraries: SoC dirs are top-level dirs excluding hosted
    soc_dirs = sorted([d for d in dir_names if d != "hosted"])
    bases = sorted({_soc_base_name(d) for d in soc_dirs})

    return bases, sorted(top_level_files)


def _verify_split_libs(destination, bases, checksum, top_level_files=None):
    """Verify per-SoC split libs exist and match checksum marker, plus expected top-level files."""
    top_level_files = top_level_files or []
    for base in bases:
        out_dir = os.path.join(destination, f"{base}-libs")
        if verbose:
            print(f"\nVerifying split libs: {out_dir}")
        if not os.path.isdir(out_dir):
            if verbose:
                print(f"  Missing directory: {out_dir}")
            return False

        # Ensure checksum marker exists
        chk_path = os.path.join(out_dir, ".package_checksum")
        if not os.path.exists(chk_path):
            if verbose:
                print(f"  Missing file: {chk_path}")
            return False

        # Ensure all top-level files from the archive root exist in each <base>-libs
        for f_name in top_level_files:
            f_path = os.path.join(out_dir, f_name)
            if not os.path.exists(f_path):
                if verbose:
                    print(f"  Missing top-level file: {f_path}")
                return False

        with open(chk_path, "r") as f:
            if f.read() != checksum:
                if verbose:
                    print("  Checksum mismatch in .package_checksum")
                return False

        # Ensure at least one SoC directory exists for this base (base or base_*)
        has_soc_dir = False
        for name in os.listdir(out_dir):
            p = os.path.join(out_dir, name)
            if os.path.isdir(p) and (name == base or name.startswith(base + "_")):
                has_soc_dir = True
                break
        if not has_soc_dir:
            if verbose:
                print(f"  Missing SoC directory for base '{base}' (expected '{base}/' or '{base}_*/')")
            return False

        if verbose:
            print(f"  OK: {base}-libs verified")
    return True


def _split_esp32_arduino_libs(destination, extracted_dir, checksum, expected_bases=None):
    """
    Split a monolithic 'esp32-arduino-libs' extraction into per-SoC tools:
      <base>-libs/{<top-level files>,<soc dirs...>,.package_checksum}
    Then remove the original 'esp32-arduino-libs' folder.
    """
    src = os.path.join(destination, extracted_dir)
    if verbose:
        print(f"\nSplitting '{extracted_dir}' into per-SoC libs under: {destination}")

    # Collect all top-level files (no hardcoded names/extensions)
    top_files = []
    for name in os.listdir(src):
        full = os.path.join(src, name)
        if os.path.isfile(full):
            top_files.append(name)
    top_files = sorted(top_files)

    # Discover SoC directories from the extracted folder
    soc_dirs = []
    for name in os.listdir(src):
        full = os.path.join(src, name)
        if not os.path.isdir(full):
            continue
        if name == "hosted":
            continue
        soc_dirs.append(name)

    bases = sorted({_soc_base_name(s) for s in soc_dirs})
    if expected_bases:
        # Use archive-derived bases when available for completeness
        bases = expected_bases
    if verbose:
        print(f"Detected SoC folders: {', '.join(sorted(soc_dirs))}")
        print(f"Detected base SoCs: {', '.join(bases)}")
        print(f"Detected top-level files: {', '.join(top_files) if top_files else '(none)'}")

    for base in bases:
        out_dir = os.path.join(destination, f"{base}-libs")
        if verbose:
            print(f"\nCreating {out_dir}")
        if os.path.isdir(out_dir):
            shutil.rmtree(out_dir, ignore_errors=True)
        mkdir_p(out_dir)

        # Copy all top-level files
        if verbose:
            print("  Copying top-level files")
        for name in top_files:
            shutil.copy2(os.path.join(src, name), out_dir)

        # Move SoC folders that belong to this base (base or base_*)
        # (much faster than copytree for large directories; safe because we delete the monolithic folder afterwards)
        copied = []
        for soc in soc_dirs:
            if soc == base or soc.startswith(base + "_"):
                if verbose:
                    print(f"  Moving folder: {soc}/")
                src_soc = os.path.join(src, soc)
                dst_soc = os.path.join(out_dir, soc)
                if os.path.exists(src_soc):
                    shutil.move(src_soc, dst_soc)
                copied.append(soc)
        if verbose and not copied:
            print(f"  WARNING: No SoC folders matched base '{base}'")

        # Write checksum marker per per-SoC tool
        with open(os.path.join(out_dir, ".package_checksum"), "w") as f:
            f.write(checksum)
        if verbose:
            print("  Wrote .package_checksum")

    # Remove monolithic folder after splitting
    if verbose:
        print(f"\nRemoving monolithic folder: {src}")
    shutil.rmtree(src, ignore_errors=True)
    return True


def is_latest_version(destination, dirname, rename_to, cfile, checksum):
    current_version = None
    expected_version = None

    try:
        expected_version = checksum
        # Special-case: monolithic esp32-arduino-libs is split into per-SoC *-libs folders
        if rename_to == "esp32-arduino-libs":
            file_list = _archive_file_list(cfile)
            bases, top_files = _get_soc_info_from_archive(file_list)
            if not bases:
                return False
            if verbose:
                print(f"\nTool: {rename_to} (split mode)")
                print(f"Expected checksum: {expected_version}")
                print(f"Expected SoCs from archive: {', '.join(bases)}")
                print(f"Expected top-level files from archive: {', '.join(top_files) if top_files else '(none)'}")
            return _verify_split_libs(destination, bases, expected_version, top_files)

        with open(os.path.join(destination, rename_to, ".package_checksum"), "r") as f:
            current_version = f.read()

        if verbose:
            print(f"\nTool: {rename_to}")
            print(f"Current version: {current_version}")
            print(f"Expected version: {expected_version}")

        if current_version and current_version == expected_version:
            if verbose:
                print("Latest version already installed. Skipping extraction")
            return True

        if verbose:
            print("New version detected")

    except Exception as e:
        if verbose:
            print(f"Failed to verify version for {rename_to}: {e}")

    return False


def unpack(filename, destination, force_extract, checksum):  # noqa: C901
    sys_name = platform.system()
    dirname = ""
    cfile = None  # Compressed file
    file_is_corrupted = False
    verify_t0 = None
    extract_t0 = None
    if not force_extract:
        print(" > Verify archive... ", end="", flush=True)
        verify_t0 = time.time()

    try:
        if filename.endswith("tar.gz"):
            if tarfile.is_tarfile(filename):
                cfile = tarfile.open(filename, "r:gz")
                dirname = cfile.getnames()[0].split("/")[0]
            else:
                print("File corrupted!")
                file_is_corrupted = True
        elif filename.endswith("tar.xz"):
            if tarfile.is_tarfile(filename):
                cfile = tarfile.open(filename, "r:xz")
                dirname = cfile.getnames()[0].split("/")[0]
            else:
                print("File corrupted!")
                file_is_corrupted = True
        elif filename.endswith("zip"):
            if zipfile.is_zipfile(filename):
                cfile = zipfile.ZipFile(filename)
                dirname = cfile.namelist()[0].split("/")[0]
            else:
                print("File corrupted!")
                file_is_corrupted = True
        else:
            raise NotImplementedError("Unsupported archive type")
    except EOFError:
        print("File corrupted or incomplete!")
        cfile = None
        file_is_corrupted = True
    except ValueError as e:
        print(f"Security validation failed: {e}")
        cfile = None
        file_is_corrupted = True

    if file_is_corrupted:
        corrupted_filename = filename + ".corrupted"
        os.rename(filename, corrupted_filename)
        if verbose:
            print(f"Renaming corrupted archive to {corrupted_filename}")
        return False

    # A little trick to rename tool directories so they don't contain version number
    rename_to = re.match(r"^([a-z][^\-]*\-*)+", dirname).group(0).strip("-")
    if rename_to == dirname and dirname.startswith("esp32-arduino-libs-"):
        rename_to = "esp32-arduino-libs"
    elif rename_to == dirname and dirname.startswith("esptool-"):
        rename_to = "esptool"

    if not force_extract:
        latest = is_latest_version(destination, dirname, rename_to, cfile, checksum)
        if verify_t0 is not None:
            print(f" Verified in {format_time(time.time() - verify_t0)}")

        if latest:
            # For split libs, is_latest_version() already verifies the split folder structure
            if rename_to == "esp32-arduino-libs":
                print(" Files ok. Skipping Extraction")
                return True
            else:
                if verify_files(filename, destination, rename_to):
                    print(" Files ok. Skipping Extraction")
                    return True
        print(" Extracting archive...")
    else:
        print(" Forcing extraction")
    extract_t0 = time.time()

    if os.path.isdir(os.path.join(destination, rename_to)):
        print("Removing existing {0} ...".format(rename_to))
        shutil.rmtree(os.path.join(destination, rename_to), ignore_errors=True)

    if filename.endswith("tar.gz"):
        if not cfile:
            cfile = tarfile.open(filename, "r:gz")
        safe_tar_extract(cfile, destination)
    elif filename.endswith("tar.xz"):
        if not cfile:
            cfile = tarfile.open(filename, "r:xz")
        safe_tar_extract(cfile, destination)
    elif filename.endswith("zip"):
        if not cfile:
            cfile = zipfile.ZipFile(filename)
        safe_zip_extract(cfile, destination)
    else:
        raise NotImplementedError("Unsupported archive type")

    if rename_to != dirname:
        print("Renaming {0} to {1} ...".format(dirname, rename_to))
        shutil.move(dirname, rename_to)

    # Special-case: split monolithic esp32-arduino-libs into per-SoC *-libs folders
    if rename_to == "esp32-arduino-libs":
        bases, _ = _get_soc_info_from_archive(_archive_file_list(cfile))
        if _split_esp32_arduino_libs(destination, rename_to, checksum, expected_bases=bases):
            if extract_t0 is not None:
                print(f" Extraction completed in {format_time(time.time() - extract_t0)}")
            print(" Files extracted and split successfully.")
            return True
        print(" Failed to split esp32-arduino-libs.")
        return False

    # Add execute permission to esptool on non-Windows platforms
    if rename_to.startswith("esptool") and "CYGWIN_NT" not in sys_name and "Windows" not in sys_name:
        st = os.stat(os.path.join(destination, rename_to, "esptool"))
        os.chmod(os.path.join(destination, rename_to, "esptool"), st.st_mode | 0o111)

    with open(os.path.join(destination, rename_to, ".package_checksum"), "w") as f:
        f.write(checksum)

    if verify_files(filename, destination, rename_to):
        if extract_t0 is not None:
            print(f" Extraction completed in {format_time(time.time() - extract_t0)}")
        print(" Files extracted successfully.")
        return True
    else:
        if extract_t0 is not None:
            print(f" Extraction completed in {format_time(time.time() - extract_t0)}")
        print(" Failed to extract files.")
        return False


def download_file_with_progress(url, filename, start_time):
    import ssl
    import contextlib

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    with contextlib.closing(urlopen(url, context=ctx)) as fp:
        total_size = int(fp.getheader("Content-Length", fp.getheader("Content-length", "0")))
        block_count = 0
        block_size = 1024 * 8
        block = fp.read(block_size)
        if block:
            with open(filename, "wb") as out_file:
                out_file.write(block)
                block_count += 1
                report_progress(block_count, block_size, total_size, start_time)
                while True:
                    block = fp.read(block_size)
                    if not block:
                        break
                    out_file.write(block)
                    block_count += 1
                    report_progress(block_count, block_size, total_size, start_time)
        else:
            raise Exception("Non-existing file or connection error")


def download_file(url, filename):
    import ssl
    import contextlib

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    with contextlib.closing(urlopen(url, context=ctx)) as fp:
        block_size = 1024 * 8
        block = fp.read(block_size)
        if block:
            with open(filename, "wb") as out_file:
                out_file.write(block)
                while True:
                    block = fp.read(block_size)
                    if not block:
                        break
                    out_file.write(block)
        else:
            raise Exception("Non-existing file or connection error")


def get_tool(tool, force_download, force_extract):
    sys_name = platform.system()
    archive_name = tool["archiveFileName"]
    checksum = tool["checksum"][8:]
    local_path = dist_dir + archive_name
    url = tool["url"]
    start_time = time.time()
    print("")
    if not os.path.isfile(local_path) or force_download:
        if verbose:
            print("Downloading '" + archive_name + "' to '" + local_path + "'")
        else:
            print("Downloading '" + archive_name + "' ...")
        sys.stdout.flush()
        if "CYGWIN_NT" in sys_name:
            import ssl

            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            urlretrieve(url, local_path, report_progress, context=ctx)
        elif "Windows" in sys_name:
            r = requests.get(url)
            with open(local_path, "wb") as f:
                f.write(r.content)
        else:
            is_ci = os.environ.get("GITHUB_WORKSPACE")
            if is_ci:
                download_file(url, local_path)
            else:
                try:
                    urlretrieve(url, local_path, report_progress)
                except:  # noqa: E722
                    download_file_with_progress(url, local_path, start_time)
                sys.stdout.write(" - Done\n")
                sys.stdout.flush()
    else:
        print("Tool {0} already downloaded".format(archive_name))
        sys.stdout.flush()

    # Time checksum verification (can be significant for large archives)
    checksum_t0 = time.time()
    if sha256sum(local_path) != checksum:
        if verbose:
            print(f"Checksum calculation completed in {format_time(time.time() - checksum_t0)}")
        print("Checksum mismatch for {0}".format(archive_name))
        return False
    if verbose:
        print(f"Checksum verified in {format_time(time.time() - checksum_t0)}")

    return unpack(local_path, ".", force_extract, checksum)


def load_tools_list(filename, platform):
    with open(filename, "r") as f:
        tools_info = json.load(f)["packages"][0]["tools"]
    tools_to_download = []
    for t in tools_info:
        if platform == "x86_64-mingw32":
            if "i686-mingw32" not in [p["host"] for p in t["systems"]]:
                raise Exception("Windows x64 requires both i686-mingw32 and x86_64-mingw32 tools")

        tool_platform = [p for p in t["systems"] if p["host"] == platform]
        if len(tool_platform) == 0:
            # Fallback to x86 on Apple ARM
            if platform == "arm64-apple-darwin":
                tool_platform = [p for p in t["systems"] if p["host"] == "x86_64-apple-darwin"]
                if len(tool_platform) == 0:
                    continue
            # Fallback to 32bit on 64bit x86 Windows
            elif platform == "x86_64-mingw32":
                tool_platform = [p for p in t["systems"] if p["host"] == "i686-mingw32"]
                if len(tool_platform) == 0:
                    continue
            else:
                if verbose:
                    print(f"Tool {t['name']} is not available for platform {platform}")
                continue
        tools_to_download.append(tool_platform[0])
    return tools_to_download


def identify_platform():
    arduino_platform_names = {
        "Darwin": {32: "i386-apple-darwin", 64: "x86_64-apple-darwin"},
        "DarwinARM": {32: "arm64-apple-darwin", 64: "arm64-apple-darwin"},
        "Linux": {32: "i686-pc-linux-gnu", 64: "x86_64-pc-linux-gnu"},
        "LinuxARM": {32: "arm-linux-gnueabihf", 64: "aarch64-linux-gnu"},
        "Windows": {32: "i686-mingw32", 64: "x86_64-mingw32"},
    }
    bits = 32
    if sys.maxsize > 2**32:
        bits = 64
    sys_name = platform.system()
    sys_platform = platform.platform()
    if "Darwin" in sys_name and (sys_platform.find("arm") > 0 or sys_platform.find("arm64") > 0):
        sys_name = "DarwinARM"
    if "Linux" in sys_name and (sys_platform.find("arm") > 0 or sys_platform.find("aarch64") > 0):
        sys_name = "LinuxARM"
    if "CYGWIN_NT" in sys_name:
        sys_name = "Windows"
    print("System: %s, Bits: %d, Info: %s" % (sys_name, bits, sys_platform))
    return arduino_platform_names[sys_name][bits]


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Download and extract tools")

    parser.add_argument("-v", "--verbose", action="store_true", required=False, help="Print verbose output")

    parser.add_argument("-d", "--force_download", action="store_true", required=False, help="Force download of tools")

    parser.add_argument("-e", "--force_extract", action="store_true", required=False, help="Force extraction of tools")

    parser.add_argument(
        "-f", "--force_all", action="store_true", required=False, help="Force download and extraction of tools"
    )

    parser.add_argument("-t", "--test", action="store_true", required=False, help=argparse.SUPPRESS)

    args = parser.parse_args()

    verbose = args.verbose
    force_download = args.force_download
    force_extract = args.force_extract
    force_all = args.force_all
    is_test = args.test

    # Set current directory to the script location
    if getattr(sys, "frozen", False):
        os.chdir(os.path.dirname(sys.executable))
    else:
        os.chdir(os.path.dirname(os.path.abspath(__file__)))

    if is_test and (force_download or force_extract or force_all):
        print("Cannot combine test (-t) and forced execution (-d | -e | -f)")
        parser.print_help(sys.stderr)
        sys.exit(1)

    if is_test:
        print("Test run!")

    if force_all:
        force_download = True
        force_extract = True

    identified_platform = identify_platform()
    print("Platform: {0}".format(identified_platform))
    tools_to_download = load_tools_list(
        current_dir + "/../package/package_esp32_index.template.json", identified_platform
    )
    mkdir_p(dist_dir)

    print("\nDownloading and extracting tools...")

    for tool in tools_to_download:
        if is_test:
            print("Would install: {0}".format(tool["archiveFileName"]))
        else:
            if not get_tool(tool, force_download, force_extract):
                if verbose:
                    print(f"Tool {tool['archiveFileName']} was corrupted. Re-downloading...\n")
                if not get_tool(tool, True, force_extract):
                    print(f"Tool {tool['archiveFileName']} was corrupted, but re-downloading did not help!\n")
                    sys.exit(1)

    print("\nPlatform Tools Installed")
