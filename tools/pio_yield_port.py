"""PlatformIO upload hook that cooperatively yields the YouAndEye serial port."""

Import("env")

import importlib.util
import os
import sys
from pathlib import Path


PROJECT_ROOT = Path(env["PROJECT_DIR"]).resolve().parent
YIELD_PATH = Path(
    os.environ.get("YOUANDEYE_YIELD_PATH", PROJECT_ROOT / ".youandeye" / "port.yield")
).resolve()
LEASE_MODULE_PATH = PROJECT_ROOT / "host" / "youandeye" / "port_lease.py"
SPEC = importlib.util.spec_from_file_location("youandeye_port_lease", LEASE_MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load port lease helper from {LEASE_MODULE_PATH}")
PORT_LEASE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = PORT_LEASE
try:
    SPEC.loader.exec_module(PORT_LEASE)
except Exception:
    sys.modules.pop(SPEC.name, None)
    raise
REQUEST_ID = None


def acquire_yield(*_args, **_kwargs):
    global REQUEST_ID
    REQUEST_ID = PORT_LEASE.write_yield_request(
        YIELD_PATH, duration_s=600.0, owner="platformio-upload"
    )
    try:
        PORT_LEASE.wait_until_port_released(YIELD_PATH, timeout_s=5.0)
    except Exception:
        PORT_LEASE.clear_yield_request(YIELD_PATH, REQUEST_ID)
        REQUEST_ID = None
        raise


def release_yield(*_args, **_kwargs):
    global REQUEST_ID
    PORT_LEASE.clear_yield_request(YIELD_PATH, REQUEST_ID)
    REQUEST_ID = None


env.AddPreAction("upload", acquire_yield)
env.AddPostAction("upload", release_yield)
