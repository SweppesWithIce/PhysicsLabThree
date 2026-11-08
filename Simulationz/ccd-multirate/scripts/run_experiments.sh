#!/usr/bin/env bash
# Прогоняет все эксперименты без окна и складывает результаты в data/.
# Запуск из корня репозитория после сборки: bash scripts/run_experiments.sh
set -euo pipefail
B=./build/physbench
mkdir -p data

# Шаг 0: интегратор и детерминированность
for hz in 60 120 240; do
  $B freefall --hz $hz --time 1.0 --out data/freefall_$hz.csv
done | tee data/freefall_summary.txt
{
  $B determinism --bodies 50  --steps 600  --seed 1
  $B determinism --bodies 100 --steps 3600 --seed 42
} | tee data/determinism.txt
{
  $B determinism --bodies 60 --steps 1200 --seed 7 --room 1
} | tee data/determinism_collisions.txt

# Шаг 1: отскок и устойчивость стека
{
  $B bounce --hz 60  --e 0.5 --h 300 --out data/bounce_60.csv
  $B bounce --hz 240 --e 0.5 --h 300 --out data/bounce_240.csv
} | tee data/bounce_summary.txt
$B bounce-sweep --hz 60  --h 300 --out data/bounce_sweep_60.csv  | tee data/bounce_sweep_60.txt
$B bounce-sweep --hz 240 --h 300 --out data/bounce_sweep_240.csv | tee data/bounce_sweep_240.txt
$B stack --hz 60 --boxes 10 --iters 10 --warm 1 --time 10 --out data/stack_warm1.csv | tee data/stack_summary.txt
$B stack --hz 60 --boxes 10 --iters 10 --warm 0 --time 10 --out data/stack_warm0.csv | tee -a data/stack_summary.txt
$B stack-sweep --hz 60 --boxes 10 --time 10 --out data/stack_sweep.csv | tee data/stack_sweep.txt

# Шаг 2: туннелирование в дискретном режиме
{
  for hz in 60 120 240; do $B tunnel --hz $hz --v 2000 --u -15; done
  $B tunnel --hz 60 --v 2000 --u 4
  $B tunnel --hz 60 --v 2000 --u -5
} | tee data/tunnel_positions.txt
$B tunnel-sweep --runs 1000 --seed 1 --out data/tunnel_sweep.csv > data/tunnel_sweep.txt

# Шаг 3: CCD
$B ccd-check --runs 2000 --hz 60 --seed 3 --out data/ccd_check.csv | tee data/ccd_check.txt
$B tunnel-sweep --runs 1000 --seed 1 --mode ccd --out data/tunnel_sweep_ccd.csv > data/tunnel_sweep_ccd.txt
$B bounce-sweep --hz 60 --h 300 --mode ccd --out data/bounce_sweep_60_ccd.csv | tee data/bounce_sweep_60_ccd.txt
$B stack --hz 60 --boxes 10 --iters 10 --warm 1 --time 10 --mode ccd --out data/stack_ccd.csv | tee data/stack_ccd.txt
$B determinism --bodies 60 --steps 1200 --seed 7 --room 1 --mode ccd | tee data/determinism_ccd.txt
$B tunnel --hz 60 --v 2000 --u -15 --trace data/trace_discrete.csv > /dev/null
$B tunnel --hz 60 --v 2000 --u -15 --mode ccd --trace data/trace_ccd.csv > /dev/null

# Шаг 4: multi-rate
{
  $B tunnel --mode multirate --hz 60 --fast-hz 60  --v 2000 --u -15
  $B tunnel --mode multirate --hz 60 --fast-hz 240 --v 2000 --u -15
} | tee data/multirate_minimal.txt
$B multirate-sweep --runs 1000 --seed 1 --out data/multirate_sweep.csv | tee data/multirate_sweep.txt
$B stack --hz 60 --boxes 10 --iters 10 --warm 1 --time 10 --mode multirate --out data/stack_multirate.csv | tee data/stack_multirate.txt
$B determinism --bodies 60 --steps 1200 --seed 7 --room 1 --mode multirate | tee data/determinism_multirate.txt
$B timing --normal 40 --fast 5 --v 2000 --time 20 --reps 11 --out data/timing_step4.csv | tee data/timing_step4.txt

# Шаг 6: сравнение режимов на основной сцене
$B main-compare --time 60 --out data/main_compare.csv | tee data/main_compare.txt
$B main-timing --time 20 --reps 15 --out data/main_timing.csv | tee data/main_timing.txt
$B stack --hz 240 --boxes 10 --iters 10 --warm 1 --time 10 --out data/stack_240.csv | tee data/stack_240.txt
