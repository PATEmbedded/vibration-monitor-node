"""Compare the C core (through vmn_cli) with the Python reference in tools/vmnlib.py.

Set VMN_CLI to the vmn_cli executable, or build it in build/ or build-host/ first.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import export_header  # noqa: E402
import vmnlib  # noqa: E402


def find_cli() -> str:
    if os.environ.get("VMN_CLI"):
        return os.environ["VMN_CLI"]
    for build in ("build", "build-host"):
        for name in ("vmn_cli", "vmn_cli.exe"):
            candidate = ROOT / build / name
            if candidate.exists():
                return str(candidate)
    found = shutil.which("vmn_cli")
    if found:
        return found
    # In CI a missing tool must fail the job, not skip it into a green result.
    (pytest.fail if os.environ.get("CI") else pytest.skip)("vmn_cli not built; set VMN_CLI")


@pytest.fixture(scope="module")
def cli() -> str:
    return find_cli()


@pytest.fixture(scope="module")
def model() -> vmnlib.Int8Model:
    return vmnlib.Int8Model.load()


def run(cli: str, *args: str, stdin: str | None = None) -> str:
    return subprocess.run([cli, *args], input=stdin, capture_output=True, text=True, check=True).stdout


CASES = [(c, shaft, seed) for c in range(4) for shaft in (12.5, 25.0, 37.5) for seed in (1, 4242, 987654321)]


@pytest.mark.parametrize("condition,shaft_hz,seed", CASES)
def test_features_match_reference(cli, condition, shaft_hz, seed):
    c_values = np.array([float(v) for v in run(cli, "features", str(condition), str(shaft_hz), str(seed)).split()])
    params = vmnlib.default_params(condition, shaft_hz)
    frame = vmnlib.generate([params], [seed])
    py_values = vmnlib.features(frame, [shaft_hz])[0]
    # float32 in C against float64 in numpy: the features are logs and ratios, so this is tight.
    np.testing.assert_allclose(c_values, py_values, rtol=1e-3, atol=2e-3)


def test_int8_scores_are_bit_exact(cli, model):
    rng = np.random.default_rng(11)
    vectors = rng.integers(-127, 128, size=(400, model.inputs))
    # Add real inputs: quantised features of simulated frames of every condition.
    params = [vmnlib.default_params(c, 25.0) for c in range(4) for _ in range(5)]
    feats = vmnlib.features(vmnlib.generate(params, range(1, len(params) + 1)), [25.0] * len(params))
    vectors = np.vstack([vectors, model.quantize(feats)])

    stdin = "\n".join(" ".join(str(int(v)) for v in row) for row in vectors) + "\n"
    c_logits = np.array([[int(v) for v in line.split()] for line in run(cli, "logits", stdin=stdin).splitlines()])
    np.testing.assert_array_equal(c_logits, model.logits(vectors))


@pytest.mark.parametrize("condition", range(4))
def test_firmware_conditions_are_classified(cli, condition):
    for seed in (3, 17, 2024):
        cls = int(run(cli, "classify", str(condition), "25", str(seed)).split()[0])
        assert cls == condition, vmnlib.CLASS_NAMES[condition]


def test_header_matches_model():
    text = export_header.render(vmnlib.Int8Model.load().data)
    assert (ROOT / "model" / "model_data.h").read_text(encoding="utf-8") == text


def test_int8_accuracy_on_fresh_synthetic_data(model):
    rng = np.random.default_rng(31337)
    params, labels = [], []
    for c in range(4):
        for _ in range(100):
            params.append(vmnlib.random_params(c, rng))
            labels.append(c)
    feats = vmnlib.features(vmnlib.generate(params, rng.integers(1, 2**32, size=len(params), dtype=np.uint64)),
                            [p.shaft_hz for p in params])
    assert (model.predict(feats) == np.array(labels)).mean() >= 0.95
