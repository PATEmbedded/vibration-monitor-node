"""Python reference for the node: signal model, features and the int8 classifier.

It follows core/src step by step where results must agree: the random sequence, the feature
definitions and the integer inference. The tests compare this module with the C code.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, replace
from pathlib import Path

import numpy as np

SAMPLE_RATE_HZ = 10240.0
FRAME_LEN = 1024
BAND_EDGES_HZ = (0.0, 50.0, 100.0, 200.0, 400.0, 800.0, 1600.0, 3200.0, 5120.0)
CLASS_NAMES = ("healthy", "imbalance", "misalignment", "bearing fault")
FEATURE_NAMES = (
    "log_rms", "log_peak", "crest", "log_kurtosis",
    "rel_1x", "rel_2x", "rel_3x",
    *(f"band_{lo:g}_{hi:g}" for lo, hi in zip(BAND_EDGES_HZ[:-1], BAND_EDGES_HZ[1:])),
)
TWO_PI = 2.0 * np.pi
ROOT = Path(__file__).resolve().parent.parent
MODEL_JSON = ROOT / "model" / "model.json"


# --- random numbers (core/include/vmn/prng.h) --------------------------------------------------

def prng_seed(seed: int) -> int:
    seed &= 0xFFFFFFFF
    return seed if seed else 0x9E3779B9


class Xorshift32:
    """One xorshift32 stream per frame, advanced together with numpy."""

    def __init__(self, seeds):
        self.state = np.array([prng_seed(int(s)) for s in seeds], dtype=np.uint32)

    def next(self) -> np.ndarray:
        x = self.state
        x = x ^ (x << np.uint32(13))
        x = x ^ (x >> np.uint32(17))
        x = x ^ (x << np.uint32(5))
        self.state = x
        return x

    def uniform(self) -> np.ndarray:
        return (self.next() >> np.uint32(8)).astype(np.float64) / 16777216.0


# --- signal model (core/src/signal_sim.c) -------------------------------------------------------

@dataclass(frozen=True)
class SimParams:
    shaft_hz: float
    harmonic_amp: tuple[float, float, float] = (0.10, 0.03, 0.01)
    noise_rms: float = 0.03
    impulse_amp: float = 0.0
    impulse_ratio: float = 3.58
    resonance_hz: float = 2800.0
    decay_s: float = 0.002


def default_params(condition: int, shaft_hz: float) -> SimParams:
    """Same values as vmn_sim_default_params."""
    p = SimParams(shaft_hz=shaft_hz)
    if condition == 1:
        p = replace(p, harmonic_amp=(0.80, 0.05, 0.02))
    elif condition == 2:
        p = replace(p, harmonic_amp=(0.20, 0.50, 0.25))
    elif condition == 3:
        p = replace(p, impulse_amp=1.0)
    return p


def random_params(condition: int, rng: np.random.Generator) -> SimParams:
    """Parameter spread for training and evaluation: wider than the firmware defaults."""
    u = rng.uniform
    shaft = u(10.0, 60.0)
    healthy = (u(0.05, 0.20), u(0.01, 0.06), u(0.0, 0.03))
    noise = u(0.01, 0.08)
    if condition == 0:
        return SimParams(shaft, healthy, noise)
    if condition == 1:
        return SimParams(shaft, (u(0.40, 1.50), u(0.01, 0.10), u(0.0, 0.05)), noise)
    if condition == 2:
        return SimParams(shaft, (u(0.05, 0.40), u(0.25, 1.00), u(0.08, 0.50)), noise)
    return SimParams(shaft, healthy, noise, impulse_amp=u(0.3, 2.0), impulse_ratio=u(3.0, 5.5),
                     resonance_hz=u(1500.0, 4000.0), decay_s=u(0.001, 0.004))


def generate(params: list[SimParams], seeds, n: int = FRAME_LEN, fs: float = SAMPLE_RATE_HZ) -> np.ndarray:
    """One frame per parameter set, drawing random numbers in the same order as the C code."""
    count = len(params)
    rng = Xorshift32(seeds)
    phase = np.stack([TWO_PI * rng.uniform() for _ in range(3)], axis=1)
    offset_draw = rng.uniform()

    shaft = np.array([p.shaft_hz for p in params])
    amp = np.array([p.harmonic_amp for p in params])
    noise_rms = np.array([p.noise_rms for p in params])
    t = np.arange(n) / fs

    x = np.zeros((count, n))
    for h in range(3):
        x += amp[:, h, None] * np.sin(TWO_PI * (h + 1) * shaft[:, None] * t[None, :] + phase[:, h, None])
    noise = np.empty((count, n))
    for i in range(n):
        total = np.zeros(count)
        for _ in range(12):
            total += rng.uniform()
        noise[:, i] = total - 6.0
    x += noise_rms[:, None] * noise

    for idx, p in enumerate(params):
        rate = p.impulse_ratio * p.shaft_hz
        if p.impulse_amp <= 0.0 or rate <= 0.0 or p.decay_s <= 0.0:
            continue
        duration = n / fs
        period = 1.0 / rate
        first = offset_draw[idx] * period
        tail = int(5.0 * p.decay_s * fs) + 1
        k = 0
        while True:
            t_k = first + k * period
            if t_k >= duration:
                break
            start = int(np.ceil(t_k * fs))
            end = min(n, start + tail + 1)
            if start < end:
                dt = np.arange(start, end) / fs - t_k
                x[idx, start:end] += p.impulse_amp * np.exp(-dt / p.decay_s) * np.sin(TWO_PI * p.resonance_hz * dt)
            k += 1
    return x


# --- features (core/src/features.c) -------------------------------------------------------------

def features(x: np.ndarray, shaft_hz, fs: float = SAMPLE_RATE_HZ) -> np.ndarray:
    """Feature matrix, one row per frame, in the order of FEATURE_NAMES."""
    x = np.atleast_2d(np.asarray(x, dtype=np.float64))
    shaft_hz = np.broadcast_to(np.asarray(shaft_hz, dtype=np.float64), (x.shape[0],))
    count, n = x.shape
    half = n // 2

    c = x - x.mean(axis=1, keepdims=True)
    var = (c ** 2).mean(axis=1)
    rms = np.sqrt(var)
    peak = np.abs(c).max(axis=1)
    crest = np.where(rms > 0, peak / np.where(rms > 0, rms, 1), 0.0)
    kurt = np.where(var > 0, (c ** 4).mean(axis=1) / np.where(var > 0, var, 1) ** 2, 0.0)

    window = 0.5 - 0.5 * np.cos(TWO_PI * np.arange(n) / n)
    amp = (4.0 / n) * np.abs(np.fft.fft(c * window, axis=1)[:, : half + 1])
    bin_hz = fs / n

    harmonics = np.zeros((count, 3))
    rows = np.arange(count)
    for h in range(3):
        k0 = np.floor((h + 1) * shaft_hz / bin_hz + 0.5).astype(np.int64)
        best = np.zeros(count)
        for d in (-1, 0, 1):
            k = k0 + d
            valid = (k >= 1) & (k <= half - 1)
            vals = np.where(valid, amp[rows, np.clip(k, 0, half)], 0.0)
            best = np.maximum(best, vals)
        harmonics[:, h] = best

    freqs = np.arange(half + 1) * bin_hz
    energy = np.zeros((count, len(BAND_EDGES_HZ) - 1))
    for b in range(len(BAND_EDGES_HZ) - 1):
        lo, hi = BAND_EDGES_HZ[b], BAND_EDGES_HZ[b + 1]
        last = b == len(BAND_EDGES_HZ) - 2
        mask = (freqs >= lo) & ((freqs < hi) | (last & (freqs <= hi)))
        energy[:, b] = (amp[:, mask] ** 2).sum(axis=1)
    total = energy.sum(axis=1)

    log_rms = np.log10(rms + 1e-9)
    out = np.empty((count, len(FEATURE_NAMES)))
    out[:, 0] = log_rms
    out[:, 1] = np.log10(peak + 1e-9)
    out[:, 2] = crest
    out[:, 3] = np.log10(kurt + 1e-9)
    out[:, 4:7] = np.log10(harmonics + 1e-9) - log_rms[:, None]
    out[:, 7:15] = np.log10(energy + 1e-12) - np.log10(total + 1e-12)[:, None]
    return out


# --- int8 model (core/src/classifier.c) ---------------------------------------------------------

class Int8Model:
    def __init__(self, data: dict):
        self.data = data
        self.inputs = data["inputs"]
        self.hidden = data["hidden"]
        self.classes = len(data["classes"])
        self.mean = np.array(data["feature_mean"])
        self.std = np.array(data["feature_std"])
        self.input_scale = data["input_scale"]
        self.w1 = np.array(data["w1"], dtype=np.int64).reshape(self.hidden, self.inputs)
        self.b1 = np.array(data["b1"], dtype=np.int64)
        self.m1 = int(data["m1"])
        self.shift1 = int(data["shift1"])
        self.w2 = np.array(data["w2"], dtype=np.int64).reshape(self.classes, self.hidden)
        self.b2 = np.array(data["b2"], dtype=np.int64)
        self.output_scale = data["output_scale"]

    @classmethod
    def load(cls, path: Path = MODEL_JSON) -> "Int8Model":
        return cls(json.loads(Path(path).read_text(encoding="utf-8")))

    def quantize(self, feats: np.ndarray) -> np.ndarray:
        z = (np.atleast_2d(feats) - self.mean) / self.std
        return np.clip(np.floor(z / self.input_scale + 0.5), -127, 127).astype(np.int64)

    def logits(self, q: np.ndarray) -> np.ndarray:
        q = np.atleast_2d(np.asarray(q, dtype=np.int64))
        acc = np.maximum(q @ self.w1.T + self.b1, 0)
        hidden = (acc * self.m1 + (1 << (self.shift1 - 1))) >> self.shift1
        hidden = np.minimum(hidden, 127)
        return hidden @ self.w2.T + self.b2

    def predict(self, feats: np.ndarray) -> np.ndarray:
        return np.argmax(self.logits(self.quantize(feats)), axis=1)
