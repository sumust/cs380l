#!/bin/bash

# Timestamp for results file
timestamp=$(date +"%Y%m%d_%H%M%S")
result_file="results_$timestamp.csv"
echo "Dataset,Method,Time,Memory Usage (KB),I/O Read (KB/s),I/O Write (KB/s),CPU Usage (%)" > $result_file

capture_and_process_stats() {
    local dataset=$1
    local method=$2

    # Clear filesystem cache and sync
    sync
    echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null
    sync

    echo "Starting $method for $dataset..."
    start_time=$(date +%s.%N)

    # Start background monitoring processes
    vmstat 1 > "${dataset}_${method}_vmstat.txt" &
    vmstat_pid=$!
    iostat -dmx 1 > "${dataset}_${method}_iostat.txt" &
    iostat_pid=$!

    # Execute the copying command
    if [ "$method" == "cp" ]; then
        cp -r $dataset ${dataset}_${method}_copy
    else
        ./io_uring_copy $dataset ${dataset}_${method}_copy
    fi

    # Ensure all I/O operations are completed
    sync

    end_time=$(date +%s.%N)
    elapsed_time=$(echo "$end_time - $start_time" | bc)

    # Stop the monitoring processes
    kill $vmstat_pid $iostat_pid
    wait $vmstat_pid $iostat_pid

    # Collect and process CPU and memory usage data
    cpu_user=$(awk '{u+=$13} END {print u/NR}' "${dataset}_${method}_vmstat.txt")
    cpu_system=$(awk '{s+=$14} END {print s/NR}' "${dataset}_${method}_vmstat.txt")
    mem_free=$(awk '/^MemFree/ {print $2}' /proc/meminfo)
    mem_buffers=$(awk '/^Buffers/ {print $2}' /proc/meminfo)
    mem_cached=$(awk '/^Cached/ {print $2}' /proc/meminfo)

    # Process I/O read/write statistics
    io_read=$(awk '/^Device/ && NR>1 {getline; print $6}' "${dataset}_${method}_iostat.txt" | awk '{sum+=$1; count++} END {if (count>0) print sum/count; else print "0"}')
    io_write=$(awk '/^Device/ && NR>1 {getline; print $7}' "${dataset}_${method}_iostat.txt" | awk '{sum+=$1; count++} END {if (count>0) print sum/count; else print "0"}')

    # Verify the integrity of the copied data
    if diff -r $dataset ${dataset}_${method}_copy > /dev/null; then
        verification_status="Verification passed: $dataset and ${dataset}_${method}_copy are identical."
    else
        verification_status="Verification failed: $dataset and ${dataset}_${method}_copy differ."
    fi

    echo "$verification_status"
    echo "$dataset,$method,$elapsed_time,$mem_free,$mem_buffers,$mem_cached,$io_read,$io_write,$cpu_user $cpu_system" >> $result_file
}

# List of datasets
datasets=("high_volume_small_files")

for dataset in "${datasets[@]}"; do
    echo "Testing dataset: $dataset"
    capture_and_process_stats $dataset "cp"
    capture_and_process_stats $dataset "io_uring"
done

echo "Tests completed."
