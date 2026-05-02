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


def loadServerShots(path: Path) -> list[dict]:
    """
    Load server-side shot-resolution rows.  Returns a list of dicts so
    the caller can group/filter however it likes.

    Pre-PR-22 schema:
      wallTimeNs, shooterClientId, shotInputTick,
      hitClientId, hitX, hitY, hitZ, hitRegion

    PR-22 added columns (back-compat: `.get(key, default)` when reading,
    so older logs from before PR-22 still parse):
      originX/Y/Z, dirX/Y/Z,
      shooterRttMs, lagCompTicks,
      hitTargetRewoundX/Y/Z, hitTargetCurrentX/Y/Z
    """
    rows: list[dict] = []
    with path.open() as f:
        reader = csv.DictReader(f)
        for row in reader:
            # Skip truncated final rows — common when SIGINT hits the
            # server mid-fprintf during the loadtest harness teardown.
            # Any cell being None means csv.DictReader saw fewer
            # fields than the header.
            if any(v is None for v in row.values()):
                continue

            def fget(key: str, default: float = 0.0) -> float:
                v = row.get(key)
                if v is None or v == "":
                    return default
                try:
                    return float(v)
                except ValueError:
                    return default

            def iget(key: str, default: int = 0) -> int:
                v = row.get(key)
                if v is None or v == "":
                    return default
                try:
                    return int(v)
                except ValueError:
                    return default

            try:
                rows.append(
                    {
                        "wallTimeNs": int(row["wallTimeNs"]),
                        "shooterClientId": int(row["shooterClientId"]),
                        "shotInputTick": int(row["shotInputTick"]),
                        "hitClientId": int(row["hitClientId"]),
                        "hitX": float(row["hitX"]),
                        "hitY": float(row["hitY"]),
                        "hitZ": float(row["hitZ"]),
                        "hitRegion": int(row["hitRegion"]),
                        # PR-22: optional fields, default 0 when absent.
                        "originX": fget("originX"),
                        "originY": fget("originY"),
                        "originZ": fget("originZ"),
                        "dirX": fget("dirX"),
                        "dirY": fget("dirY"),
                        "dirZ": fget("dirZ"),
                        "shooterRttMs": iget("shooterRttMs"),
                        "lagCompTicks": iget("lagCompTicks"),
                        "hitTargetRewoundX": fget("hitTargetRewoundX"),
                        "hitTargetRewoundY": fget("hitTargetRewoundY"),
                        "hitTargetRewoundZ": fget("hitTargetRewoundZ"),
                        "hitTargetCurrentX": fget("hitTargetCurrentX"),
                        "hitTargetCurrentY": fget("hitTargetCurrentY"),
                        "hitTargetCurrentZ": fget("hitTargetCurrentZ"),
                        # PR-27: client-asserted animation telemetry.
                        "clientIntentTargetClientId": iget("clientIntentTargetClientId", 0xFFFF),
                        "animStateDelta": fget("animStateDelta"),
                        "clientIntentReceived": iget("clientIntentReceived"),
                    }
                )
            except ValueError:
                # Malformed numeric cell — skip silently rather than
                # abort the whole report.
                continue
    return rows


def reportShots(rows: list[dict]) -> None:
    """
    Hit-rate report keyed off the server's authoritative shot log.
    A "hit" is any non-`k_missClientId` (= 65535) hitClientId.
    """
    if not rows:
        print("no shots logged")
        return
    missCid = 0xFFFF
    total = len(rows)
    hits = sum(1 for r in rows if r["hitClientId"] != missCid)
    misses = total - hits
    hitRate = hits / total if total > 0 else 0.0

    perShooter: dict[int, list[dict]] = defaultdict(list)
    for r in rows:
        perShooter[r["shooterClientId"]].append(r)

    print(f"PR-18b shot-resolution log:")
    print(f"  total shots resolved:   {total}")
    print(f"  hits:                   {hits}")
    print(f"  misses:                 {misses}")
    print(f"  global hit rate:        {hitRate*100:.1f} %")
    print(f"  shooting clients:       {len(perShooter)}")

    perRegionCount: dict[int, int] = defaultdict(int)
    for r in rows:
        if r["hitClientId"] != missCid:
            perRegionCount[r["hitRegion"]] += 1
    if perRegionCount:
        print(f"  hit-region distribution:")
        # Region IDs match `physics::HitboxRegion` enum.  We don't import
        # the enum here; numeric IDs are sufficient for regression tests
        # and the user can grep the C++ side if a name lookup is needed.
        for region, count in sorted(perRegionCount.items()):
            pct = count * 100.0 / hits if hits > 0 else 0.0
            print(f"    region {region}: {count} ({pct:.1f}% of hits)")
    print()


