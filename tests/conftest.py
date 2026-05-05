import logging
import os
import ipaddress
import random
import string
from pathlib import Path

import pytest

REGEX_IPV4 = r"(\b(?:(?:25[0-5]|2[0-4]\d|1\d{2}|[1-9]?\d)\.){3}(?:25[0-5]|2[0-4]\d|1\d{2}|[1-9]?\d)\b)"


def _patch_wokwi_chip_upload():
    """Patch Wokwi._setup_simulation to upload custom chip WASM files.

    pytest-embedded-wokwi uploads the diagram, ELF, and firmware but does not
    upload custom chip WASM files defined in wokwi.toml, so any test that uses
    a custom chip fails silently (MISO returns junk).  This patch wraps the
    original _setup_simulation to also read wokwi.toml from the same directory
    as the diagram file, upload every chip binary listed there, and pass the
    resulting file names to start_simulation(chips=[...]).
    """
    try:
        import tomllib
    except ImportError:
        return  # Python < 3.11 without tomli installed; skip patch

    try:
        from pytest_embedded_wokwi.wokwi import Wokwi
    except ImportError:
        return  # pytest-embedded-wokwi not installed; skip patch

    original_setup = Wokwi._setup_simulation

    def _setup_simulation_with_chips(self, diagram: str, firmware_path: str, elf_path: str) -> None:
        # Collect chip binaries from wokwi.toml before the connection is opened.
        chip_files = []
        toml_dir = os.path.dirname(os.path.abspath(diagram))
        toml_path = os.path.join(toml_dir, "wokwi.toml")
        if os.path.exists(toml_path):
            with open(toml_path, "rb") as f:
                toml_data = tomllib.load(f)
            for chip in toml_data.get("chip", []):
                binary = chip.get("binary", "")
                if not binary:
                    logging.warning("wokwi.toml chip entry is missing the 'binary' field; skipping")
                    continue
                chip_path = os.path.join(toml_dir, binary)
                if os.path.exists(chip_path):
                    chip_files.append((os.path.basename(chip_path), Path(chip_path)))
                else:
                    logging.warning("Custom chip binary not found: %s", chip_path)

        # Wrap start_simulation on the client instance so that chip WASMs are
        # uploaded and injected into the chips list after the connection is open.
        orig_start = self.client.start_simulation

        def _start_with_chips(**kwargs):
            chips = []
            for chip_name, chip_path in chip_files:
                chips.append(self.client.upload_file(chip_name, chip_path))
                logging.info("Uploaded custom chip: %s", chip_name)
            kwargs["chips"] = chips
            orig_start(**kwargs)

        self.client.start_simulation = _start_with_chips
        try:
            original_setup(self, diagram, firmware_path, elf_path)
        finally:
            self.client.start_simulation = orig_start

    Wokwi._setup_simulation = _setup_simulation_with_chips


_patch_wokwi_chip_upload()


# Pytest arguments
def pytest_addoption(parser):
    parser.addoption("--wifi-password", action="store", default=None, help="Wi-Fi password.")
    parser.addoption("--wifi-ssid", action="store", default=None, help="Wi-Fi SSID.")


# Fixtures
@pytest.fixture(scope="session")
def wifi_ssid(request):
    return request.config.getoption("--wifi-ssid")


@pytest.fixture(scope="session")
def wifi_pass(request):
    return request.config.getoption("--wifi-password")


@pytest.fixture(scope="session")
def ci_job_id(request):
    return os.environ.get("CI_JOB_ID")


# Helper functions
def is_valid_ipv4(ip):
    # Check if the IP address is a valid IPv4 address
    try:
        ipaddress.IPv4Address(ip)
        return True
    except ipaddress.AddressValueError:
        return False


def rand_str4():
    # Generate a random string of 4 characters
    return "".join(random.choices(string.ascii_letters + string.digits, k=4))
