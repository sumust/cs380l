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
    iostat -dmx 1 > "${dataset}_${method}_iostat.txt" &
    iostat_pid=$!
    sleep 2  # Ensure stats collection starts
}

process_stats() {
    local dataset=$1
    local method=$2

    # Process CPU and memory statistics
    local cpu_stat=$(awk '{u+=$13; s+=$14} END {if (NR > 2) print u/(NR-2) " " s/(NR-2); else print "0 0"}' "${dataset}_${method}_vmstat.txt")
    local mem_stat=$(awk '{free+=$4; buff+=$5; cache+=$6} END {if (NR > 2) print free/(NR-2) " " buff/(NR-2) " " cache/(NR-2); else print "0 0 0"}' "${dataset}_${method}_vmstat.txt")

    # Process I/O read/write statistics
    local io_read=$(awk '/^Device/ && NR>1 {getline; print $6}' "${dataset}_${method}_iostat.txt" | awk '{sum+=$1; count++} END {if (count>0) print sum/count; else print "0"}')
    local io_write=$(awk '/^Device/ && NR>1 {getline; print $7}' "${dataset}_${method}_iostat.txt" | awk '{sum+=$1; count++} END {if (count>0) print sum/count; else print "0"}')

    echo "$cpu_stat,$mem_stat,$io_read,$io_write"
}

for dataset in small_files large_files larger_files many_large_files mixed_files nested_dirs; do
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
