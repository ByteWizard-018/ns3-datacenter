#!/usr/bin/env python3
"""Writes the data behind each report figure and table as CSV, using make_figures.py's helpers.

    python3 export_figure_data.py               # writes figure_data/*.csv
    python3 export_figure_data.py other_dir     # writes them somewhere else
"""
import os, sys, csv, math
import make_figures as mf

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(mf.HERE, "figure_data")
os.makedirs(OUT, exist_ok=True)
FAMILY = {"rr": "Round robin", "w2": "Fixed weight", "w4": "Fixed weight", "w8": "Fixed weight", "aifirst": "AI first",
          "aimd50": "AIMD weight", "aimd100": "AIMD weight", "aimd200": "AIMD weight"}


def write(name, header, rows):
    path = os.path.join(OUT, name + ".csv")
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for r in rows:
            w.writerow([f"{v:.4f}" if isinstance(v, float) else v for v in r])
    print("wrote", path, f"({len(rows)} rows)")


def r(x, nd=4):
    return x if not isinstance(x, float) or math.isnan(x) else round(x, nd)


# Fig 2: timing patterns (the AI schedule used in every run)
def d_patterns():
    p = mf.lines(os.path.join(mf.F("results"), "params.txt"))[0]
    write("fig02_patterns", ["ai_start_s", "period_s", "iterations", "slot_s"], [[float(p[0]), float(p[1]), int(p[2]), float(p[3])]])


# Fig 3: baseline, CUBIC, round robin
def d_baseline():
    names = {1: "disjoint", 2: "random", 3: "sync", 4: "strict disjoint"}
    rows = []
    alone = mf.cct_ms("results", "ai-alone")
    for p in (1, 2, 3, 4):
        for load in ("0.3", "0.5"):
            run = f"both_p{p}_{load}"
            rows.append([names[p], load, mf.cct_ms("results", run), alone, mf.slowdown("results", run),
                         mf.short_p99_us("results", run), mf.short_p99_us("results", f"tcp-alone_p{p}_{load}"),
                         mf.p99_ratio("results", p, load), mf.pfc("results", run)])
    write("fig03_baseline", ["pattern", "tcp_load", "allreduce_ms", "allreduce_alone_ms", "ai_slowdown",
                             "tcp_short_p99_us", "tcp_alone_short_p99_us", "tcp_p99_ratio", "pfc_pauses"], rows)


# Fig 4: policies with CUBIC (random, load 0.5)
def d_cubic():
    cases = [("results", "round robin"), ("results_w2", "w=2"), ("results_w4", "w=4"), ("results_w8", "w=8"),
             ("results_wait50", "wait D=50us"), ("results_wait100", "wait D=100us"), ("results_wait200", "wait D=200us"),
             ("results_wait100w2", "wait D=100us + cap 2"), ("results_wait100w4", "wait D=100us + cap 4"),
             ("results_wait100w8", "wait D=100us + cap 8")]
    rows = [[name, mf.slowdown(f, "both_p2_0.5"), mf.p99_ratio(f, 2, "0.5"), mf.pfc(f, "both_p2_0.5")]
            for f, name in cases if mf.have((f, "both_p2_0.5"))]
    write("fig04_cubic", ["policy", "ai_slowdown", "tcp_p99_ratio", "pfc_pauses"], rows)


# Fig 5 and A4: AI vs TCP trade-off with DCTCP (random = pattern 2, sync = pattern 3; load 0.5)
def d_tradeoff(pat, name):
    rows = []
    for run in ["rr", "w2", "w4", "w8", "aifirst", "aimd50", "aimd100", "aimd200",
                "wait50", "wait100", "wait150", "wait200", "wait300", "wait500"]:
        p = mf.tradeoff_point(run, pat)
        if p:
            fam = FAMILY.get(run, "Waiting limit")
            rows.append([fam, run, p[0], p[1]])
    write(name, ["family", "policy", "ai_slowdown", "tcp_p99_ratio"], rows)


