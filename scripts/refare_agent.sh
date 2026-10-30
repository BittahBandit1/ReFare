#!/bin/bash
# ReFare test runner: lets Claude rebuild, launch and close the game on its own.
#
# Start it once and leave it running (Ctrl+C stops it):
#   caffeinate -dims scripts/refare_agent.sh
# caffeinate keeps the display awake: a locked or sleeping screen stops the
# game from presenting, which would make timing measurements meaningless.
#
# It watches build/claude-jobs/ for a file named next.job. A job may contain
# only these lines (anything else rejects the whole job, so a job can't run
# any other command on this Mac):
#   seconds=N              run for N seconds (10-900), then quit cleanly
#   env=NAME=VALUE         NAME must start with REFARE_ or MTL_; VALUE plain
#   package=1              instead of running the game, build the tools and
#                          package ReFare.app (scripts/make-app.sh)
# Use env=REFARE_NO_ACTIVATE=1 to open the game window without taking keyboard
# focus, so typing in other apps never reaches the game.
# Every job rebuilds first (make run does nothing extra when up to date), then
# runs with --automation-dir build/claude-auto so pad input, screenshots and a
# clean stop can be driven through files while it runs. The log goes to
# build/logs/job-<id>.log and the result to build/claude-jobs/<id>.done.
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT=$PWD
JOBS=$ROOT/build/claude-jobs
AUTO=build/claude-auto
mkdir -p "$JOBS" "$ROOT/build/logs"

LOCK=$JOBS/.agent-lock
if ! mkdir "$LOCK" 2>/dev/null; then
  old=$(cat "$LOCK/pid" 2>/dev/null)
  if [ -n "$old" ] && kill -0 "$old" 2>/dev/null; then
    echo "Another ReFare test runner is already running (pid $old)."
    exit 1
  fi
  echo "Removing a stale lock left by an earlier runner."
  rm -rf "$LOCK" && mkdir "$LOCK" || exit 1
fi
echo $$ > "$LOCK/pid"
trap 'rm -rf "$LOCK"; echo; echo "ReFare test runner stopped."; exit 0' INT TERM EXIT

echo "ReFare test runner waiting for jobs in build/claude-jobs (Ctrl+C to stop)."
while true; do
  if [ ! -f "$JOBS/next.job" ]; then sleep 2; continue; fi
  id=$(date +%Y%m%d-%H%M%S)
  mv "$JOBS/next.job" "$JOBS/$id.job"
  seconds=60; envs=(); bad=""; package=""
  while IFS= read -r line || [ -n "$line" ]; do
    line=${line%$'\r'}
    case "$line" in
      ""|"#"*) ;;
      seconds=*)
        v=${line#seconds=}
        if [[ "$v" =~ ^[0-9]+$ ]] && [ "$v" -ge 10 ] && [ "$v" -le 900 ]; then seconds=$v
        else bad="bad seconds: $v"; fi ;;
      env=*)
        kv=${line#env=}
        if [[ "$kv" =~ ^(REFARE_|MTL_)[A-Z0-9_]+=[A-Za-z0-9._/:-]*$ ]]; then envs+=("$kv")
        else bad="rejected env line: $kv"; fi ;;
      package=1) package=1 ;;
      *) bad="rejected line: $line" ;;
    esac
  done < "$JOBS/$id.job"
  if [ -n "$bad" ]; then
    echo "job $id rejected ($bad)" | tee "$JOBS/$id.done"
    continue
  fi

  log=$ROOT/build/logs/job-$id.log
  if [ -n "$package" ]; then
    echo "job $id: package ReFare.app"
    { make tools JOBS=8 && bash scripts/make-app.sh; } > "$log" 2>&1; rc=$?
    printf 'exit=%s\nlog=%s\n' "$rc" "build/logs/job-$id.log" > "$JOBS/$id.done"
    echo "job $id finished (exit $rc)"
    continue
  fi
  echo "job $id: run ${seconds}s ${envs[*]:-}"
  rm -rf "$ROOT/$AUTO"
  start=$(date +%s)
  env "${envs[@]+"${envs[@]}"}" make run GAME=Crazy-Taxi-USA JOBS=8 \
    RUN_ARGS="--graphics Metal --user-dir build/user --automation-dir $AUTO" > "$log" 2>&1 &
  pid=$!
  # The build may take a while; the run time counts from when the game booted.
  booted=""
  while kill -0 $pid 2>/dev/null; do
    if [ -z "$booted" ] && grep -q '^booted=1' "$ROOT/$AUTO/status.txt" 2>/dev/null; then
      booted=$(date +%s)
    fi
    if [ -n "$booted" ] && [ $(( $(date +%s) - booted )) -ge "$seconds" ]; then break; fi
    if [ $(( $(date +%s) - start )) -ge $(( seconds + 1800 )) ]; then break; fi
    sleep 1
  done
  if kill -0 $pid 2>/dev/null; then
    # Clean quit through the automation interface, so exit reports still print.
    mkdir -p "$ROOT/$AUTO/commands"
    printf 'command=stop\n' > "$ROOT/$AUTO/commands/zzzz-stop.tmp"
    mv "$ROOT/$AUTO/commands/zzzz-stop.tmp" "$ROOT/$AUTO/commands/zzzz-stop.txt"
    for _ in $(seq 1 20); do kill -0 $pid 2>/dev/null || break; sleep 1; done
    if kill -0 $pid 2>/dev/null; then
      pkill -f "lib/ModernGekko/build/moderngekko-run" 2>/dev/null
      sleep 2
    fi
  fi
  wait $pid 2>/dev/null; rc=$?
  printf 'exit=%s\nbooted=%s\nlog=%s\n' "$rc" "${booted:+yes}" "build/logs/job-$id.log" > "$JOBS/$id.done"
  echo "job $id finished (exit $rc)"
done
