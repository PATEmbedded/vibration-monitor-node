"""System tests: the firmware ELF runs on an emulated STM32F407 in Renode, and a Modbus RTU client
talks to it over the emulated USART2, which Renode exposes on a TCP port.

    RENODE=/path/to/renode VMN_ELF=build-fw/vmn-node.elf pytest test/renode
"""
from __future__ import annotations

import os
import socket
import subprocess
import time
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
RENODE = os.environ.get("RENODE", "renode")
ELF = Path(os.environ.get("VMN_ELF", ROOT / "build-fw" / "vmn-node.elf")).resolve()
PORT = int(os.environ.get("VMN_PORT", "4321"))
LOG = Path(os.environ.get("VMN_RENODE_LOG", ROOT / "build-fw" / "renode.log"))

# Register addresses, as in app/include/vmn/node.h
HR_ADDRESS, HR_SHAFT, HR_SOURCE, HR_INTERVAL = range(4)
IR_VERSION, IR_FRAMES, IR_CLASS, IR_CONFIDENCE, IR_RMS = range(5)
IR_CRC_ERRORS = 12


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


class ModbusError(Exception):
    def __init__(self, function: int, code: int):
        super().__init__(f"exception {code} for function 0x{function:02X}")
        self.function = function
        self.code = code


class Client:
    """Minimal Modbus RTU client over the TCP bridge to the emulated UART."""

    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.address = 1

    def _drain(self):
        self.sock.settimeout(0.01)
        try:
            while self.sock.recv(1024):
                pass
        except (socket.timeout, BlockingIOError):
            pass

    def _recv_exact(self, n: int, deadline: float) -> bytes:
        data = b""
        while len(data) < n:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"got {len(data)} of {n} bytes")
            self.sock.settimeout(remaining)
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise ConnectionError("Renode closed the socket")
            data += chunk
        return data

    def send_raw(self, frame: bytes):
        self._drain()
        self.sock.sendall(frame)

    def request(self, pdu: bytes, address: int | None = None, timeout: float = 3.0) -> bytes | None:
        address = self.address if address is None else address
        frame = bytes([address]) + pdu
        self.send_raw(frame + crc16(frame).to_bytes(2, "little"))
        if address == 0:
            return None
        deadline = time.monotonic() + timeout
        head = self._recv_exact(2, deadline)
        if head[1] & 0x80:
            rest = self._recv_exact(3, deadline)
        elif head[1] in (0x03, 0x04):
            count = self._recv_exact(1, deadline)
            rest = count + self._recv_exact(count[0] + 2, deadline)
        else:
            rest = self._recv_exact(6, deadline)
        resp = head + rest
        assert crc16(resp) == 0, f"bad CRC in reply {resp.hex()}"
        assert resp[0] == address, f"reply from address {resp[0]}"
        if resp[1] & 0x80:
            raise ModbusError(resp[1] & 0x7F, resp[2])
        return resp[2:-2]

    def read(self, function: int, start: int, count: int = 1) -> list[int]:
        data = self.request(bytes([function]) + start.to_bytes(2, "big") + count.to_bytes(2, "big"))
        return [int.from_bytes(data[1 + 2 * i : 3 + 2 * i], "big") for i in range(count)]

    def holding(self, start: int, count: int = 1) -> list[int]:
        return self.read(0x03, start, count)

    def inputs(self, start: int, count: int = 1) -> list[int]:
        return self.read(0x04, start, count)

    def write(self, reg: int, value: int, address: int | None = None):
        return self.request(bytes([0x06]) + reg.to_bytes(2, "big") + value.to_bytes(2, "big"), address)

    def write_many(self, start: int, values: list[int], address: int | None = None):
        payload = b"".join(v.to_bytes(2, "big") for v in values)
        return self.request(bytes([0x10]) + start.to_bytes(2, "big") + len(values).to_bytes(2, "big")
                            + bytes([len(payload)]) + payload, address)

    def wait_frames(self, count: int, timeout: float = 30.0) -> int:
        """Waits until the node has finished `count` more analyses; returns the new counter."""
        start = self.inputs(IR_FRAMES)[0]
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            now = self.inputs(IR_FRAMES)[0]
            if (now - start) % 65536 >= count:
                return now
            time.sleep(0.05)
        raise TimeoutError("the node stopped analysing")


