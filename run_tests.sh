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

    # Execute the copying command in the background and get its PID
    if [ "$method" == "cp" ]; then
        cp -r $dataset ${dataset}_${method}_copy &
    else
        ./io_uring_copy $dataset ${dataset}_${method}_copy &
    fi
    copy_pid=$!

    # Monitor CPU, memory, and I/O usage of the process
    pidstat -p $copy_pid 1 > "${dataset}_${method}_pidstat.txt" &
    pidstat_pid=$!
    iotop -b -p $copy_pid -n 10 -d 1 > "${dataset}_${method}_iotop.txt" &
    iotop_pid=$!

    # Wait for the copy process to complete
    wait $copy_pid

    # Ensure all I/O operations are completed
    sync

    end_time=$(date +%s.%N)
    elapsed_time=$(echo "$end_time - $start_time" | bc)

    # Stop the monitoring processes
    kill $pidstat_pid $iotop_pid
    wait $pidstat_pid $iotop_pid

    # Collect and process CPU and memory usage data
    cpu_usage=$(awk '{if ($1 != "Linux") print $7" "$8}' "${dataset}_${method}_pidstat.txt" | tail -n 1)
    mem_usage=$(awk '/Total DISK READ and DISK WRITE/ {getline; print $4" "$10}' "${dataset}_${method}_iotop.txt" | tail -n 1)

    # Verify the integrity of the copied data
    if diff -r $dataset ${dataset}_${method}_copy > /dev/null; then
        verification_status="Verification passed: $dataset and ${dataset}_${method}_copy are identical."
    else
        verification_status="Verification failed: $dataset and ${dataset}_${method}_copy differ."
    fi

    echo "$verification_status"
    echo "$dataset,$method,$elapsed_time,$mem_usage,$cpu_usage" >> $result_file
}

# List of datasets
datasets=("very_large_file" "very_large_file1")

for dataset in "${datasets[@]}"; do
    echo "Testing dataset: $dataset"
    capture_and_process_stats $dataset "cp"
    capture_and_process_stats $dataset "io_uring"
done

echo "Tests completed."
