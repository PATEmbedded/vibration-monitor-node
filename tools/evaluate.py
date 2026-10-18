"""Evaluate the int8 model on a fresh synthetic test set and print a confusion matrix.

    python tools/evaluate.py --min-accuracy 0.95

The test set uses its own seed, so it never overlaps the training data. The output is Markdown,
ready for a CI job summary.
"""
from __future__ import annotations

import argparse
import sys

import numpy as np

from vmnlib import CLASS_NAMES, Int8Model, features, generate, random_params


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--per-class", type=int, default=250)
    ap.add_argument("--seed", type=int, default=2026)
    ap.add_argument("--min-accuracy", type=float, default=0.0)
    args = ap.parse_args()

    rng = np.random.default_rng(args.seed)
    params, labels = [], []
    for cls in range(len(CLASS_NAMES)):
        for _ in range(args.per_class):
            params.append(random_params(cls, rng))
            labels.append(cls)
    seeds = rng.integers(1, 2**32, size=len(params), dtype=np.uint64)
    feats = features(generate(params, seeds), [p.shaft_hz for p in params])
    labels = np.array(labels)
    predicted = Int8Model.load().predict(feats)

    accuracy = float((predicted == labels).mean())
    print(f"### int8 model on {len(labels)} synthetic frames (seed {args.seed})\n")
    print(f"Accuracy: **{accuracy:.4f}**\n")
    print("| true \\ predicted | " + " | ".join(CLASS_NAMES) + " |")
    print("|---|" + "---:|" * len(CLASS_NAMES))
    for t, name in enumerate(CLASS_NAMES):
        row = [int(((labels == t) & (predicted == p)).sum()) for p in range(len(CLASS_NAMES))]
        print(f"| {name} | " + " | ".join(map(str, row)) + " |")
    if accuracy < args.min_accuracy:
        sys.exit(f"accuracy {accuracy:.4f} is below {args.min_accuracy}")


if __name__ == "__main__":
    main()
