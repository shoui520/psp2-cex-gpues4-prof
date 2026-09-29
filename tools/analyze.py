#!/usr/bin/env python3
"""Analyze gpuprof captures; never interpret host duration as GPU cost."""
import argparse
import csv
import json
from collections import Counter
from pathlib import Path

COLUMNS = ("sequence kind before_us after_us thread context frame scene scope object "
           "arg0 arg1 arg2 arg3 arg4 arg5 arg6 arg7 flags result label").split()


def read_capture(stream):
    rows = csv.reader(stream, strict=True)
    if next(rows, None) != ["gpuprof-capture", "1"]:
        raise ValueError("unsupported capture header")
    if next(rows, None) != ["columns", *COLUMNS]:
        raise ValueError("unsupported capture columns")
    events = []
    footer = None
    for row in rows:
        if footer is not None:
            raise ValueError("data after capture footer")
        if row and row[0] == "end":
            if len(row) != 3:
                raise ValueError("malformed footer")
            footer = [int(v) for v in row[1:]]
            continue
        if len(row) != len(COLUMNS) + 1 or row[0] != "event":
            raise ValueError("malformed event")
        e = dict(zip(COLUMNS, row[1:]))
        for key in COLUMNS[:-1]:
            e[key] = int(e[key])
            lo, hi = (-(1 << 31), (1 << 31)-1) if key == "result" else (0, (1 << 64)-1)
            if not lo <= e[key] <= hi:
                raise ValueError(f"{key} out of range")
        if e["sequence"] != len(events) or e["before_us"] > e["after_us"]:
            raise ValueError("invalid sequence or timestamp bracket")
        events.append(e)
    if footer is None or footer[0] != len(events) or not 0 <= footer[1] < 1 << 64:
        raise ValueError("missing or invalid footer")
    return events, footer[1]


def overlapping_context_calls(events):
    """Find ambiguous calls even when callbacks were recorded out of order.

    This is a conservative host interval check, not a GPU submission order.
    Connected overlapping intervals form a group; separate contexts never mix.
    """
    by_context = {}
    for e in events:
        if e["kind"] == 17 and e["context"]:
            by_context.setdefault(e["context"], []).append(e)
    ambiguous = set()
    for calls in by_context.values():
        group, end = [], -1
        for e in sorted(calls, key=lambda e: (e["before_us"], e["after_us"])):
            if e["before_us"] >= end:
                if len(group) > 1:
                    ambiguous.update(group)
                group = []
            group.append(e["sequence"])
            end = max(end, e["after_us"])
        if len(group) > 1:
            ambiguous.update(group)
    return ambiguous


