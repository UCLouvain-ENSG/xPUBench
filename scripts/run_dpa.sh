#!/bin/bash
# fncts=( bounce "iplookup ip1k" "iplookup ip10k" crc )
fncts=( bounce crc )
set -x
for fnt in "${fncts[@]}"
do
    echo "Launching ${fnt}"
    # Energy
    #sleep 5
    ## Latency CMP
    # python3 npf/npf.py local+dpa:DPA --tags nikita comparison comparison_lat rate ${fnt} --test main_script.npf --cluster generatordpa=grincheux,nic=0 dpa=bf3-atchoum,nic=0  --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3 --single-output results-csv/comparison-lat-dpa-${fnt// /-}.csv --graph-filename graphs/comparison-all-test/comparison-lat-${fnt// /-}-dpa --variables 'NTHREADS={254}' --config 'n_runs=5' --preserve-temporaries --no-conntest $@
    # ## TP CMP
    # python3 npf/npf.py local+dpa:DPA --tags nikita comparison rate ${fnt} --test main_script.npf --cluster generatordpa=sam,nic=0 dpa=bf3-atchoum,nic=1  --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3 --single-output results-csv/comparison-dpa-${fnt// /-}.csv --graph-filename graphs/comparison-all-test/comparison-dpa-${fnt// /-}-dpa --variables 'NTHREADS={254}' --config 'n_runs=5' --preserve-temporaries --no-conntest $@
    # ## EVAL
    # python3 npf/npf.py local+dpa:DPA --tags nikita comparison rate ${fnt} --test main_script.npf --cluster generatordpa=sam,nic=0 dpa=bf3-atchoum,nic=1  --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3 --single-output results-csv/dpa-${fnt// /-}.csv --graph-filename graphs/comparison-all-test/${fnt// /-}-dpa --variables 'NTHREADS={16,32,64,128,190,254}' --config 'n_runs=5' --preserve-temporaries --no-conntest $@
    ## Energy
    python3 npf/npf.py local+dpa:"DPA"  --tags nikita energy rate benchlab ${fnt} --test main_script.npf --cluster generatordpa=grincheux dpa=bf3-atchoum manager=rpi  --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3 --single-output results-csv/energy-dpa-${fnt// /-}.csv --graph-filename graphs/comparison-all-test/comparison-energy-dpa-${fnt// /-} --config 'n_runs=3' --variables 'NTHREADS={16,32,64,128,254}' --preserve-temporaries $@ 
    #
    python3 npf/npf.py local+dpu,bf3:"DPU (BF3)"  --tags nikita energy rate benchlab ${fnt} --test main_script.npf --cluster generatordpu=grincheux bf3=bf3-atchoum manager=rpi  --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3 --single-output results-csv/energy-bf3-${fnt// /-}.csv --graph-filename graphs/comparison-all-test/comparison-energy-bf3-${fnt// /-} --config 'n_runs=3' --variables 'NTHREADS={1,2,4,8,16}' --preserve-temporaries $@ 
    # #
    # python3 npf/npf.py local+dpu,bf2:"DPU (BF2)"  --tags nikita energy rate benchlab ${fnt} --test main_script.npf --cluster generatordpu=grincheux bf2=bf2-benchlab manager=rpi  --show-full --show-all --show-cmd --config var_names+={version:Version} --graph-size 6 3 --single-output results-csv/energy-bf2-${fnt// /-}.csv --graph-filename graphs/comparison-all-test/comparison-energy-bf2-${fnt// /-} --config 'n_runs=3' --variables 'NTHREADS={1,2,4,7}' --preserve-temporaries $@ 


done