# Fig 6 and Table 1: the waiting limit D (DCTCP, random, load 0.5)
def d_dsweep():
    rows = []
    for d in (50, 100, 150, 200, 300, 500):
        f = f"results_dctcp_wait{d}"
        if mf.have((f, "both_p2_0.5")):
            rows.append([d, mf.slowdown(f, "both_p2_0.5"), mf.p99_ratio(f, 2, "0.5"), mf.pfc(f, "both_p2_0.5")])
    write("fig06_dsweep", ["D_us", "ai_slowdown", "tcp_p99_ratio", "pfc_pauses"], rows)
    refs = [[n, *mf.tradeoff_point(n, 2)] for n in ("w4", "w8") if mf.tradeoff_point(n, 2)]
    write("fig06_dsweep_fixed_reference", ["policy", "ai_slowdown", "tcp_p99_ratio"], refs)


# Fig 7: three seeds (one row per seed; the figure shows mean, min and max)
def d_seeds():
    seeds = [("results_dctcp", 7), ("results_dctcp_s11", 11), ("results_dctcp_s13", 13)]
    pols = [("", "round robin"), ("_w4", "w=4"), ("_aimd100", "AIMD 100"), ("_wait100", "waiting limit 100")]
    rows = []
    for suffix, name in pols:
        for pat, load in ((2, "0.3"), (2, "0.5"), (3, "0.3"), (3, "0.5")):
            for base, seed in seeds:
                f, run = base + suffix, f"both_p{pat}_{load}"
                if mf.have((f, run)):
                    rows.append([name, "random" if pat == 2 else "sync", load, seed,
                                 mf.slowdown(f, run), mf.short_p99_us(f, run), mf.short_p99_us(base, f"tcp-alone_p{pat}_{load}")])
    write("fig07_seeds", ["policy", "pattern", "tcp_load", "seed", "ai_slowdown", "tcp_short_p99_us", "tcp_alone_short_p99_us"], rows)


# Fig 8: TCP load changes during the run (one row per seed and phase; the figure shows the mean over seeds)
def d_profile():
    folders = [("results_dctcp_profile", 7), ("results_dctcp_s11_profile", 11), ("results_dctcp_s13_profile", 13)]
    rows = []
    for run in ("w4", "w8", "aimd100", "wait100", "wait200", "tcp-alone"):
        for f, seed in folders:
            if mf.have((f, run), (f, "wait100"), (f, "w8"), (f, "tcp-alone")):
                loads, ai, tcp = mf.phase_values(f, run)
                for i, load in enumerate(loads):
                    rows.append([run, seed, i + 1, load, "" if run == "tcp-alone" else ai[i], tcp[i]])
    write("fig08_profile", ["policy", "seed", "phase", "tcp_load", "ai_slowdown", "tcp_short_p99_us"], rows)


# Fig 9 and Table 2: Reverie comparison (DCTCP, random, load 0.5)
def d_reverie():
    cells = [("_dt", "default (Dynamic Thresholds)", "round robin"), ("_reverie", "Reverie", "round robin"),
             ("_dt_wait100", "default (Dynamic Thresholds)", "waiting limit 100"), ("_reverie_wait100", "Reverie", "waiting limit 100")]
    rows = []
    for size in ("12MiB", "2.56MB"):
        for suf, scheme, pol in cells:
            f = f"results_dctcp_rv_{size}{suf}"
            if mf.have((f, "both_p2_0.5")):
                rows.append([size, scheme, pol, mf.slowdown(f, "both_p2_0.5"), mf.short_p99_us(f, "both_p2_0.5"),
                             mf.pfc(f, "both_p2_0.5"), mf.drops(f, "both_p2_0.5")])
    write("fig09_reverie", ["buffer", "buffer_scheme", "policy", "ai_slowdown", "tcp_short_p99_us", "pfc_pauses", "tcp_drops"], rows)


# Fig A1: distribution of short-flow completion times (201 quantile points per run)
def d_cdf():
    runs = [("results_dctcp", "tcp-alone_p2_0.5", "TCP alone"), ("results_dctcp", "both_p2_0.5", "round robin"),
            ("results_dctcp_w4", "both_p2_0.5", "w=4"), ("results_dctcp_aimd100", "both_p2_0.5", "AIMD 100"),
            ("results_dctcp_wait100", "both_p2_0.5", "waiting limit 100")]
    rows = []
    for f, run, name in runs:
        if not mf.have((f, run)):
            continue
        v = sorted(x[2] for x in mf.flows(f, run) if x[1] <= mf.SHORT)
        for k in range(201):
            q = k / 200
            rows.append([name, q, v[max(0, math.ceil(q * len(v)) - 1)]])
    write("figA1_cdf", ["policy", "fraction_of_flows", "short_flow_fct_us"], rows)