def summarize(events, dropped=0):
    contexts, programs, threads, gl_programs = {}, {}, {}, {}
    generations = Counter()
    warnings = Counter()
    draws, scenes = [], []
    transfers, presentations = [], []
    diagnostics = decode_diagnostics(events)
    ambiguous_calls = overlapping_context_calls(events)
    if dropped:
        warnings["capture buffer overflow: attribution history incomplete"] += dropped

    def warn(reason):
        warnings[reason] += 1

    def generation(kind, address):
        generations[kind, address] += 1
        return generations[kind, address]

    snapshots = []
    for e in events:
        if e["kind"] != 23:
            break
        snapshots.append(e)
    if any(e["kind"] == 23 for e in events[len(snapshots):]):
        raise ValueError("snapshot outside capture prefix")
    snapshot_programs = {}
    for e in snapshots:
        kind, address = e["object"], e["context"]
        if kind == 0:
            continue
        gen = e["arg1"]
        if not gen or not address:
            raise ValueError("invalid snapshot identity")
        if kind in (2, 3):
            key = (kind-1, e["arg0"], address)
            if key in programs or gen in snapshot_programs or not e["arg2"]:
                raise ValueError("invalid snapshot shader")
            programs[key] = {"generation": gen, "refs": e["arg2"], "name": "",
                             "uncertain": False, "after": e["after_us"]}
            snapshot_programs[gen] = key
            generations["shader", key] = gen
        elif kind != 1:
            raise ValueError("unknown snapshot object kind")
    for e in snapshots:
        if e["object"] != 1:
            continue
        address, gen = e["context"], e["arg1"]
        if address in contexts:
            raise ValueError("duplicate snapshot context")
        bindings = []
        for stage, field in ((1, "arg3"), (2, "arg4")):
            key = snapshot_programs.get(e[field])
            if e[field] and (not key or key[0] != stage):
                warn("snapshot binding references retired shader")
                key = None
            bindings.append((key, e[field]) if key else None)
        contexts[address] = {"generation": gen, "scene": None, "next_scene": 0,
            "front_fragment_enabled": {0: None, 1: True, 2: False}.get(e["arg5"]),
            "back_fragment_enabled": {0: None, 1: True, 2: False}.get(e["arg6"]),
            "draw": 0, "vs": bindings[0], "fs": bindings[1], "after": e["after_us"],
            "uncertain": False}
        generations["context", address] = gen

    for e in events:
        t = threads.setdefault(e["thread"], {"frame": None, "frame_name": "", "scopes": [],
                                             "draw": "", "gl": [], "gl_program": None})
        kind, call = e["kind"], e["object"]
        a = [e[f"arg{i}"] for i in range(8)]
        if kind in (21, 22, 23, 24):
            continue
        if e["flags"]:
            warn("event flags present (including possible label truncation)")
        if kind == 6:
            t["frame"] = e["frame"]
            t["frame_name"] = e["label"]
        elif kind == 7:
            parent = t["scopes"][-1][0] if t["scopes"] else 0
            if parent != e["scope"]:
                warn("scope parent mismatch")
                t["scopes"].clear()
            t["scopes"].append((call, e["label"]))
        elif kind == 8:
            if not t["scopes"] or t["scopes"][-1][0] != e["scope"]:
                warn("unmatched scope pop")
                t["scopes"].clear()
            else:
                t["scopes"].pop()
        elif kind == 19:
            t["draw"] = e["label"]
        elif kind == 18:
            p = gl_programs.get(a[2]) if a[0] == 3 else programs.get(tuple(a[:3]))
            if p:
                p["name"] = e["label"]
            else:
                warn("shader name without observed program creation")
        elif kind == 16:
            warn("explicit coverage gap: " + e["label"])
        elif kind == 20:
            if a[7] == 0:
                t["gl"].append((call, e["sequence"], e["before_us"]))
            elif a[7] == 1 and t["gl"] and (t["gl"][-1][0], t["gl"][-1][2]) == (call, e["before_us"]):
                t["gl"].pop()
                if call == 1 and a[0]:
                    if a[0] in gl_programs and not gl_programs[a[0]]["delete_requested"]:
                        warn("GL program created with already-live name")
                    gl_programs[a[0]] = {"handle": a[0], "generation": generation("gl", a[0]),
                        "name": "", "delete_requested": False}
                elif call == 2 and a[0] in gl_programs:
                    gl_programs[a[0]]["delete_requested"] = True
                elif call == 3:
                    # Void UseProgram has no success indication. This is a hint,
                    # never the authoritative shader attached to a GPU draw.
                    t["gl_program"] = gl_programs.get(a[0])
            else:
                warn("unmatched vitaGL entry/return")
                t["gl"].clear()
        elif kind == 17:
            address = e["context"]
            if call in (18, 19, 20, 21, 22):
                operation = {"sequence": e["sequence"], "thread": e["thread"],
                    "frame": t["frame"], "frame_name": t["frame_name"],
                    "pass": "/".join(name for _, name in t["scopes"]),
                    "name": t["draw"], "call_kind": call, "args": a,
                    "result": e["result"], "host_call_us": e["after_us"]-e["before_us"],
                    "gpu_time_us": None}
                (presentations if call == 18 else transfers).append(operation)
                if e["result"]:
                    warn("failed presentation or transfer call")
                continue
            if e["result"]:
                warn("failed GXM call")
                continue
            if call == 17:
                for key in list(programs):
                    if key[1] == a[0]:
                        del programs[key]
                continue
            if call in (11, 12, 13, 14, 15, 16, 25):
                stage = 1 if call in (11, 13, 15) else 2
                handle = a[2] if call in (11, 12, 25) else a[1]
                key = (stage, a[0], handle)
                p = programs.get(key)
                if p and e["before_us"] < p["after"]:
                    p["uncertain"] = True
                    warn("overlapping shader lifetime calls")
                if call in (11, 12, 25):
                    if not handle:
                        warn("successful shader create without handle")
                        continue
                    if p is None:
                        p = programs[key] = {"generation": generation("shader", key),
                            "refs": 0, "name": "", "uncertain": False, "after": 0}
                    p["refs"] += 1
                    if call == 25:
                        p["name"] = "GXM mask update"
                elif p is None:
                    warn("shader reference without creation history")
                elif call in (13, 14):
                    p["refs"] += 1
                else:
                    p["refs"] -= 1
                    if not p["refs"]:
                        del programs[key]
                if p:
                    p["after"] = max(p["after"], e["after_us"])
                continue
            if call == 9:
                if address in contexts:
                    warn("context created while already live")
                contexts[address] = {"generation": generation("context", address),
                    "scene": None, "next_scene": 0, "draw": 0, "vs": None, "fs": None,
                    "after": e["after_us"], "uncertain": False}
                continue
            c = contexts.get(address)
            if c is None:
                warn("GXM context missing creation history")
                # Retain submitted work even when identity cannot be recovered.
                c = contexts[address] = {"generation": None, "scene": None,
                    "next_scene": 0, "draw": 0, "vs": None, "fs": None,
                    "after": 0, "uncertain": True}
            if e["sequence"] in ambiguous_calls or e["before_us"] < c["after"]:
                c["uncertain"] = True
                warn("overlapping calls on one GXM context")
            c["after"] = max(c["after"], e["after_us"])
            if call == 10:
                if c["scene"] is not None:
                    warn("context destroyed with open observed scene")
                del contexts[address]
            elif call == 1:
                if c["scene"] is not None:
                    warn("nested GXM scene")
                    c["uncertain"] = True
                c["next_scene"] += 1
                c["scene"] = c["next_scene"]
                c["draw"] = 0
                scene = {"context": address, "context_generation": c["generation"],
                    "scene": c["scene"], "begin_sequence": e["sequence"],
                    "end_sequence": None, "begin_thread": e["thread"],
                    "frame": t["frame"], "frame_name": t["frame_name"],
                    "pass": "/".join(name for _, name in t["scopes"]),
                    "flags": a[0], "render_target": a[1], "valid_region": a[2],
                    "vertex_sync": a[3], "fragment_sync": a[4],
                    "color_surface": a[5], "depth_surface": a[6],
                    "draw_sequences": [], "flush_sequences": [],
                    "vertex_notification": None, "fragment_notification": None,
                    "closed": False, "gpu_time_us": None}
                scenes.append(scene)
                c["scene_record"] = scene
            elif call == 2:
                if c["scene"] is None:
                    warn("GXM end without begin")
                elif c.get("scene_record") is not None:
                    c["scene_record"].update(end_sequence=e["sequence"], closed=True,
                        vertex_notification=a[0], fragment_notification=a[1])
                c["scene"] = None
                c["scene_record"] = None
            elif call == 3:
                if c["scene"] is None:
                    warn("GXM flush outside observed scene")
                elif c.get("scene_record") is not None:
                    c["scene_record"]["flush_sequences"].append(e["sequence"])
            elif call in (23, 24):
                mode = {0: True, 0x200000: False}.get(a[0])
                if mode is None:
                    warn("unknown fragment program enable mode")
                c["front_fragment_enabled" if call == 23 else "back_fragment_enabled"] = mode
            elif call in (7, 8):
                stage = call - 6
                matches = [(k, p) for k, p in programs.items() if k[0] == stage and k[2] == a[0]]
                binding = None
                if len(matches) == 1:
                    k, p = matches[0]
                    binding = (k, p["generation"])
                else:
                    warn("shader binding without unique live lifetime")
                c["vs" if stage == 1 else "fs"] = binding
            elif call in (4, 5, 6):
                def shader(binding):
                    if not binding or c["uncertain"]:
                        return None
                    key, gen = binding
                    p = programs.get(key)
                    if not p or p["generation"] != gen or p["uncertain"]:
                        warn("draw references retired or uncertain shader")
                        return None
                    return {"stage": key[0], "patcher": key[1], "handle": key[2],
                            "generation": gen, "name": p["name"]}
                if call == 6:
                    warn("precomputed draw state not decoded")
                if c["scene"] is None:
                    warn("draw outside observed scene")
                elif c.get("scene_record") is not None:
                    c["scene_record"]["draw_sequences"].append(e["sequence"])
                draws.append({"sequence": e["sequence"], "thread": e["thread"],
                    "frame": t["frame"], "frame_name": t["frame_name"],
                    "context": address, "context_generation": c["generation"],
                    "scene": c["scene"], "ordinal": c["draw"], "call_kind": call,
                    "pass": "/".join(name for _, name in t["scopes"]), "name": t["draw"],
                    "gl_call_sequence": t["gl"][-1][1] if t["gl"] else None,
                    "gl_program_hint": dict(t["gl_program"], binding_verified=False)
                        if t["gl_program"] else None,
                    "vertex_shader": shader(c["vs"]) if call != 6 else None,
                    "fragment_shader": shader(c["fs"]) if call != 6 else None,
                    "front_fragment_enabled": c.get("front_fragment_enabled") if call != 6 else None,
                    "back_fragment_enabled": c.get("back_fragment_enabled") if call != 6 else None,
                    "host_call_us": e["after_us"]-e["before_us"], "gpu_time_us": None,
                    "identity_uncertain": c["uncertain"] or bool(dropped)})
                c["draw"] += 1
            elif call != 3:
                warn("unsupported GXM call kind")
        else:
            warn("unsupported event kind")
    for t in threads.values():
        if t["scopes"] or t["gl"]:
            warn("capture ended with open annotation or GL scope")
    for scene in scenes:
        if not scene["closed"]:
            warn("scene lacks observed successful end")
    timing = sampled_timing(events, draws, scenes, diagnostics, dropped)
    return {"format_version": 1, "events": len(events), "dropped": dropped,
            "warnings": dict(warnings), "draws": draws, "scenes": scenes,
            "diagnostics": diagnostics,
            "transfers": transfers, "presentations": presentations,
            "gpu_timing_available": False,
            "sampled_fragment_timing": timing,
            "lifetime_basis": "observed history; complete interception required"}


