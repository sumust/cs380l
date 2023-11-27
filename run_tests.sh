#!/bin/bash

echo "Running tests..."

# File to store the results
result_file="results.csv"
echo "Dataset,Method,Time,CPU Usage,Memory Usage,I/O Read,I/O Write" > $result_file

capture_stats() {
    local dataset=$1
    local method=$2
    vmstat 1 > "${dataset}_${method}_vmstat.txt" &
    vmstat_pid=$!
    iostat 1 > "${dataset}_${method}_iostat.txt" &
    iostat_pid=$!
    sleep 2 
}

process_stats() {
    local dataset=$1
    local method=$2
    local cpu_stat=$(awk '{u+=$13; s+=$14} END {if (NR > 0) print u/NR " " s/NR; else print "0 0"}' "${dataset}_${method}_vmstat.txt")
    local mem_stat=$(awk '{free+=$4; buff+=$5; cache+=$6} END {if (NR > 0) print free/NR " " buff/NR " " cache/NR; else print "0 0 0"}' "${dataset}_${method}_vmstat.txt")
    local io_stat=$(awk '/^Device/ {getline; print $5 " " $6}' "${dataset}_${method}_iostat.txt")
    echo "${cpu_stat},${mem_stat},${io_stat}"
}

# Added many_large_files dataset
for dataset in small_files large_files many_large_files mixed_files nested_dirs; do
    echo "Testing dataset: $dataset"

    # Clear filesystem cache
    echo 3 | sudo tee /proc/sys/vm/drop_caches

    echo "Starting cp for $dataset..."
    capture_stats $dataset "cp"
    cp_time=$(/usr/bin/time -f "%e" cp -r $dataset ${dataset}_cp_copy 2>&1)
    kill $vmstat_pid $iostat_pid
    cp_stats=$(process_stats $dataset "cp")
    echo "$dataset,cp,$cp_time,$cp_stats" >> $result_file

    # Clear filesystem cache again
    echo 3 | sudo tee /proc/sys/vm/drop_caches

    echo "Starting io_uring for $dataset..."
    capture_stats $dataset "io_uring"
    uring_time=$(/usr/bin/time -f "%e" ./io_uring_copy $dataset ${dataset}_uring_copy 2>&1)
    kill $vmstat_pid $iostat_pid
    uring_stats=$(process_stats $dataset "io_uring")
    echo "$dataset,io_uring,$uring_time,$uring_stats" >> $result_file
done

echo "Tests completed."