# Fig A2: long flows (> 1 MB)
def d_longflows():
    runs = [("results_dctcp", "tcp-alone_p2_0.5", "TCP alone"), ("results_dctcp", "both_p2_0.5", "round robin"),
            ("results_dctcp_w4", "both_p2_0.5", "w=4"), ("results_dctcp_w8", "both_p2_0.5", "w=8"),
            ("results_dctcp_aimd100", "both_p2_0.5", "AIMD 100"), ("results_dctcp_wait100", "both_p2_0.5", "waiting limit 100"),
            ("results_dctcp_wait200", "both_p2_0.5", "waiting limit 200"), ("results_dctcp_aifirst", "both_p2_0.5", "AI first")]
    rows = [[name, mf.long_mean_ms(f, run), sum(1 for x in mf.flows(f, run) if x[1] > mf.LONG)]
            for f, run, name in runs if mf.have((f, run))]
    write("figA2_longflows", ["policy", "long_flow_mean_fct_ms", "long_flows"], rows)


# Fig A3: every AllReduce iteration
def d_iterations():
    runs = [("results_dctcp", "ai-alone", "AI alone"), ("results_dctcp", "both_p2_0.5", "round robin"),
            ("results_dctcp_w4", "both_p2_0.5", "w=4"), ("results_dctcp_aimd100", "both_p2_0.5", "AIMD 100"),
            ("results_dctcp_wait100", "both_p2_0.5", "waiting limit 100")]
    rows = []
    for f, run, name in runs:
        if mf.have((f, run)):
            for row in mf.lines(os.path.join(mf.F(f), run, "cct.txt")):
                rows.append([name, int(row[0]), float(row[1]), float(row[2]), 1000 * float(row[3])])
    write("figA3_iterations", ["policy", "iteration", "start_s", "end_s", "allreduce_ms"], rows)


# Table 3: all TCP flow sizes and the AI transfer tail (DCTCP, random, load 0.5)
def d_allflows():
    def ai_transfers_ms(f, run):
        path = os.path.join(mf.F(f), run, "rdma-fct.txt")
        return [float(l.split()[1]) / 1e6 for l in open(path) if l.startswith("FCT")] if os.path.exists(path) else []
    runs = [("results_dctcp", "tcp-alone_p2_0.5", "TCP alone"), ("results_dctcp", "both_p2_0.5", "round robin"),
            ("results_dctcp_aifirst", "both_p2_0.5", "AI first"), ("results_dctcp_w4", "both_p2_0.5", "w=4"),
            ("results_dctcp_w8", "both_p2_0.5", "w=8"), ("results_dctcp_aimd100", "both_p2_0.5", "AIMD 100"),
            ("results_dctcp_wait100", "both_p2_0.5", "waiting limit 100"), ("results_dctcp_wait200", "both_p2_0.5", "waiting limit 200")]
    rows = []
    for f, run, name in runs:
        if not mf.have((f, run)):
            continue
        fl = mf.flows(f, run)
        ms = lambda sel: [x[2] / 1000 for x in fl if sel(x[1])]
        ai = ai_transfers_ms(f, run) if run.startswith("both") else []
        rows.append([name, mf.p99(ms(lambda s: s <= mf.SHORT)), mf.p99(ms(lambda s: mf.SHORT < s <= mf.LONG)),
                     mf.p99(ms(lambda s: True)), mf.long_mean_ms(f, run), mf.p99(ai) if ai else ""])
    write("table3_allflows", ["policy", "tcp_short_p99_ms", "tcp_medium_p99_ms", "tcp_all_p99_ms", "tcp_long_mean_ms",
                              "ai_transfer_p99_ms"], rows)


if __name__ == "__main__":
    for job in (d_patterns, d_baseline, d_cubic, lambda: d_tradeoff(2, "fig05_tradeoff"), d_dsweep, d_seeds,
                d_profile, d_reverie, d_cdf, d_longflows, d_iterations, lambda: d_tradeoff(3, "figA4_tradeoff_sync"),
                d_allflows):
        try:
            job()
        except Exception as e:
            print("FAILED", getattr(job, "__name__", "export"), "->", e)
    print("CSV files are in", OUT)
