import logging
import os
import ipaddress
import random
import string
from pathlib import Path

import pytest

REGEX_IPV4 = r"(\b(?:(?:25[0-5]|2[0-4]\d|1\d{2}|[1-9]?\d)\.){3}(?:25[0-5]|2[0-4]\d|1\d{2}|[1-9]?\d)\b)"


def _patch_wokwi_chip_upload():
    """Patch Wokwi._setup_simulation to upload custom chip files.

    pytest-embedded-wokwi uploads the diagram, ELF, and firmware but does not
    upload custom chip files defined in wokwi.toml.  The Wokwi server requires:
      1. The chip WASM binary uploaded (e.g. "echo.chip.wasm").
      2. The chip JSON descriptor uploaded (e.g. "echo.chip.json").
      3. The JSON descriptor file names passed in start_simulation(chips=[...]).
    Without these uploads the server cannot find the chip and the simulation
    runs without it, so any test relying on a custom chip will fail silently.

    This patch wraps the original _setup_simulation to read wokwi.toml from the
    same directory as the diagram file, upload both files for every [[chip]]
    entry, and inject the JSON descriptor names into the chips list passed to
    start_simulation.
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
        # Collect chip files from wokwi.toml before the connection is opened.
        # Each entry yields a (wasm_path, json_path) pair.  Both files must be
        # uploaded; the Wokwi server expects the JSON descriptor name in the
        # chips list and resolves the WASM binary by convention from the same
        # upload namespace.
        chip_pairs = []
        toml_dir = Path(diagram).resolve().parent
        toml_path = toml_dir / "wokwi.toml"
        if toml_path.exists():
            with open(toml_path, "rb") as f:
                toml_data = tomllib.load(f)
            for chip in toml_data.get("chip", []):
                binary = chip.get("binary", "")
                if not binary:
                    logging.warning("wokwi.toml chip entry is missing the 'binary' field; skipping")
                    continue
                wasm_path = toml_dir / binary
                # JSON descriptor lives beside the WASM with the same base name.
                json_path = wasm_path.parent / (wasm_path.name.removesuffix(".wasm") + ".json")
                if not wasm_path.exists():
                    logging.warning("Custom chip binary not found: %s", wasm_path)
                    continue
                if not json_path.exists():
                    logging.warning("Custom chip descriptor not found: %s", json_path)
                    continue
                chip_pairs.append((wasm_path, json_path))

        # Wrap start_simulation on the client instance so that chip files are
        # uploaded and injected into the chips list after the connection is open.
        orig_start = self.client.start_simulation

        def _start_with_chips(**kwargs):
            chip_descriptors = []
            for wasm_path, json_path in chip_pairs:
                self.client.upload_file(wasm_path.name, wasm_path)
                logging.info("Uploaded custom chip binary: %s", wasm_path.name)
                chip_descriptors.append(self.client.upload_file(json_path.name, json_path))
                logging.info("Uploaded custom chip descriptor: %s", json_path.name)
            kwargs["chips"] = kwargs.get("chips", []) + chip_descriptors
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
