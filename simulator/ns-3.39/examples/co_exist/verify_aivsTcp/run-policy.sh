#!/bin/bash
# AI+TCP runs under one switch policy, reusing the baseline's AI-alone and TCP-alone runs.
# From the ns-3.39 folder:  NAME=wait100 FLAGS="--tcpWaitLimit=100" TCPCC=dctcp bash examples/co_exist/verify_aivsTcp/run-policy.sh
D=examples/co_exist/verify_aivsTcp
BIN=build/examples/co_exist/ns3.39-verify_aivsTcp-optimized
TCPCC=${TCPCC:-cubic} # cubic or dctcp
S=""; DC=0; [ "$TCPCC" = dctcp ] && { S="_dctcp"; DC=1; }
SEED=${SEED:-7}; [ "$SEED" != 7 ] && S="${S}_s$SEED"
BASE=$D/results$S
NAME=${NAME:-aifirst}; FLAGS=${FLAGS:---aiFirst=1}
PATTERNS=${PATTERNS:-"2 3"}; LOADS=${LOADS:-"0.5"}; PAR=${PAR:-3}
OUT=$D/results${S}_$NAME
ALGO=${ALGO:-1}; WIN=${WIN:-0}
read AI_START P ITERS W < $BASE/params.txt
END=$(awk -v s=$AI_START -v n=$ITERS -v p=$P 'BEGIN{printf "%.6f", s+n*p+p}')
COMMON="--conf=examples/Reverie/config-workload.txt --alphasFile=examples/Reverie/alphas --cdfFileName=$D/websearch.txt --LEAF_COUNT=16 --SPINE_COUNT=4 --SERVER_COUNT=16 --LEAF_SERVER_CAPACITY=25 --SPINE_LEAF_CAPACITY=25 --algorithm=$ALGO --windowCheck=$WIN --randomSeed=$SEED --aiStart=$AI_START --tcpDctcp=$DC $FLAGS"

run() {
  name=$1; shift
  mkdir -p $OUT/$name
  $BIN $COMMON --fctOutFile=$OUT/$name/rdma-fct.txt --pfcOutFile=$OUT/$name/pfc.txt --torOutFile=$OUT/$name/tor.txt \
       --cctFile=$OUT/$name/cct.txt --tcpFctFile=$OUT/$name/tcp-fct.txt "$@" > $OUT/$name/run.log 2>&1
}
export -f run; export BIN COMMON OUT

rm -rf $OUT; mkdir -p $OUT
cp $BASE/params.txt $OUT/
ln -s ../results$S/ai-alone $OUT/ai-alone
for d in $BASE/tcp-alone_*; do ln -s ../results$S/$(basename $d) $OUT/$(basename $d); done

JOBS=$OUT/jobs.txt; : > $JOBS
for pat in $PATTERNS; do
  for load in $LOADS; do
    E=$END; [ $pat = 4 ] && E=$(awk -v e=$END 'BEGIN{printf "%.6f", 3*e}')
    echo "both_p${pat}_${load} --ai=1 --pattern=$pat --load=$load --aiIters=$ITERS --aiWindow=$W --aiPeriod=$P --END_TIME=$E" >> $JOBS
  done
done
echo "Policy '$NAME' ($FLAGS): $(wc -l < $JOBS) runs, $PAR at a time..."
xargs -P $PAR -L 1 bash -c 'run "$@"' _ < $JOBS
echo "Done. Now: python3 $D/summarize.py $OUT"
