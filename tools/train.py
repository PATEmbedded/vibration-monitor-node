"""Train the fault classifier on synthetic vibration, quantise it to int8 and write model/model.json.

    python tools/train.py            # then: python tools/export_header.py

Training is plain numpy: a 15-16-4 MLP with ReLU, softmax cross-entropy and Adam. The weights are
then quantised per tensor to int8; the hidden layer is requantised with an integer multiplier and
shift, so the firmware runs the network without floating point.
"""
from __future__ import annotations

import argparse
import json
import math

import numpy as np

from vmnlib import CLASS_NAMES, FEATURE_NAMES, MODEL_JSON, Int8Model, features, generate, random_params


def make_dataset(per_class: int, seed: int):
    rng = np.random.default_rng(seed)
    params, labels = [], []
    for cls in range(len(CLASS_NAMES)):
        for _ in range(per_class):
            params.append(random_params(cls, rng))
            labels.append(cls)
    seeds = rng.integers(1, 2**32, size=len(params), dtype=np.uint64)
    x = generate(params, seeds)
    return features(x, [p.shaft_hz for p in params]), np.array(labels)


def train_mlp(x, y, hidden: int, epochs: int, seed: int):
    rng = np.random.default_rng(seed)
    n_in, n_out = x.shape[1], len(CLASS_NAMES)
    w1 = rng.normal(0.0, math.sqrt(2.0 / n_in), (hidden, n_in))
    b1 = np.zeros(hidden)
    w2 = rng.normal(0.0, math.sqrt(2.0 / hidden), (n_out, hidden))
    b2 = np.zeros(n_out)
    params = [w1, b1, w2, b2]
    m = [np.zeros_like(p) for p in params]
    v = [np.zeros_like(p) for p in params]
    lr, beta1, beta2, eps, decay = 3e-3, 0.9, 0.999, 1e-8, 1e-4
    onehot = np.eye(n_out)[y]
    step = 0
    for _ in range(epochs):
        order = rng.permutation(len(x))
        for start in range(0, len(x), 128):
            idx = order[start : start + 128]
            xb, yb = x[idx], onehot[idx]
            pre = xb @ w1.T + b1
            h = np.maximum(pre, 0.0)
            logits = h @ w2.T + b2
            logits -= logits.max(axis=1, keepdims=True)
            p = np.exp(logits)
            p /= p.sum(axis=1, keepdims=True)
            d = (p - yb) / len(idx)
            dh = d @ w2
            dh[pre <= 0.0] = 0.0
            grads = [dh.T @ xb + decay * w1, dh.sum(axis=0), d.T @ h + decay * w2, d.sum(axis=0)]
            step += 1
            for param, g, mi, vi in zip(params, grads, m, v):
                mi[:] = beta1 * mi + (1 - beta1) * g
                vi[:] = beta2 * vi + (1 - beta2) * g * g
                param -= lr * (mi / (1 - beta1**step)) / (np.sqrt(vi / (1 - beta2**step)) + eps)
    return w1, b1, w2, b2


def f32(values):
    """Round to float32 so the C header and the Python reference use the same constants."""
    return [float(np.float32(v)) for v in np.ravel(values)]


def quantize(w1, b1, w2, b2, z_train):
    input_scale = float(np.float32(np.percentile(np.abs(z_train), 99.9) / 127.0))
    q_in = np.clip(np.floor(z_train / input_scale + 0.5), -127, 127)

    s_w1 = np.abs(w1).max() / 127.0
    w1q = np.clip(np.round(w1 / s_w1), -127, 127).astype(np.int64)
    b1q = np.round(b1 / (input_scale * s_w1)).astype(np.int64)
    acc = np.maximum(q_in @ w1q.T + b1q, 0)
    s_a1 = max(np.percentile(acc * input_scale * s_w1, 99.9) / 127.0, 1e-12)

    mantissa, exponent = math.frexp(input_scale * s_w1 / s_a1)
    m1 = round(mantissa * 2**31)
    if m1 == 2**31:
        m1 //= 2
        exponent += 1
    shift1 = 31 - exponent
    if not 1 <= shift1 <= 62:
        raise SystemExit(f"requantisation shift {shift1} out of range")

    s_w2 = np.abs(w2).max() / 127.0
    w2q = np.clip(np.round(w2 / s_w2), -127, 127).astype(np.int64)
    b2q = np.round(b2 / (s_a1 * s_w2)).astype(np.int64)
    return {
        "input_scale": input_scale,
        "w1": w1q.ravel().tolist(), "b1": b1q.tolist(), "m1": int(m1), "shift1": int(shift1),
        "w2": w2q.ravel().tolist(), "b2": b2q.tolist(),
        "output_scale": float(np.float32(s_a1 * s_w2)),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--per-class", type=int, default=1500)
    ap.add_argument("--hidden", type=int, default=16)
    ap.add_argument("--epochs", type=int, default=60)
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()

    x_train, y_train = make_dataset(args.per_class, args.seed)
    x_test, y_test = make_dataset(max(args.per_class // 4, 50), args.seed + 1000)

    mean = np.array(f32(x_train.mean(axis=0)))
    std = np.array(f32(x_train.std(axis=0) + 1e-6))
    z_train = (x_train - mean) / std
    w1, b1, w2, b2 = train_mlp(z_train, y_train, args.hidden, args.epochs, args.seed)

    pre = np.maximum(((x_test - mean) / std) @ w1.T + b1, 0.0)
    float_acc = float((np.argmax(pre @ w2.T + b2, axis=1) == y_test).mean())

    data = {
        "format": 1,
        "classes": list(CLASS_NAMES),
        "features": list(FEATURE_NAMES),
        "inputs": int(x_train.shape[1]),
        "hidden": args.hidden,
        "feature_mean": f32(mean),
        "feature_std": f32(std),
        **quantize(w1, b1, w2, b2, z_train),
    }
    int8_acc = float((Int8Model(data).predict(x_test) == y_test).mean())
    data["training"] = {
        "per_class": args.per_class, "epochs": args.epochs, "seed": args.seed,
        "held_out_float_accuracy": round(float_acc, 4), "held_out_int8_accuracy": round(int8_acc, 4),
        "numpy": np.__version__,
    }
    MODEL_JSON.parent.mkdir(exist_ok=True)
    MODEL_JSON.write_text(json.dumps(data, indent=1) + "\n", encoding="utf-8")
    print(f"held-out accuracy: float {float_acc:.4f}, int8 {int8_acc:.4f}; wrote {MODEL_JSON}")


if __name__ == "__main__":
    main()
