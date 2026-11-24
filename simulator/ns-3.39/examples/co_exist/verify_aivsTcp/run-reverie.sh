#!/bin/bash
# Reverie comparison: buffer {DT, Reverie} x policy {RR, wait 100 us} x size {12 MiB, 2.56 MB}.
# From the ns-3.39 folder:  TCPCC=dctcp bash examples/co_exist/verify_aivsTcp/run-reverie.sh
D=examples/co_exist/verify_aivsTcp
BIN=${BIN:-build/examples/co_exist/ns3.39-verify_aivsTcp-optimized}
TCPCC=${TCPCC:-cubic}
S=""; DC=0; [ "$TCPCC" = dctcp ] && { S="_dctcp"; DC=1; }
SEED=${SEED:-7}; [ "$SEED" != 7 ] && S="${S}_s$SEED"
BASE=$D/results$S
PATTERNS=${PATTERNS:-"2 3"}; LOAD=${LOAD:-0.5}; PAR=${PAR:-7}; ALGO=${ALGO:-1}; WIN=${WIN:-0}
read AI_START P ITERS W < $BASE/params.txt
END=$(awk -v s=$AI_START -v n=$ITERS -v p=$P 'BEGIN{printf "%.6f", s+n*p+p}')
COMMON="--conf=examples/Reverie/config-workload.txt --alphasFile=examples/Reverie/alphas --cdfFileName=$D/websearch.txt --LEAF_COUNT=16 --SPINE_COUNT=4 --SERVER_COUNT=16 --LEAF_SERVER_CAPACITY=25 --SPINE_LEAF_CAPACITY=25 --algorithm=$ALGO --windowCheck=$WIN --randomSeed=$SEED --aiStart=$AI_START --tcpDctcp=$DC --reverieSetup=1 --aiIters=$ITERS --aiWindow=$W --aiPeriod=$P --END_TIME=$END"

run() {
  dir=$1; name=$2; shift 2
  mkdir -p $dir/$name
  $BIN $COMMON --fctOutFile=$dir/$name/rdma-fct.txt --pfcOutFile=$dir/$name/pfc.txt --torOutFile=$dir/$name/tor.txt \
       --cctFile=$dir/$name/cct.txt --tcpFctFile=$dir/$name/tcp-fct.txt "$@" > $dir/$name/run.log 2>&1
}
export -f run; export BIN COMMON

JOBS=$D/results${S}_rv_jobs.txt; : > $JOBS
# add <dir> <name> <flags...>: one job line, skipped if that run already finished
add() {
  dir=$1; name=$2; shift 2
  grep -qs '^DROPS' $dir/$name/run.log && return
  echo "$dir $name $*" | tr -s ' ' | sed 's/ $//' >> $JOBS
}
for size in 12MiB 2.56MB; do
  [ $size = 12MiB ] && BUF="" || BUF="--bufferBytes=2560000"
  for model in dt reverie; do
    [ $model = dt ] && M="" || M="--bufferModel=reverie --bufferalgIngress=111 --bufferalgEgress=111"
    OUT=$D/results${S}_rv_${size}_${model}; POL=${OUT}_wait100
    mkdir -p $OUT $POL
    cp $BASE/params.txt $OUT/; cp $BASE/params.txt $POL/
    ln -sfn ../$(basename $OUT)/ai-alone $POL/ai-alone
    add $OUT ai-alone --pattern=2 --load=0 --ai=1 $BUF $M
    for pat in $PATTERNS; do
      ln -sfn ../$(basename $OUT)/tcp-alone_p${pat}_$LOAD $POL/tcp-alone_p${pat}_$LOAD
      add $OUT tcp-alone_p${pat}_$LOAD --ai=0 --pattern=$pat --load=$LOAD $BUF $M
      add $OUT both_p${pat}_$LOAD --ai=1 --pattern=$pat --load=$LOAD $BUF $M
      add $POL both_p${pat}_$LOAD --ai=1 --pattern=$pat --load=$LOAD --tcpWaitLimit=100 $BUF $M
    done
  done
done
echo "Reverie comparison: $(wc -l < $JOBS) runs, $PAR at a time..."
xargs -P $PAR -L 1 bash -c 'run "$@"' _ < $JOBS
echo "Done. Now: for f in $D/results${S}_rv_*/; do python3 $D/summarize.py \$f; done"
