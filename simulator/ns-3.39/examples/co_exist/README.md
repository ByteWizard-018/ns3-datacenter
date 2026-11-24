# AI collective and TCP traffic in a shared data-centre fabric

UGRC (CS4900) project, IIT Madras, July to November 2026.
Yanadi Bharath (CS23B066), guided by Prof. Krishna Sivalingam.

A Ring AllReduce over RDMA and web-search TCP flows share a leaf-spine fabric in ns-3. The experiment measures how much each slows the other, then tests switch scheduling policies that decide which queue sends next at a shared port. The main policy, the waiting limit, serves AI first unless the oldest TCP packet has waited longer than D.

## Changes to the simulator

Two parts of the simulator changed, 61 lines in total. Everything else is new files in this folder.

| File | Change |
|---|---|
| `src/network/utils/broadcom-egress-queue.h`, `.cc` | Egress scheduler policies: AI first, fixed AI weight w, waiting limit D, cap, AIMD weight. Records each packet's arrival time and counts which queue is served while AI waits. |
| `src/point-to-point/model/switch-node.h`, `.cc` | Counts packets dropped by buffer admission, split into lossy (TCP) and lossless. |

## Files in `verify_aivsTcp/`

| File | Purpose |
|---|---|
| `verify_aivsTcp.cc` | The simulation: topology, RDMA and switch setup, Ring AllReduce, TCP traffic, timing patterns, policy flags |
| `CMakeLists.txt` | Builds the example |
| `cdf.c`, `cdf.h`, `leaf-spine.txt`, `websearch.txt` | Copied unchanged from `examples/Reverie` |
| `run-experiment.sh` | Baseline: calibrates the AI slot, then runs AI alone, TCP alone and both for every pattern and load |
| `run-policy.sh` | Reruns the AI + TCP runs under one switch policy |
| `run-profile.sh` | TCP load that changes during the run (0.2, 0.6, 0.2) |
| `run-reverie.sh` | Reverie comparison: two buffer schemes, two policies, two buffer sizes |
| `summarize.py` | Summary table of one results folder |
| `phase_summary.py` | Per-phase numbers for the changing-load runs |
| `make_figures.py` | All report figures |
| `export_figure_data.py` | The data behind each figure and table, as CSV |
| `figure_data/` | Output of `export_figure_data.py` |

The simulation code reuses large parts of `examples/Reverie/test-mixed.cc`, `examples/Reverie/reverie-evaluation-sigcomm2023.cc` and `examples/ABM/abm-evaluation.cc`. Section labels in `verify_aivsTcp.cc` say which part came from where.

## Setup

| Item | Value |
|---|---|
| Topology | 16 leaves x 16 servers, 4 spines, 25 Gbps links, 2 us delay, 4:1 oversubscription |
| AI job | Ring AllReduce over 16 ranks (servers 0-7 and 16-23, interleaved), 64 MB in 4 MB chunks, 30 steps, DCQCN, switch queue 3 (lossless) |
| TCP | Servers 8-15 and 24-31, Poisson arrivals, web-search sizes, load 0.3 or 0.5 of the uplink, CUBIC or DCTCP, switch queue 1 (lossy) |
| Timing patterns | 1 disjoint, 2 random, 3 sync, 4 strict disjoint |
| Switch buffer | 12 MiB shared, Dynamic Thresholds (or Reverie) |

## Build

```bash
cd simulator/ns-3.39
./configure.sh
./ns3 build verify_aivsTcp
```

## Run

Run every script from `simulator/ns-3.39`. One run takes about an hour; `PAR` sets how many run at once.

