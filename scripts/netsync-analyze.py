#!/usr/bin/env python3
"""
PR-18 — Netcode regression test analyzer.

Joins per-bot snapshot-observation logs (`bot_<id>.csv`) with the
server-side ground-truth log (`server_truth.csv`) and reports the
euclidean desync between what each bot SAW for each entity and what
the server's authoritative state actually was at the same wall-clock
moment.

Inputs (all columns lowerCamelCase):

    server_truth.csv:
      wallTimeNs,serverTick,clientId,posX,posY,posZ

    bot_<id>.csv  (one per bot):
      wallTimeNs,observerBotId,observedClientId,posX,posY,posZ

Algorithm:

  for each row in each bot file:
    target = (observedClientId, wallTimeNs)
    find the two server rows for the same clientId bracketing
    wallTimeNs;  linearly interpolate position between them; compute
    `euclidean(bot_pos, interpolated_truth)`.
  aggregate the per-row desync values into mean / p50 / p99 / max
  per (observerBotId, observedClientId) and globally.

Why linear interpolation between bracketing samples?  At the default
32 Hz throttle, server samples are ~31 ms apart.  Linearly
interpolating between two adjacent samples is a much better
approximation of "where was the entity at exactly wallTimeNs T" than
nearest-neighbour, especially for entities moving at 400-500 u/s.

Usage:

    netsync-analyze.py <run-dir>

where <run-dir> contains `server_truth.csv` and `bot_*.csv`.  The
loadtest harness auto-creates this layout.

Output: per-(observer, observed) summary lines, then a global
summary, on stdout.  Exit 0 on success.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def loadServerTruth(path: Path) -> dict[int, list[tuple[int, float, float, float]]]:
    """
    Returns: clientId -> sorted list of (wallTimeNs, x, y, z) tuples.
    """
    perEntity: dict[int, list[tuple[int, float, float, float]]] = defaultdict(list)
    with path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            cid = int(row["clientId"])
            t = int(row["wallTimeNs"])
            x = float(row["posX"])
            y = float(row["posY"])
            z = float(row["posZ"])
            perEntity[cid].append((t, x, y, z))
    for cid in perEntity:
        perEntity[cid].sort()
    return perEntity


def lerpServerPos(
    samples: list[tuple[int, float, float, float]], t: int
) -> tuple[float, float, float] | None:
    """
    Linear interpolation of (x, y, z) at wallTimeNs = t between the two
    bracketing server samples.  Returns None if `t` is outside the
    sampled range — those rows are excluded from desync stats since
    extrapolation would give misleading numbers.
    """
    if len(samples) < 2:
        return None
    times = [s[0] for s in samples]
    if t < times[0] or t > times[-1]:
        return None
    idx = bisect.bisect_left(times, t)
    # bisect_left: insertion point.  idx==0 means t == times[0]; idx==len
    # means t > all. We've already guarded the boundaries.
    if idx == 0:
        s = samples[0]
        return (s[1], s[2], s[3])
    if times[idx] == t:
        s = samples[idx]
        return (s[1], s[2], s[3])
    a = samples[idx - 1]
    b = samples[idx]
    span = b[0] - a[0]
    if span <= 0:
        return (a[1], a[2], a[3])
    alpha = (t - a[0]) / span
    return (
        a[1] + (b[1] - a[1]) * alpha,
        a[2] + (b[2] - a[2]) * alpha,
        a[3] + (b[3] - a[3]) * alpha,
    )


def percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    if len(values) == 1:
        return values[0]
    s = sorted(values)
    k = (len(s) - 1) * (q / 100.0)
    lo = math.floor(k)
    hi = math.ceil(k)
    if lo == hi:
        return s[int(k)]
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("runDir", type=Path, help="directory containing server_truth.csv + bot_*.csv")
    parser.add_argument(
        "--top",
        type=int,
        default=5,
        help="show top-N (observer, observed) pairs by mean desync (default 5)",
    )
    args = parser.parse_args()

    truthPath = args.runDir / "server_truth.csv"
    if not truthPath.exists():
        print(f"error: missing {truthPath}", file=sys.stderr)
        return 1

    truth = loadServerTruth(truthPath)
    if not truth:
        print(f"error: server_truth.csv is empty (no rows)", file=sys.stderr)
        return 1

    botFiles = sorted(args.runDir.glob("bot_*.csv"))
    if not botFiles:
        print(f"error: no bot_*.csv files in {args.runDir}", file=sys.stderr)
        return 1

    # Aggregate desync values keyed by (observerBotId, observedClientId)
    # plus a global pool.
    perPair: dict[tuple[int, int], list[float]] = defaultdict(list)
    allDesync: list[float] = []
    botRowsTotal = 0
    botRowsOutOfRange = 0
    botRowsUnknownEntity = 0

    for botFile in botFiles:
        with botFile.open() as f:
            reader = csv.DictReader(f)
            for row in reader:
                botRowsTotal += 1
                obsId = int(row["observerBotId"])
                cid = int(row["observedClientId"])
                t = int(row["wallTimeNs"])
                bx = float(row["posX"])
                by = float(row["posY"])
                bz = float(row["posZ"])

                serverSamples = truth.get(cid)
                if serverSamples is None:
                    botRowsUnknownEntity += 1
                    continue
                interp = lerpServerPos(serverSamples, t)
                if interp is None:
                    botRowsOutOfRange += 1
                    continue
                dx = bx - interp[0]
                dy = by - interp[1]
                dz = bz - interp[2]
                d = math.sqrt(dx * dx + dy * dy + dz * dz)
                perPair[(obsId, cid)].append(d)
                allDesync.append(d)

    # Per-pair summary, sorted by mean desync descending.
    rankedPairs = sorted(
        ((pair, statistics.mean(vs), len(vs), percentile(vs, 50), percentile(vs, 99), max(vs))
         for pair, vs in perPair.items()),
        key=lambda x: -x[1],
    )

    print(f"PR-18 netsync analyzer — {args.runDir}")
    print(f"")
    print(f"server truth rows: {sum(len(v) for v in truth.values())}")
    print(f"server truth entities: {len(truth)}")
    print(f"bot files: {len(botFiles)}")
    print(f"bot observation rows total: {botRowsTotal}")
    print(f"  paired with server truth: {len(allDesync)}")
    print(f"  out-of-range (before/after server log window): {botRowsOutOfRange}")
    print(f"  unknown entity (no server truth for clientId): {botRowsUnknownEntity}")
    print(f"")

    if not allDesync:
        print(f"warning: zero paired observations — analyzer cannot report desync")
        return 0

    print(f"Global desync (units):")
    print(f"  count:  {len(allDesync)}")
    print(f"  mean:   {statistics.mean(allDesync):.3f}")
    print(f"  median: {percentile(allDesync, 50):.3f}")
    print(f"  p99:    {percentile(allDesync, 99):.3f}")
    print(f"  max:    {max(allDesync):.3f}")
    print(f"")

    print(f"Top {args.top} (observerBot, observedClient) pairs by mean desync:")
    print(f"  {'observer':>10}  {'observed':>10}  {'count':>8}  {'mean':>8}  {'p50':>8}  {'p99':>8}  {'max':>8}")
    for pair, mean, count, p50, p99, mx in rankedPairs[: args.top]:
        print(f"  {pair[0]:>10}  {pair[1]:>10}  {count:>8}  {mean:>8.3f}  {p50:>8.3f}  {p99:>8.3f}  {mx:>8.3f}")
    print(f"")

    # Sanity-check signal: if mean global desync is > 100 units, the
    # bot fleet is meaningfully out of sync with server reality and we
    # should fail the test.  At sub-tick precision we expect under
    # 5 units typical, under 50 units even at high simulated jitter.
    meanGlobal = statistics.mean(allDesync)
    if meanGlobal > 100.0:
        print(f"FAIL: mean desync {meanGlobal:.1f} u exceeds 100 u threshold — netcode regression?")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