def loadBotShots(runDir: Path) -> list[dict]:
    """
    Load PR-21 bot-side shot-intent rows across all `bot_shots_*.csv`
    files in the run directory.  Each row is a single trigger pull
    on the bot side with origin/dir + intended target.

    Pre-PR-22 schema:
      wallTimeNs, shooterClientId, shotInputTick,
      originX/Y/Z, dirX/Y/Z,
      intendedTargetClientId, intendedTargetX/Y/Z, intendedTargetDist

    PR-22 added columns (back-compat with `.get(key, default)`):
      botRayHit, botHitClientId, botHitX/Y/Z, botHitDist
    """
    rows: list[dict] = []
    for f in sorted(runDir.glob("bot_shots_*.csv")):
        with f.open() as fh:
            reader = csv.DictReader(fh)
            for row in reader:
                if any(v is None for v in row.values()):
                    continue

                def fget(key: str, default: float = 0.0) -> float:
                    v = row.get(key)
                    if v is None or v == "":
                        return default
                    try:
                        return float(v)
                    except ValueError:
                        return default

                def iget(key: str, default: int = 0) -> int:
                    v = row.get(key)
                    if v is None or v == "":
                        return default
                    try:
                        return int(v)
                    except ValueError:
                        return default

                try:
                    rows.append({
                        "wallTimeNs": int(row["wallTimeNs"]),
                        "shooterClientId": int(row["shooterClientId"]),
                        "shotInputTick": int(row["shotInputTick"]),
                        "originX": float(row["originX"]),
                        "originY": float(row["originY"]),
                        "originZ": float(row["originZ"]),
                        "dirX": float(row["dirX"]),
                        "dirY": float(row["dirY"]),
                        "dirZ": float(row["dirZ"]),
                        "intendedTargetClientId": int(row["intendedTargetClientId"]),
                        "intendedTargetX": float(row["intendedTargetX"]),
                        "intendedTargetY": float(row["intendedTargetY"]),
                        "intendedTargetZ": float(row["intendedTargetZ"]),
                        "intendedTargetDist": float(row["intendedTargetDist"]),
                        # PR-22: optional bot-side AABB raycast.  When
                        # absent (older runs) defaults to "no hit".
                        "botRayHit": iget("botRayHit"),
                        "botHitClientId": iget("botHitClientId", 0xFFFF),
                        "botHitX": fget("botHitX"),
                        "botHitY": fget("botHitY"),
                        "botHitZ": fget("botHitZ"),
                        "botHitDist": fget("botHitDist"),
                    })
                except ValueError:
                    continue
    return rows