```bash
E=examples/co_exist/verify_aivsTcp

# Baseline (round robin), CUBIC and DCTCP
bash $E/run-experiment.sh
TCPCC=dctcp bash $E/run-experiment.sh

# One policy, random and sync patterns, load 0.5
NAME=aifirst  FLAGS="--aiFirst=1"                      TCPCC=dctcp bash $E/run-policy.sh
NAME=w4       FLAGS="--aiWeight=4"                     TCPCC=dctcp bash $E/run-policy.sh
NAME=aimd100  FLAGS="--aimdTarget=100"                 TCPCC=dctcp bash $E/run-policy.sh
NAME=wait100  FLAGS="--tcpWaitLimit=100"               TCPCC=dctcp bash $E/run-policy.sh
NAME=wait100w4 FLAGS="--tcpWaitLimit=100 --tcpWaitCap=4" bash $E/run-policy.sh   # cap, CUBIC

# Other seeds and loads
SEED=11 TCPCC=dctcp PATTERNS="2 3" LOADS="0.3 0.5" bash $E/run-experiment.sh
SEED=11 NAME=wait100 FLAGS="--tcpWaitLimit=100" TCPCC=dctcp PATTERNS="2 3" LOADS="0.3 0.5" bash $E/run-policy.sh

# Changing TCP load, and the Reverie comparison
TCPCC=dctcp bash $E/run-profile.sh
TCPCC=dctcp bash $E/run-reverie.sh

# Tables, figures and figure data
python3 $E/summarize.py $E/results_dctcp_wait100
python3 $E/phase_summary.py $E/results_dctcp_profile
cd $E && python3 make_figures.py && python3 export_figure_data.py
```

Results go to `results[_dctcp][_s<seed>][_<NAME>]/`, one folder per run inside.

## Options added to `verify_aivsTcp`

| Option | Meaning |
|---|---|
| `--pattern` | TCP timing: 1 disjoint, 2 random, 3 sync, 4 strict disjoint |
| `--ai` | 1 runs the AllReduce, 0 keeps its schedule with no AI traffic (TCP-alone run) |
| `--aiIters`, `--aiStart`, `--aiWindow`, `--aiPeriod` | AllReduce count, start time, slot length and period (s) |
| `--ringChunkBytes` | Bytes per ring flow per step (4000000 gives a 64 MB AllReduce) |
| `--loadProfile` | TCP load per equal phase, for example `0.2,0.6,0.2` (random pattern only) |
| `--tcpDctcp` | 1 uses DCTCP with ECN, 0 uses CUBIC |
| `--aiFirst` | 1 always serves the AI queue before TCP |
| `--aiWeight` | Fixed weight w: up to w AI packets per TCP packet |
| `--tcpWaitLimit` | Waiting limit D (us): AI first unless the oldest TCP packet waited longer than D |
| `--tcpWaitCap` | With the waiting limit, a late TCP packet goes only after w AI packets in a row |
| `--aimdTarget` | AIMD weight target D (us): weight +1 while TCP's smoothed wait is below D, halved above |
| `--reverieSetup` | PFC headroom only on lossless queues and one shared pool, as in Reverie's evaluation |
| `--bufferBytes` | Switch buffer size in bytes (0 uses the config file) |
| `--gamma` | Reverie's low-pass filter weight |
| `--cctFile`, `--tcpFctFile` | Output files |

## Output of one run

| File | Contents |
|---|---|
| `cct.txt` | One line per AllReduce: iteration, start, end, duration (s); iteration 0 is warm-up |
| `tcp-fct.txt` | One line per TCP flow: finish time, size, completion time (ns), ideal time, slowdown |
| `rdma-fct.txt` | One line per RDMA flow (the 4 MB ring transfers) |
| `pfc.txt` | One line per PFC pause event |
| `run.log` | Flow count, `DROPS tcp=... lossless=...`, and `WHILE_AI_WAITING` turn counts when a policy is on |

AI slowdown is the mean AllReduce time over iterations 1 to 3 divided by the AI-alone time. The TCP P99 ratio is the 99th-percentile completion time of flows up to 100 KB with AI, divided by the same with TCP alone.

## Credits

Built on [ns3-datacenter](https://github.com/inet-tub/ns3-datacenter) by Vamsi Addanki and others, which includes the Reverie buffer model. ns-3 and the copied example code are under GPL v2.