@pytest.fixture(scope="module")
def node():
    if not ELF.exists():
        # In CI a missing firmware must fail the job, not skip it into a green result.
        (pytest.fail if os.environ.get("CI") else pytest.skip)(f"{ELF} not built")
    LOG.parent.mkdir(parents=True, exist_ok=True)
    script = (ROOT / "test" / "renode" / "vmn-node.resc").as_posix()
    commands = f"$elf=@{ELF.as_posix()}; $port={PORT}; include @{script}; start"
    with open(LOG, "w") as log:
        proc = subprocess.Popen([RENODE, "--disable-gui", "-P", "-1", "--plain", "-e", commands],
                                stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
    try:
        deadline = time.monotonic() + 120
        sock = None
        while time.monotonic() < deadline and sock is None:
            if proc.poll() is not None:
                pytest.fail(f"Renode exited early; see {LOG}")
            try:
                sock = socket.create_connection(("127.0.0.1", PORT), timeout=1)
            except OSError:
                time.sleep(0.5)
        if sock is None:
            pytest.fail(f"Renode did not open port {PORT}; see {LOG}")
        client = Client(sock)
        # The firmware answers once the scheduler runs.
        while True:
            try:
                client.inputs(IR_VERSION)
                break
            except (TimeoutError, AssertionError):
                if time.monotonic() > deadline:
                    pytest.fail(f"the firmware never answered; see {LOG}")
        yield client
        sock.close()
    finally:
        proc.kill()
        proc.wait(timeout=30)


@pytest.fixture
def client(node):
    """Each test starts from the default settings and restores them afterwards. The restore is a
    broadcast, so it works whatever address the test left the node at."""
    yield node
    node.write_many(HR_ADDRESS, [1, 250, 0, 500], address=0)
    node.address = 1
    time.sleep(0.2)
    assert node.holding(HR_ADDRESS, 4) == [1, 250, 0, 500]


def test_reports_firmware_version(client):
    assert client.inputs(IR_VERSION) == [0x0100]


def test_default_settings(client):
    assert client.holding(HR_ADDRESS, 4) == [1, 250, 0, 500]


@pytest.mark.parametrize("condition,name", list(enumerate(["healthy", "imbalance", "misalignment", "bearing fault"])))
def test_classifies_each_condition(client, condition, name):
    client.write(HR_SOURCE, condition)
    client.wait_frames(2)
    cls, confidence = client.inputs(IR_CLASS, 2)
    assert cls == condition, f"expected {name}"
    assert confidence > 500


def test_reports_plausible_levels(client):
    client.wait_frames(1)
    rms, peak, crest, kurtosis = client.inputs(IR_RMS, 4)
    assert 50 < rms < 120          # 0.080 expected for the default healthy signal
    assert peak > rms
    assert 130 < crest < 500       # x100
    assert 100 < kurtosis < 400    # x100


def test_rejects_out_of_range_value(client):
    with pytest.raises(ModbusError) as err:
        client.write(HR_SHAFT, 2000)
    assert err.value.code == 3
    assert client.holding(HR_SHAFT) == [250]


def test_rejects_unknown_register_and_function(client):
    with pytest.raises(ModbusError) as err:
        client.inputs(100)
    assert err.value.code == 2
    with pytest.raises(ModbusError) as err:
        client.request(bytes([0x2B, 0x0E, 0x01, 0x00]))
    assert err.value.code == 1


def test_drops_frames_with_a_bad_crc(client):
    before = client.inputs(IR_CRC_ERRORS)[0]
    client.send_raw(bytes([0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0xDE, 0xAD]))
    with pytest.raises(TimeoutError):
        client._recv_exact(1, time.monotonic() + 0.5)
    assert client.inputs(IR_CRC_ERRORS)[0] == before + 1


def test_write_multiple_and_read_back(client):
    client.write_many(HR_SHAFT, [300, 1, 200])
    assert client.holding(HR_SHAFT, 3) == [300, 1, 200]


def test_broadcast_write_is_applied_without_reply(client):
    assert client.write(HR_INTERVAL, 400, address=0) is None
    time.sleep(0.2)
    assert client.holding(HR_INTERVAL) == [400]


def test_changes_slave_address_after_replying(client):
    client.write(HR_ADDRESS, 17)  # this reply still comes from address 1
    client.address = 17
    assert client.holding(HR_ADDRESS) == [17]
    with pytest.raises(TimeoutError):
        client.request(bytes([0x03, 0, 0, 0, 1]), address=1, timeout=0.5)


def test_keeps_running_past_the_watchdog_timeout(client):
    # The watchdog fires after about 4 s if the supervisor stops feeding it. A reset would restart
    # the frame counter, so the counter must only grow: 14 frames is 7 s at the 500 ms interval.
    first = last = client.inputs(IR_FRAMES)[0]
    deadline = time.monotonic() + 60
    while last - first < 14:
        assert time.monotonic() < deadline, "the node stopped analysing"
        time.sleep(0.2)
        now = client.inputs(IR_FRAMES)[0]
        assert now >= last, "the frame counter went back: the node was reset"
        last = now
