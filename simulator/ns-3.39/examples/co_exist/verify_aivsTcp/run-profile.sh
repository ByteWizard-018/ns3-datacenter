#!/bin/bash
# Changing TCP load (--loadProfile), each policy. From the ns-3.39 folder:
#   TCPCC=dctcp bash examples/co_exist/verify_aivsTcp/run-profile.sh
#   python3 examples/co_exist/verify_aivsTcp/phase_summary.py examples/co_exist/verify_aivsTcp/results_dctcp_profile
D=examples/co_exist/verify_aivsTcp
BIN=build/examples/co_exist/ns3.39-verify_aivsTcp-optimized
TCPCC=${TCPCC:-cubic}
S=""; DC=0; [ "$TCPCC" = dctcp ] && { S="_dctcp"; DC=1; }
SEED=${SEED:-7}; [ "$SEED" != 7 ] && S="${S}_s$SEED"
BASE=$D/results$S
OUT=$D/results${S}_profile
PROFILE=${PROFILE:-0.2,0.6,0.2}; ITERS=${ITERS:-6}; PAR=${PAR:-7}; ALGO=${ALGO:-1}; WIN=${WIN:-0}
read AI_START P BASE_ITERS W < $BASE/params.txt
END=$(awk -v s=$AI_START -v n=$ITERS -v p=$P 'BEGIN{printf "%.6f", s+n*p+p}')
# --load only sets the scale; phase loads come from --loadProfile
COMMON="--conf=examples/Reverie/config-workload.txt --alphasFile=examples/Reverie/alphas --cdfFileName=$D/websearch.txt --LEAF_COUNT=16 --SPINE_COUNT=4 --SERVER_COUNT=16 --LEAF_SERVER_CAPACITY=25 --SPINE_LEAF_CAPACITY=25 --algorithm=$ALGO --windowCheck=$WIN --randomSeed=$SEED --aiStart=$AI_START --tcpDctcp=$DC --pattern=2 --load=0.5 --loadProfile=$PROFILE --aiIters=$ITERS --aiWindow=$W --aiPeriod=$P --END_TIME=$END"

run() {
  name=$1; shift
  mkdir -p $OUT/$name
  $BIN $COMMON --fctOutFile=$OUT/$name/rdma-fct.txt --pfcOutFile=$OUT/$name/pfc.txt --torOutFile=$OUT/$name/tor.txt \
       --cctFile=$OUT/$name/cct.txt --tcpFctFile=$OUT/$name/tcp-fct.txt "$@" > $OUT/$name/run.log 2>&1
}
export -f run; export BIN COMMON OUT

rm -rf $OUT; mkdir -p $OUT
echo "$AI_START $P $ITERS $W $PROFILE" > $OUT/params.txt
ln -s ../results$S/ai-alone $OUT/ai-alone
JOBS=$OUT/jobs.txt
printf '%s\n' "tcp-alone --ai=0" "rr --ai=1" "w4 --ai=1 --aiWeight=4" "w8 --ai=1 --aiWeight=8" \
  "aimd100 --ai=1 --aimdTarget=100" "wait100 --ai=1 --tcpWaitLimit=100" "wait200 --ai=1 --tcpWaitLimit=200" > $JOBS
echo "Profile $PROFILE, seed $SEED: $(wc -l < $JOBS) runs, $PAR at a time..."
xargs -P $PAR -L 1 bash -c 'run "$@"' _ < $JOBS
echo "Done. Now: python3 $D/phase_summary.py $OUT"
