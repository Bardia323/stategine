#!/usr/bin/env bash
# Stategine - a frame time taken only on a quiet card.
#
# A card shared with other work (a model being trained, another render) gives
# frame times that are the other work's: they swing two to ten times between
# runs, and a change measured so means nothing. This looks at the card first -
# how busy it is, and how much of its memory other programs hold - and refuses
# to time while it is busy; quiet, it runs the program three times and gives
# the best of the three, with what the card was doing beside each.
#
#   tools/quiet_time.sh [--anyway] [--busy PCT] [--held MIB] -- <program> <args...>
#
#   --busy PCT   the card counts as busy above this use, in percent (10)
#   --held MIB   or when other programs hold more than this memory (2048)
#   --anyway     time it busy all the same: every line then says BUSY
#
# The program's own output is kept; its lines that start `perf ` (the engine's
# programs print their frame times so) are what is compared. Exit 2: refused.
set -u
busy=10 held=2048 anyway=0
while [ $# -gt 0 ]; do
  case "$1" in
    --busy) busy=$2; shift 2 ;;
    --held) held=$2; shift 2 ;;
    --anyway) anyway=1; shift ;;
    --) shift; break ;;
    *) break ;;
  esac
done
[ $# -gt 0 ] || { sed -n '2,20p' "$0"; exit 1; }
command -v nvidia-smi >/dev/null || { echo "quiet_time: no nvidia-smi - the card's load cannot be read, so no time is trusted" >&2; exit 2; }

# The card's use (mean over a second) and the memory others hold, now.
card() {
  local sum=0 n=0 u
  for _ in 1 2 3 4 5; do
    u=$(nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits | head -1)
    sum=$((sum + u)); n=$((n + 1)); sleep 0.2
  done
  local mem
  mem=$(nvidia-smi --query-compute-apps=used_memory --format=csv,noheader,nounits | awk '{s += $1} END {print s + 0}')
  echo "$((sum / n)) $mem"
}

read -r use mem < <(card)
if [ "$use" -gt "$busy" ] || [ "$mem" -gt "$held" ]; then
  echo "quiet_time: the card is busy - ${use}% used, ${mem} MiB held by other programs (quiet is <= ${busy}% and <= ${held} MiB): its times would be theirs" >&2
  nvidia-smi --query-compute-apps=pid,process_name,used_memory --format=csv,noheader >&2
  [ "$anyway" = 1 ] || exit 2
fi

best=""
for run in 1 2 3; do
  read -r use mem < <(card)
  flag=""
  { [ "$use" -gt "$busy" ] || [ "$mem" -gt "$held" ]; } && flag=" BUSY"
  line=$("$@" 2>&1 | tee /dev/stderr | grep '^perf ' | tail -1)
  echo "run $run: ${line:-no perf line} (card ${use}%, ${mem} MiB held by others${flag})"
  mean=$(echo "$line" | sed -n 's/.* mean \([0-9.]*\).*/\1/p')
  if [ -n "$mean" ] && { [ -z "$best" ] || awk "BEGIN {exit !($mean < $best)}"; }; then best=$mean; fi
done
echo "best of 3: mean ${best:-?} ms"
