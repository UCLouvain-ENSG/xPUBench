This folder contains all the NPF scripts implementing xPUBench.

The measures in our work were made using the scripts in this repo. All the scripts require [NPF](https://github.com/tbarbette/npf). NPF needs to be available in the parent folder or be in the PATH.

### Execution

The main NPF script is `main_script.npf`.
To help with execution, some examples are available in the Bash scripts `runs_split.sh` and `run_dpa.sh`.

```
sh npf-script.s
```

### Zero-loss Throughput Exploration
ZLT exploration can be enabled by adding `--exp-design "zlt(RATE,RX-GOODPUT-GBPS-PKTGEN)"` to any NPF command. In that case, set the RATE variable to `[1-100#1]` to specify the range you want to explore.

### Folders
The *baseline*, *ethermirror*, *iplookukp*, *crc* and *shared* folders contain the NPF scripts and FastClick config files used to make the measures.  
The *ip_tables* folder contains IP table bin files used for the IP Lookup measures.  
The *cluster* folder gives information about the nodes in the network to NPF.   
