#!/bin/bash
fncts=( bounce "iplookup ip1k" crc "iplookup ip10k" )
set -x

for fnt in "${fncts[@]}"
do
    echo "Launching ${fnt}"

    # CPU Scalability
    ../npf/npf.py local+cpu:"CPU" --tags maxime ${fnt} cpu_scalability --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3 --result-path results-maxime --single-output results-maxime-csv/cpu-scalability-${fnt// /-}.csv --graph-filename results-maxime-graphs/cpu-scalability-${fnt// /-} --config n_runs=3 --cache
    
    # Scalability and energy
    ../npf/npf.py local+cpu:"CPU" local+gpu-coalescent:"ROI" local+gpu-commlist:"Zero-Copy" --tags maxime ${fnt} cpu_scalability cpu_gpu_scalability rate uncore energy energyfreq --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/scalability-energy-${fnt// /-}.csv --graph-filename results-maxime-graphs/scalability-energy-${fnt// /-} --config n_runs=3 --no-conntest
    ../npf/npf.py local+gpu-doca:"GPUDirect" --tags maxime ${fnt} gpu_only_scalability rate uncore energy energyfreq --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/scalability-energy-${fnt// /-}-gpudirect.csv --graph-filename results-maxime-graphs/scalability-energy-${fnt// /-}-gpudirect --config n_runs=3 --no-conntest

    # Energy
    ../npf/npf.py local+cpu:"CPU" local+gpu-coalescent:"ROI" local+gpu-commlist:"Zero-Copy" local+gpu-doca:"GPUDirect" --tags maxime ${fnt} comparison rate uncore energy --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/comparison-uncore-${fnt// /-}.csv --graph-filename results-maxime-graphs/comparison-uncore-${fnt// /-} --config n_runs=3 --no-conntest
    
    # Comparison
    ../npf/npf.py local+cpu:"CPU" local+gpu-coalescent:"ROI" local+gpu-commlist:"Zero-Copy" local+gpu-doca:"GPUDirect" --tags maxime ${fnt} comparison rate --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0  --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/comparison-${fnt// /-}.csv --graph-filename results-maxime-graphs/comparison-${fnt// /-} --config n_runs=3 --no-conntest --no-graph --keep-parameters --cache
    
    ../npf/npf.py local+cpu:"CPU" local+gpu-coalescent:"ROI" local+gpu-commlist:"Zero-Copy" local+gpu-doca:"GPUDirect" --tags maxime ${fnt} comparison_lat rate --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/comparison-lat-${fnt// /-}.csv --graph-filename results-maxime-graphs/comparison-lat-${fnt// /-} --config n_runs=1 --no-conntest --no-graph --keep-parameters --cache

    ../npf/npf.py local+dpu,bf2:"BF2" --tags maxime ${fnt} comparison rate --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 generatordpu=sam2,nic=0 bf2=bf2-jaskier-max,nic=0 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/comparison-bf2-${fnt// /-}.csv --graph-filename results-maxime-graphs/comparison-bf2-${fnt// /-} --config n_runs=5 --no-graph --force-retest
    
    ../npf/npf.py local+dpu,bf3:"BF3" --tags maxime ${fnt} comparison comparison_lat rate --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 generatordpu=sam2,nic=0 bf3=bf3-atchoum-max,nic=1 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/comparison-lat-bf3-${fnt// /-}.csv --graph-filename results-maxime-graphs/comparison-lat-bf3-${fnt// /-} --config n_runs=1 --force-retest
    
    
    # GPU Only Scalability
    ../npf/npf.py local+gpu-doca --tags maxime ${fnt} gpu_only_scalability rate --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/gpu-only-scalability-${fnt// /-}.csv --graph-filename results-maxime-graphs/gpu-only-scalability-${fnt// /-} --config n_runs=3 --no-graph --no-conntest --cache --preserve-temporaries 
    ../npf/npf.py local+gpu-doca --tags maxime ${fnt} uncore energy --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/gpu-only-scalability-uncore-${fnt// /-}.csv --graph-filename results-maxime-graphs/gpu-only-scalability-uncore-${fnt// /-} --config n_runs=3 --cache
    ../npf/npf.py local+gpu-doca --tags maxime ${fnt} gpu_only_scalability_batch rate --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/gpu-only-scalability-batch-${fnt// /-}.csv --graph-filename results-maxime-graphs/gpu-only-scalability-batch-${fnt// /-} --config n_runs=1
 
    # GPU Scalability
    ../npf/npf.py local+gpu-mw:"Master-Workers" local+gpu-coalescent:"ROI" local+gpu-commlist:"Zero-Copy" --tags maxime ${fnt} gpu_scalability_batch rate --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 --show-full --show-all --show-cmd --result-path results-maxime --single-output results-maxime-csv/gpu-scalability-batch-${fnt// /-}.csv --graph-filename results-maxime-graphs/gpu-scalability-batch-${fnt// /-} --config n_runs=3 --no-graph --no-conntest --cache
    ../npf/npf.py local+gpu-mw:"Master-Workers" local+gpu-coalescent:"ROI" local+gpu-commlist:"Zero-Copy" --tags maxime ${fnt} gpu_scalability --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/gpu-scalability-${fnt// /-}.csv --graph-filename results-maxime-graphs/gpu-scalability-${fnt// /-} --config n_runs=3 --no-graph

    # DPU
    ../npf/npf.py local+dpu,bf2:"DPU (BF2)" --tags maxime ${fnt} dpu_scalability --test main_script.npf --cluster generatordpu=sam,nic=0 bf2=bf2-jaskier-max --show-full --show-all --show-cmd --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/dpu-scalability-bf2-${fnt// /-}.csv --graph-filename results-maxime-graphs/dpu-scalability-bf2-${fnt// /-} --config n_runs=2 --force-retest
    ../npf/npf.py local+dpu,bf3:"DPU (BF3)" --tags maxime ${fnt} dpu_scalability --test main_script.npf --cluster generatordpu=sam,nic=0 bf3=bf3-atchoum-max,nic=1 --show-full --show-all --show-cmd --graph-size 6 3  --result-path results-maxime --single-output results-maxime-csv/dpu-scalability-bf3-${fnt// /-}.csv --graph-filename results-maxime-graphs/dpu-scalability-bf3-${fnt// /-} --config n_runs=5 --force-retest

    # GPU-CPU Scalability
    ../npf/npf.py local+gpu-commlist:"Zero-Copy" --tags maxime ${fnt} cpu_gpu_scalability --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3 --result-path results-maxime --single-output results-maxime-csv/cpu-gpu-scalability-${fnt// /-}-mempool.csv --graph-filename results-maxime-graphs/cpu-gpu-scalability-${fnt// /-}-mempool --config n_runs=3 --no-graph --no-conntest --cache
    ../npf/npf.py local+gpu-mw:"Master-Workers" local+gpu-coalescent:"ROI" local+gpu-commlist:"Zero-Copy" --tags maxime ${fnt} cpu_gpu_scalability fixed_rate --test main_script.npf --cluster generatorhost=sam,nic=1 server=sauron,nic=0 --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3 --result-path results-maxime --single-output results-maxime-csv/cpu-gpu-scalability-${fnt// /-}-fixed-rate.csv --graph-filename results-maxime-graphs/cpu-gpu-scalability-${fnt// /-}-fixed-rate --no-conntest --no-graph --variable 'SIZE=128' --keep-parameters --config n_runs=3

done


