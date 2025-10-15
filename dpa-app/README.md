## DPA implementations 

This folder contains the DPA implementation of the XPU paper. 
We used the work from Chen et al. as a basis for this project : https://github.com/RC4ML/BenchBF3.
The requirements and advices are similar to their project.

## Compilation the project

```bash
cmake . && make
```

## Running the project


The different application can be run from the same executable by providing different command line options:

```bash
sudo bin_dpu/dpa_xpu --device_name=${IB_DEVICE_NMAE} --g_thread_num=${NB_THREADS} --buffer_on_host=true --ip_lookup ${PATH_TO_TABLE_DPA}$  --compute_crc=true
```

--ip_lookup: runs the ip_lookup application, you need to specify the path to the table 
--compute_crc: boolean value to determine if the DPA need to compute the CRC or not

