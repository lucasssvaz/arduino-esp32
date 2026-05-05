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
    a custom chip fails silently (MISO returns junk).  This patch reads
    wokwi.toml from the same directory as the diagram file, uploads every chip
    binary listed there, and passes the resulting file names to
    start_simulation(chips=[...]).
    """
    try:
        import tomllib
    except ImportError:
        return  # Python < 3.11 without tomli installed; skip patch

    try:
        from pytest_embedded_wokwi.wokwi import Wokwi
    except ImportError:
        return  # pytest-embedded-wokwi not installed; skip patch

    def _setup_simulation_with_chips(self, diagram: str, firmware_path: str, elf_path: str) -> None:
        hello = self.client.connect()
        logging.info("Connected to Wokwi Simulator, server version: %s", hello.get("version", "unknown"))

        self.client.upload_file("diagram.json", Path(diagram))
        self.client.upload_file("pytest.elf", Path(elf_path))

        if firmware_path.endswith("flasher_args.json"):
            firmware = self.client.upload_idf_firmware(firmware_path)
            kwargs = {"firmware": firmware.firmware, "elf": "pytest.elf", "flash_size": firmware.flash_size}
        else:
            firmware = self.client.upload_file("pytest.bin", Path(firmware_path))
            kwargs = {"firmware": firmware, "elf": "pytest.elf"}

        # Upload custom chips listed in wokwi.toml (same directory as diagram).
        chips = []
        toml_dir = os.path.dirname(os.path.abspath(diagram))
        toml_path = os.path.join(toml_dir, "wokwi.toml")
        if os.path.exists(toml_path):
            with open(toml_path, "rb") as f:
                toml_data = tomllib.load(f)
            for chip in toml_data.get("chip", []):
                binary = chip.get("binary", "")
                chip_path = os.path.join(toml_dir, binary)
                if os.path.exists(chip_path):
                    chip_name = os.path.basename(chip_path)
                    chips.append(self.client.upload_file(chip_name, Path(chip_path)))
                    logging.info("Uploaded custom chip: %s", chip_name)
        kwargs["chips"] = chips

        logging.info("Uploaded diagram and firmware to Wokwi. Starting simulation...")
        self.client.start_simulation(**kwargs)

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
