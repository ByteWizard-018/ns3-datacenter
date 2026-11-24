#!/bin/bash
# Baseline runs. From the ns-3.39 folder:
#   bash examples/co_exist/verify_aivsTcp/run-experiment.sh
D=examples/co_exist/verify_aivsTcp
BIN=build/examples/co_exist/ns3.39-verify_aivsTcp-optimized
TCPCC=${TCPCC:-cubic} # cubic or dctcp
S=""; DC=0; [ "$TCPCC" = dctcp ] && { S="_dctcp"; DC=1; }
SEED=${SEED:-7}; [ "$SEED" != 7 ] && S="${S}_s$SEED"
OUT=$D/results$S
ITERS=4; AI_START=0.005
# RDMA: DCQCN, no sender window
ALGO=${ALGO:-1}; WIN=${WIN:-0}
COMMON="--conf=examples/Reverie/config-workload.txt --alphasFile=examples/Reverie/alphas --cdfFileName=$D/websearch.txt --LEAF_COUNT=16 --SPINE_COUNT=4 --SERVER_COUNT=16 --LEAF_SERVER_CAPACITY=25 --SPINE_LEAF_CAPACITY=25 --algorithm=$ALGO --windowCheck=$WIN --randomSeed=$SEED --aiStart=$AI_START --tcpDctcp=$DC"

# run <name> <flags...>
run() {
  name=$1; shift
  mkdir -p $OUT/$name
  $BIN $COMMON --fctOutFile=$OUT/$name/rdma-fct.txt --pfcOutFile=$OUT/$name/pfc.txt --torOutFile=$OUT/$name/tor.txt \
       --cctFile=$OUT/$name/cct.txt --tcpFctFile=$OUT/$name/tcp-fct.txt "$@" > $OUT/$name/run.log 2>&1
}
export -f run; export BIN COMMON OUT

rm -rf $OUT; mkdir -p $OUT

echo "Calibrating: one AllReduce, AI alone..."
run calibration --pattern=2 --load=0 --ai=1 --aiIters=1 --END_TIME=1
CCT=$(awk '{print $4}' $OUT/calibration/cct.txt)
read W P END <<< $(awk -v c=$CCT -v s=$AI_START -v n=$ITERS 'BEGIN{w=1.1*c; p=2*w; printf "%.6f %.6f %.6f", w, p, s+n*p+p}')
END4=$(awk -v e=$END 'BEGIN{printf "%.6f", 3*e}')
echo "CCT=$CCT s -> slot=$W s, period=$P s, simulation end=$END s"
echo "$AI_START $P $ITERS $W" > $OUT/params.txt

# one line per run: <name> <flags>
JOBS=$OUT/jobs.txt
T="--aiIters=$ITERS --aiWindow=$W --aiPeriod=$P --END_TIME=$END"
echo "ai-alone --pattern=2 --load=0 --ai=1 $T" > $JOBS
for pat in ${PATTERNS:-1 2 3 4}; do
  for load in ${LOADS:-0.3 0.5}; do
    TT=$T; [ $pat = 4 ] && TT="--aiIters=$ITERS --aiWindow=$W --aiPeriod=$P --END_TIME=$END4"
    echo "tcp-alone_p${pat}_${load} --ai=0 --pattern=$pat --load=$load $TT" >> $JOBS
    echo "both_p${pat}_${load} --ai=1 --pattern=$pat --load=$load $TT" >> $JOBS
  done
done

PAR=${PAR:-3} # parallel runs
echo "Running $(wc -l < $JOBS) simulations, $PAR at a time..."
xargs -P $PAR -L 1 bash -c 'run "$@"' _ < $JOBS
echo "Done. Now: python3 $D/summarize.py"