def sampled_timing(events, draws, scenes, samples, dropped, max_gap_us=3500):
    """Opt-in 3.65 identity mirror plus sampled PDS endpoint interpolation.

    No inferred counter offset, no CPU-duration substitution. Hardware ownership
    remains a candidate (sequential reads cannot exclude switch-away/back).
    Unknown intervals stay unassigned; no observation is not zero cost.
    """
    output = dict(available=False, reason="missing explicit GXM identity seed",
                  model="sampled-fragment-PDS-midpoint-v1", max_gap_us=max_gap_us,
                  passes=[], shaders=[], matched_core_observations=0)
    seeds = [e for e in events if e["kind"] == 24]
    if not seeds:
        return output
    def reject(reason):
        output["reason"] = reason
        return output
    if dropped or any(e["kind"] == 16 for e in events):
        return reject("incomplete capture history")
    if len(seeds) != 1 or seeds[0]["arg0"] != 365:
        return reject("unsupported or repeated identity seed")
    seed = seeds[0]
    pid, frame, ordinal = (seed[f"arg{i}"] for i in (1, 2, 3))
    if not 0 < pid <= 0xffffffff or frame > 0xffffff or ordinal > 0xffffffff:
        return reject("invalid identity seed")
    if any(e["kind"] in (17, 23) and e["sequence"] < seed["sequence"] for e in events):
        return reject("seed must precede all GXM history")
    by_begin = {s["begin_sequence"]: s for s in scenes}
    calls = [e for e in events if e["kind"] == 17 and e["object"] in (1, 2, 18)]
    calls.sort(key=lambda e: (e["before_us"], e["sequence"]))
    if any(b["before_us"] < a["after_us"] for a, b in zip(calls, calls[1:])):
        return reject("overlapping identity-counter operations")
    open_scene = None
    identities = {}
    for e in calls:
        if e["result"]:
            return reject("failed identity-counter operation")
        if e["object"] == 1:
            if open_scene is not None or e["arg0"]:
                return reject("overlapping or unsupported scene")
            open_scene = by_begin.get(e["sequence"])
            if open_scene is None:
                return reject("missing scene history")
            open_scene["hardware_identity_candidate"] = [pid, frame, ordinal]
            open_scene["submission_begin_us"] = e["before_us"]
        elif e["object"] == 2:
            if open_scene is None or open_scene["context"] != e["context"]:
                return reject("unmatched scene end")
            key = (pid, frame, ordinal)
            if key in identities or ordinal == 0xffffffff:
                return reject("identity reuse or wrap")
            identities[key] = open_scene
            ordinal += 1
            open_scene = None
        else:
            if open_scene is not None or frame == 0xffffff:
                return reject("presentation inside scene or counter wrap")
            frame += 1
            ordinal = 0
    if open_scene is not None:
        return reject("unfinished scene")
    if any(e["kind"] == 17 and e["object"] in (3, 5, 6) for e in events):
        return reject("flush, instanced or precomputed batch ordering not validated")
    if any(e["kind"] == 17 and (e["object"] not in range(1, 26)
                               or (e["object"] == 4 and e["result"])) for e in events):
        return reject("unsupported or failed draw submission")
    if any(d["identity_uncertain"] for d in draws):
        return reject("ambiguous draw order")
    # Transfer work shares scheduler PID fields. Do not assign it to a retained
    # GXM identity. Startup transfers before acquisition are allowed.
    if samples and any(e["kind"] == 17 and e["object"] in (19, 20, 21)
                       and e["after_us"] >= samples[0]["before_us"] for e in events):
        return reject("transfer work overlaps hardware acquisition")
    if len({s["thread"] for s in samples}) > 1:
        return reject("multiple hardware sampler streams")
    if any(s["driver_fingerprint"] != 0xc0f361a3 for s in samples):
        return reject("unsupported driver fingerprint")
    draw_by_seq = {d["sequence"]: d for d in draws}
    event_by_seq = {e["sequence"]: e for e in events}
    previous, durations, sensitivity = {}, Counter(), Counter()
    rejected, reasons, hit = Counter(), Counter(), Counter()
    shader_times, shader_sensitivity, pass_times = Counter(), Counter(), Counter()
    gaps, widths = [], []
    for sample in sorted(samples, key=lambda s: s["sample_id"]):
        i = sample["sample_id"]
        cpu = (sample["before_us"] + sample["after_us"]) / 2
        mhz = sample["clock_before_mhz"] if sample["clock_before_mhz"] == sample["clock_after_mhz"] else 0
        for core in sample["cores"]:
            c = core["core"]
            ident = None
            raw = core["pds_before"]
            # TAG-stage disagreement does not invalidate stage-local PDS
            # timing. It DOES prohibit calling the signal value an exclusive
            # metric for this draw; no such attribution is performed here.
            batch = (raw >> 16) & 8191
            state = core["scheduler_before"]
            scene = identities.get(tuple(state[3:6]))
            reason = None
            if core["scheduler_before"] != core["scheduler_after"] or core["flags"] & 2:
                reason = "scheduler changed"
            elif raw != core["pds_after"] or core["flags"] & 4:
                reason = "PDS changed"
            elif not raw & (1 << 29) or not batch:
                reason = "inactive/unmapped PDS"
            elif state[3] != pid or sample["process_id"] != pid:
                reason = "foreign process"
            elif scene is None or sample["before_us"] < scene["submission_begin_us"]:
                reason = "unmatched scene"
            elif batch > len(scene["draw_sequences"]):
                reason = "batch outside scene"
            else:
                seq = scene["draw_sequences"][batch - 1]
                d = draw_by_seq.get(seq)
                if d is None or d["fragment_shader"] is None:
                    reason = "unresolved draw/shader"
                elif sample["before_us"] < event_by_seq[seq]["before_us"]:
                    reason = "draw not yet submitted"
                else:
                    ident = (scene["begin_sequence"], seq)
            width = (core["timer_after"] - core["timer_before"]) & 0xffffffff
            if width >= 100000:
                reason = "wide timer bracket"
            if reason:
                ident = None
                reasons[reason] += 1
            elif ident is not None:
                hit[ident] += 1
            if mhz in (111, 222):
                widths.append(width / (6.920455 * mhz / 111))
            tick = (core["timer_before"] + width // 2) & 0xffffffff
            old = previous.get(c)
            previous[c] = (i, cpu, tick, mhz, ident)
            if old is None:
                continue
            oi, ocpu, otick, omhz, other = old
            gap = cpu - ocpu
            if gap <= 0 or i <= oi:
                return reject("nonmonotonic hardware samples")
            gaps.append(gap)
            why = None
            if i != oi + 1:
                why = "failed/missing sample"
            elif ident is None or other is None:
                why = "unattributed endpoint"
            elif ident[0] != other[0]:
                why = "scene transition"
            elif mhz != omhz or mhz not in (111, 222):
                why = "unsupported/changing clock"
            elif gap > max_gap_us:
                why = "excessive sampling gap"
            else:
                rate = 6.920455 * mhz / 111
                us = ((tick - otick) & 0xffffffff) / rate
                if gap * rate >= 2**32 or not .8 * gap <= us <= 1.2 * gap:
                    why = "timer discontinuity"
            if why:
                rejected[why] += gap / 4000
                continue
            def shader_key(identity):
                s = draw_by_seq[identity[1]]["fragment_shader"]
                return (s["patcher"], s["handle"], s["generation"])
            for key in (other, ident):
                durations[c, key] += us / 2
                if ident != other:
                    sensitivity[c, key] += us / 2
                d = draw_by_seq[key[1]]
                shader_times[shader_key(key)] += us / 8000
                pass_times[d["pass"] or "Unlabelled"] += us / 8000
            if shader_key(other) != shader_key(ident):
                for key in (other, ident):
                    shader_sensitivity[shader_key(key)] += us / 8000
    hit_by_sequence = {key[1]: key for key in hit}
    for d in draws:
        ident = hit_by_sequence.get(d["sequence"])
        measured = ident is not None and any((c, ident) in durations for c in range(4))
        d["sampled_fragment_ms"] = sum(durations[c, ident] for c in range(4)) / 4000 if measured else None
        d["fragment_samples"] = hit[ident] if ident is not None else 0
        d["endpoint_sensitivity_ms"] = sum(sensitivity[c, ident] for c in range(4)) / 4000 if measured else None
        d["sampled_fragment_per_core_ms"] = [durations[c, ident]/1000 if (c, ident) in durations else None for c in range(4)]
    shader_names = {(s["patcher"], s["handle"], s["generation"]): s["name"]
                    for d in draws if (s := d["fragment_shader"])}
    output.update(available=bool(durations), reason="" if durations else "no assignable timing intervals",
        matched_core_observations=sum(hit.values()), observation_rejections=dict(reasons),
        unassigned_gap_ms=dict(rejected),
        sampling_gap_us={"min": min(gaps), "max": max(gaps), "mean": sum(gaps)/len(gaps)} if gaps else None,
        max_timer_bracket_us=max(widths, default=None),
        failed_samples=sum(bool(s["result"]) for s in samples),
        draws_without_timing=sum(d['sampled_fragment_ms'] is None for d in draws),
        passes=[dict(name=k, estimated_ms=v) for k,v in pass_times.most_common()],
        shaders=[dict(identity=list(k), name=shader_names[k], estimated_ms=v,
                      endpoint_sensitivity_ms=shader_sensitivity[k]) for k,v in shader_times.most_common()])
    return output


def decode_diagnostics(events):
    samples = []
    seen = set()
    i = 0
    while i < len(events):
        e = events[i]
        if e["kind"] == 22:
            raise ValueError("orphan diagnostic core row")
        if e["kind"] != 21:
            i += 1
            continue
        key = (e["thread"], e["object"])
        if key in seen:
            raise ValueError("duplicate diagnostic sample ID")
        seen.add(key)
        sample = {"thread": key[0], "sample_id": key[1], "before_us": e["before_us"],
                  "after_us": e["after_us"], "result": e["result"],
                  "group": e["arg0"], "tag_group": e["arg1"],
                  "clock_before_mhz": e["arg2"], "clock_after_mhz": e["arg3"],
                  "process_id": e["arg4"], "driver_fingerprint": e["arg5"],
                  "abi": e["arg6"], "cores": []}
        i += 1
        if not e["result"]:
            for core in range(4):
                parts = []
                for part in range(3):
                    if i >= len(events):
                        raise ValueError("incomplete diagnostic sample")
                    row = events[i]
                    if (row["kind"], row["thread"], row["object"], row["context"], row["scope"],
                        row["before_us"], row["after_us"], row["result"]) != (
                            22, *key, core, part, e["before_us"], e["after_us"], 0):
                        raise ValueError("interrupted or mismatched diagnostic sample")
                    values = [row[f"arg{n}"] for n in range(8)]
                    if any(v > 0xffffffff for v in values):
                        raise ValueError("diagnostic word exceeds uint32")
                    parts.append(values)
                    i += 1
                sample["cores"].append({"core": core,
                    "scheduler_before": parts[0][:6], "scheduler_after": parts[1][:6],
                    "timer_before": parts[0][6], "timer_after": parts[0][7],
                    "pds_before": parts[1][6], "tag_before": parts[1][7],
                    "value": parts[2][0], "tag_after": parts[2][1],
                    "pds_after": parts[2][2], "flags": parts[2][3]})
        samples.append(sample)
    return samples


def json_numbers(value):
    """Avoid meaningless floating-point tails in the analysis output."""
    if isinstance(value, float):
        return round(value, 6)
    if isinstance(value, dict):
        return {k: json_numbers(v) for k,v in value.items()}
    if isinstance(value, list):
        return [json_numbers(v) for v in value]
    return value


def agent_report(report):
    """Versioned analysis, without duplicating raw events or hardware samples."""
    timing = report['sampled_fragment_timing']
    draws = report['draws']
    available = timing['available']
    total = sum(p['estimated_ms'] for p in timing['passes']) if available else None
    def shader_key(shader):
        return (shader['patcher'], shader['handle'], shader['generation']) if shader else None
    keys = sorted({shader_key(d['fragment_shader']) for d in draws if d['fragment_shader']})
    shader_ids = {key: f'fs{i+1}' for i,key in enumerate(keys)}
    shader_costs = {tuple(s['identity']): s for s in timing['shaders']}
    groups, passes, shaders = {}, {}, {}
    for d in draws:
        name = d['pass'] or 'Unlabelled'
        key = shader_key(d['fragment_shader'])
        group_key = (name, d['name'], key)
        for collection, identity, fields in (
                (passes, name, dict(name=name)),
                (shaders, key, dict(id=shader_ids.get(key), name=(d['fragment_shader'] or {}).get('name') or None)),
                (groups, group_key, dict(pass_name=name, name=d['name'] or None,
                                        fragment_shader_id=shader_ids.get(key)))):
            row = collection.setdefault(identity, dict(**fields, draw_count=0,
                timed_draw_count=0, estimated_fragment_ms=None, observed_frames=set()))
            row['draw_count'] += 1
            if d['frame'] is not None:
                row['observed_frames'].add((d['thread'],d['frame']))
            cost = d.get('sampled_fragment_ms')
            if cost is not None:
                row['timed_draw_count'] += 1
                row['estimated_fragment_ms'] = (row['estimated_fragment_ms'] or 0) + cost
    def ranked(collection):
        rows = list(collection.values())
        rows.sort(key=lambda r: (r['estimated_fragment_ms'] is None,
                                -(r['estimated_fragment_ms'] or 0), str(r.get('name'))))
        for i,row in enumerate(rows,1):
            row['rank'] = i if row['estimated_fragment_ms'] is not None else None
            row['observed_frame_count'] = len(row.pop('observed_frames'))
            row['untimed_draw_count'] = row['draw_count'] - row['timed_draw_count']
            cost = row['estimated_fragment_ms']
            row['share_of_attributed_time_pct'] = 100*cost/total if total and cost is not None else None
        return rows
    for key,row in shaders.items():
        row['endpoint_sensitivity_ms'] = shader_costs.get(key,{}).get('endpoint_sensitivity_ms')
    pass_rows = ranked(passes)
    top = pass_rows[0] if available and pass_rows else None
    failed = sum(bool(s['result']) for s in report['diagnostics'])
    untimed = sum(d.get('sampled_fragment_ms') is None for d in draws)
    return dict(schema='psp2-gpuprof.analysis', schema_version=1,
        status='estimated' if available else 'timing_unavailable',
        timing_unavailable_reason=None if available else timing['reason'],
        measurement=dict(metric='sampled_fragment_processing', unit='ms',
            scope='capture_total', core_aggregation='four_core_mean',
            model=timing['model'], missing_values=None,
            percentage_denominator='attributed_fragment_time_only',
            shader_id_scope='capture_local', unsampled_work_included=False),
        summary=dict(dominant_pass=top['name'] if top else None,
                     attributed_fragment_ms=total,
                     dominant_pass_share_pct=top['share_of_attributed_time_pct'] if top else None),
        capture=dict(events=report['events'], dropped_events=report['dropped'],
            draws=len(draws), scenes=len(report['scenes']),
            closed_scenes=sum(s['closed'] for s in report['scenes']),
            hardware_samples=len(report['diagnostics']), transfers=len(report['transfers']),
            presentations=len(report['presentations'])),
        coverage=dict(timed_draws=len(draws)-untimed, untimed_draws=untimed,
            failed_samples=failed, matched_core_observations=timing.get('matched_core_observations',0),
            rejected_core_observations=timing.get('observation_rejections',{}),
            unassigned_cpu_gap_ms_by_reason=timing.get('unassigned_gap_ms',{}),
            sampling_gap_us=timing.get('sampling_gap_us'),
            max_timer_bracket_us=timing.get('max_timer_bracket_us')),
        passes=pass_rows, fragment_shaders=ranked(shaders), draw_groups=ranked(groups),
        warnings=[dict(message=message,count=count) for message,count in sorted(report['warnings'].items())])


def format_report(report):
    """Plain-text report; capture labels never emit terminal control codes."""
    rule = "─" * 76
    lines = ["", "  GPU Profiling Summary", "  " + rule]
    timing = report["sampled_fragment_timing"]
    if timing["available"]:
        lines += ["", "  Fragment processing · capture totals · four-core average", "",
                  f"  {'Pass':27}  {'Relative time':28}  {'Time':>13}"]
        passes = sorted(timing["passes"], key=lambda p: p["estimated_ms"], reverse=True)
        longest = max((p["estimated_ms"] for p in passes), default=0)
        for p in passes:
            name = ascii(p["name"])[1:-1]
            if len(name) > 27:
                name = name[:26] + "…"
            size = max(1, round(28*p["estimated_ms"]/longest)) if longest and p["estimated_ms"] > 0 else 0
            bar = "█" * size
            value = f"{p['estimated_ms']:,.3f} ms"
            lines.append(f"  {name:27}  {bar:28}  {value:>13}")
        lines += ["", "  Estimated timings. Unsampled work is excluded."]
    else:
        lines += ["", "  Fragment timing unavailable", "  " + ascii(timing["reason"])[1:-1]]

    lines += ["", "  " + rule, "  Capture", ""]
    def pair(left, value, right, other):
        lines.append(f"  {left:20} {value:>13}    {right:20} {other:>13}")
    pair("Recorded events", f"{report['events']:,}", "Draw calls", f"{len(report['draws']):,}")
    pair("Hardware samples", f"{len(report['diagnostics']):,}", "Presentations", f"{len(report['presentations']):,}")
    pair("Scenes closed", f"{sum(s['closed'] for s in report['scenes']):,} / {len(report['scenes']):,}",
         "Transfer calls", f"{len(report['transfers']):,}")
    if "draws_without_timing" in timing:
        lines += ["", "  Timing coverage", ""]
        pair("Draws without timing", f"{timing['draws_without_timing']:,} / {len(report['draws']):,}",
             "Failed samples", f"{timing['failed_samples']:,}")
        gaps = sum(timing["unassigned_gap_ms"].values())
        lines.append(f"  Unassigned gaps      {gaps:>10,.3f} ms   (CPU-clock envelope)")
    if report["warnings"]:
        lines += ["", "  Warnings", ""]
        for warning, count in report["warnings"].items():
            lines.append(f"  {count:>6,} × {ascii(warning)[1:-1]}")
    lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--json", action="store_true", help="structured analysis for agents (JSON schema v1)")
    args = parser.parse_args()
    try:
        with args.capture.open(encoding="utf-8", errors="replace", newline="") as stream:
            report = summarize(*read_capture(stream))
    except (OSError, ValueError, csv.Error) as exc:
        if args.json:
            print(json.dumps(dict(schema='psp2-gpuprof.analysis', schema_version=1,
                status='error', error=dict(code='capture_read_error', message=str(exc))), ensure_ascii=True))
            return 2
        parser.exit(2, f"gpuprof: {exc}\n")
    if args.json:
        print(json.dumps(json_numbers(agent_report(report)), indent=2, allow_nan=False))
    else:
        print(format_report(report))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