def reportHitReg(botShots: list[dict], serverShots: list[dict]) -> None:
    """
    Join bot-side intent with server-side resolution by
    `(shooterClientId, shotInputTick)` and report:

      - total shots fired vs server-resolved (ratio = packet-loss + tick-mismatch indicator)
      - hit rate (server-side determined)
      - intended-vs-actual target match rate
      - hit-region distribution
      - hit-point distance distribution (where intended target hit vs server's resolved point)
      - PR-22: client-vs-server agreement matrix (both hit / only client / only server / neither)
      - PR-22: client-vs-server hit-target match rate (when both hit, do they agree on WHO?)
      - PR-22: lag-comp drift distribution (rewound vs current target position on the server)
      - PR-22: ray-origin desync distribution (server's view of origin minus bot's view)
    """
    if not botShots:
        print("PR-21 hitreg: no bot shots logged (set GROUP2_BOT_SHOTS_CSV_PREFIX + GROUP2_BOT_AI=1)")
        print()
        return

    serverByKey: dict[tuple[int, int], dict] = {}
    for s in serverShots:
        serverByKey[(s["shooterClientId"], s["shotInputTick"])] = s

    missCid = 0xFFFF
    fired = len(botShots)
    matched = 0
    hitsOnIntended = 0
    hitsOnOther = 0
    misses = 0
    regionCounts: dict[int, int] = defaultdict(int)
    hitPointDistances: list[float] = []  # server-hit-point vs bot-intended-target

    # PR-22: client-vs-server agreement counters.
    bothHit = 0
    bothHitSameTarget = 0
    bothHitDifferentTarget = 0
    onlyClientHit = 0
    onlyServerHit = 0
    neitherHit = 0

    # PR-22: lag-comp drift (in world units, XZ-projected so the
    # rewound-capsule-mid vs foot-position vertical offset doesn't
    # bias the magnitude).
    lagCompDriftXZ: list[float] = []

    # PR-22: ray-origin desync — server's view of shooter origin minus
    # bot's view.  Reflects mid-air drift between client prediction
    # and server authority.  Should be <1 unit with prediction
    # reconciliation working.
    rayOriginDesync: list[float] = []

    # PR-22: lag-comp ticks distribution — bucket the matched shots by
    # how far back the server rewound, so the user can see the
    # rewind-magnitude split by RTT.
    rttBuckets: dict[int, int] = defaultdict(int)
    rttToHits: dict[int, int] = defaultdict(int)

    for b in botShots:
        sv = serverByKey.get((b["shooterClientId"], b["shotInputTick"]))
        if sv is None:
            continue
        matched += 1

        # PR-22: client-vs-server agreement matrix.
        botHit = bool(b.get("botRayHit", 0))
        srvHit = sv["hitClientId"] != missCid
        if botHit and srvHit:
            bothHit += 1
            if b.get("botHitClientId", missCid) == sv["hitClientId"]:
                bothHitSameTarget += 1
            else:
                bothHitDifferentTarget += 1
        elif botHit and not srvHit:
            onlyClientHit += 1
        elif srvHit and not botHit:
            onlyServerHit += 1
        else:
            neitherHit += 1

        # PR-22: lag-comp drift — distance between rewound and current
        # target position on the server.  Only meaningful when the
        # server had a hit (otherwise the columns are zeroed).
        if srvHit and (sv["hitTargetCurrentX"] != 0.0 or sv["hitTargetCurrentZ"] != 0.0):
            dx = sv["hitTargetCurrentX"] - sv["hitTargetRewoundX"]
            dz = sv["hitTargetCurrentZ"] - sv["hitTargetRewoundZ"]
            lagCompDriftXZ.append(math.sqrt(dx * dx + dz * dz))

        # PR-22: ray-origin desync — Euclidean distance, server's
        # origin vs bot's origin.
        if sv.get("originX", 0.0) != 0.0 or sv.get("originZ", 0.0) != 0.0:
            ddx = sv["originX"] - b["originX"]
            ddy = sv["originY"] - b["originY"]
            ddz = sv["originZ"] - b["originZ"]
            rayOriginDesync.append(math.sqrt(ddx * ddx + ddy * ddy + ddz * ddz))

        # PR-22: RTT bucketing.  Round to nearest 25 ms for readable
        # buckets at the typical 0/30/100/200 sweep points.
        rttBucket = ((sv.get("shooterRttMs", 0) + 12) // 25) * 25
        rttBuckets[rttBucket] += 1
        if srvHit:
            rttToHits[rttBucket] += 1

        if sv["hitClientId"] == missCid:
            misses += 1
            continue
        if sv["hitClientId"] == b["intendedTargetClientId"]:
            hitsOnIntended += 1
        else:
            hitsOnOther += 1
        regionCounts[sv["hitRegion"]] += 1
        # PR-22: XZ-only distance — the bot logs the target's foot
        # position in `intendedTargetX/Y/Z`, while the server logs the
        # actual hit point on the body capsule.  Vertical bias varies
        # with target stance / aim offset / hit region, so we project
        # to the horizontal plane to get a clean "how far off was the
        # ray" number.
        dx = b["intendedTargetX"] - sv["hitX"]
        dz = b["intendedTargetZ"] - sv["hitZ"]
        hitPointDistances.append(math.sqrt(dx * dx + dz * dz))

    print(f"PR-21 hit-reg analysis:")
    print(f"  shots fired by bots:     {fired}")
    print(f"  shots resolved by server: {matched} ({100.0 * matched / fired:.1f} %)")
    if matched == 0:
        print(f"  no shots matched between bot intent and server resolution")
        print()
        return
    print(f"  hits on intended target: {hitsOnIntended} ({100.0 * hitsOnIntended / matched:.1f} %)")
    print(f"  hits on OTHER target:    {hitsOnOther} ({100.0 * hitsOnOther / matched:.1f} %)")
    print(f"  misses:                  {misses} ({100.0 * misses / matched:.1f} %)")
    if regionCounts:
        print(f"  hit-region distribution (server-side):")
        for region, count in sorted(regionCounts.items()):
            pct = 100.0 * count / max(1, hitsOnIntended + hitsOnOther)
            print(f"    region {region}: {count} ({pct:.1f}% of hits)")
    if hitPointDistances:
        hitPointDistances.sort()
        print(f"  intended-target → server-hit-point distance (units):")
        print(f"    mean:  {sum(hitPointDistances) / len(hitPointDistances):.2f}")
        print(f"    p50:   {percentile(hitPointDistances, 50):.2f}")
        print(f"    p99:   {percentile(hitPointDistances, 99):.2f}")
        print(f"    max:   {hitPointDistances[-1]:.2f}")

    # PR-22: client-vs-server hit-decision agreement.  This is the
    # headline diagnostic for "is the lag-comp delivering the same
    # result both sides see?".  Real-game expectation under healthy
    # netcode: bothHit large + bothHitSameTarget ≈ bothHit, with
    # onlyServerHit small and onlyClientHit ≈ 0 (server lag-comp
    # should NOT find hits the client already missed at the AABB
    # level — that would mean a target moved into a position only
    # the rewound capsule occupied).
    print(f"  client-vs-server agreement matrix:")
    print(f"    both hit:               {bothHit} ({100.0 * bothHit / matched:.1f} %)")
    print(f"      same target:          {bothHitSameTarget} "
          f"({(100.0 * bothHitSameTarget / bothHit) if bothHit else 0.0:.1f} % of bothHit)")
    print(f"      different target:     {bothHitDifferentTarget}")
    print(f"    only client hit (AABB): {onlyClientHit} ({100.0 * onlyClientHit / matched:.1f} %)")
    print(f"    only server hit:        {onlyServerHit} ({100.0 * onlyServerHit / matched:.1f} %)")
    print(f"    neither hit:            {neitherHit} ({100.0 * neitherHit / matched:.1f} %)")

    if lagCompDriftXZ:
        lagCompDriftXZ.sort()
        print(f"  lag-comp drift (rewound vs current target XZ, units):")
        print(f"    mean:  {sum(lagCompDriftXZ) / len(lagCompDriftXZ):.2f}")
        print(f"    p50:   {percentile(lagCompDriftXZ, 50):.2f}")
        print(f"    p99:   {percentile(lagCompDriftXZ, 99):.2f}")
        print(f"    max:   {lagCompDriftXZ[-1]:.2f}")

    if rayOriginDesync:
        rayOriginDesync.sort()
        print(f"  ray-origin desync (server view − bot view, units):")
        print(f"    mean:  {sum(rayOriginDesync) / len(rayOriginDesync):.2f}")
        print(f"    p50:   {percentile(rayOriginDesync, 50):.2f}")
        print(f"    p99:   {percentile(rayOriginDesync, 99):.2f}")
        print(f"    max:   {rayOriginDesync[-1]:.2f}")

    if rttBuckets:
        print(f"  shots by shooter RTT bucket (server-side):")
        for rtt, count in sorted(rttBuckets.items()):
            hits = rttToHits.get(rtt, 0)
            hr = 100.0 * hits / count if count else 0.0
            print(f"    {rtt:>4d} ms: {count:>5d} shots, {hits:>5d} hits ({hr:.1f} % hit rate)")

    # PR-27: client-asserted animation-state delta distribution.
    # Only counts shots where (a) the client SHOT_INTENT was received,
    # and (b) the server's hit target matches the client's claimed
    # target — those are the rows where `animStateDelta` is meaningful.
    intentReceived = 0
    intentMatchedTarget = 0
    intentMissingTarget = 0  # client claimed someone, server hit someone else
    intentNoTarget = 0       # client said "no target" (0xFFFF)
    animDeltas: list[float] = []
    for sv in serverShots:
        if not sv.get("clientIntentReceived", 0):
            continue
        intentReceived += 1
        clientTgt = sv.get("clientIntentTargetClientId", 0xFFFF)
        if clientTgt == 0xFFFF:
            intentNoTarget += 1
            continue
        if sv["hitClientId"] == clientTgt:
            intentMatchedTarget += 1
            animDeltas.append(sv.get("animStateDelta", 0.0))
        else:
            intentMissingTarget += 1

    if intentReceived > 0:
        print(f"  PR-27 client anim-state assertions:")
        print(f"    SHOT_INTENT received:   {intentReceived} ({100.0 * intentReceived / matched:.1f} % of matched shots)")
        print(f"    matched target  (server hit who client claimed):       {intentMatchedTarget}")
        print(f"    different target (server hit someone else):            {intentMissingTarget}")
        print(f"    no specific target claimed by client (0xFFFF):         {intentNoTarget}")
        if animDeltas:
            animDeltas.sort()
            avg = sum(animDeltas) / len(animDeltas)
            print(f"    anim-state delta (matched-target only, dimensionless):")
            print(f"      mean:  {avg:.4f}")
            print(f"      p50:   {percentile(animDeltas, 50):.4f}")
            print(f"      p90:   {percentile(animDeltas, 90):.4f}")
            print(f"      p99:   {percentile(animDeltas, 99):.4f}")
            print(f"      max:   {animDeltas[-1]:.4f}")
            # Bucket against an indicative epsilon (PR-27a default = 0.10).
            buckets = [(0.01, 0), (0.05, 0), (0.10, 0), (0.25, 0), (1.0, 0)]
            over1 = 0
            for d in animDeltas:
                placed = False
                for i, (thr, _) in enumerate(buckets):
                    if d <= thr:
                        buckets[i] = (thr, buckets[i][1] + 1)
                        placed = True
                        break
                if not placed:
                    over1 += 1
            print(f"      distribution (cumulative buckets):")
            for thr, cnt in buckets:
                pct = 100.0 * cnt / len(animDeltas)
                print(f"        Δ ≤ {thr:.2f}: {cnt:>5d} ({pct:5.1f} %)")
            if over1 > 0:
                pct = 100.0 * over1 / len(animDeltas)
                print(f"        Δ >  1.00: {over1:>5d} ({pct:5.1f} %)  // different clip / state machine disagreement")

    print()


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

    # PR-18b: report shot-resolution stats first when the file is
    # present.  Hit-rate is the headline metric for hitreg-under-loss
    # regression tests and is independent of the desync analysis
    # below.  Both blocks render even if one input is missing.
    shotsPath = args.runDir / "server_shots.csv"
    serverShots: list[dict] = []
    if shotsPath.exists():
        serverShots = loadServerShots(shotsPath)
        reportShots(serverShots)

    # PR-21: when bot shot-intent logs exist, join with server's
    # resolution log and report hit-reg accuracy.  Intended-vs-actual
    # target distinction is the headline lag-comp signal.
    botShots = loadBotShots(args.runDir)
    if botShots:
        reportHitReg(botShots, serverShots)

    truthPath = args.runDir / "server_truth.csv"
    if not truthPath.exists():
        print(f"error: missing {truthPath}", file=sys.stderr)
        return 1

    truth = loadServerTruth(truthPath)
    if not truth:
        print(f"error: server_truth.csv is empty (no rows)", file=sys.stderr)
        return 1

    # PR-22b: `bot_*.csv` matches both observation logs (bot_0.csv) AND
    # shot-intent logs (bot_shots_0.csv).  Filter to numeric-suffix
    # observation logs only — shot logs are loaded separately by
    # `loadBotShots`.
    botFiles = sorted(p for p in args.runDir.glob("bot_*.csv") if "shots" not in p.name)
    if not botFiles:
        print(f"error: no bot observation csvs in {args.runDir}", file=sys.stderr)
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
