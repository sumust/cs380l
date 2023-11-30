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

    # Execute the copying command in the background and capture its PID
    if [ "$method" == "cp" ]; then
        cp -r $dataset ${dataset}_${method}_copy &
    else
        ./io_uring_copy $dataset ${dataset}_${method}_copy &
    fi
    copy_pid=$!

    # Initialize variables
    local io_read_total=0
    local io_write_total=0
    local memory_total=0
    local count=0

    # Monitor the process
    while kill -0 $copy_pid 2>/dev/null; do
        # Extract I/O statistics
        local io_read=$(awk '/read_bytes/ {print $2}' /proc/$copy_pid/io)
        local io_write=$(awk '/write_bytes/ {print $2}' /proc/$copy_pid/io)

        # Extract memory usage
        local memory=$(awk '/VmRSS:/ {print $2}' /proc/$copy_pid/status)

        # Aggregate data
        io_read_total=$((io_read_total + io_read))
        io_write_total=$((io_write_total + io_write))
        memory_total=$((memory_total + memory))
        count=$((count + 1))

        sleep 1
    done

    # Calculate averages
    local io_read_avg=$((io_read_total / count))
    local io_write_avg=$((io_write_total / count))
    local memory_avg=$((memory_total / count))

    end_time=$(date +%s.%N)
    elapsed_time=$(echo "$end_time - $start_time" | bc)

    # Verify the integrity of the copied data
    if diff -r $dataset ${dataset}_${method}_copy > /dev/null; then
        verification_status="Verification passed: $dataset and ${dataset}_${method}_copy are identical."
    else
        verification_status="Verification failed: $dataset and ${dataset}_${method}_copy differ."
    fi

    echo "$verification_status"
    echo "$dataset,$method,$elapsed_time,$memory_avg,$io_read_avg,$io_write_avg" >> $result_file
}

# List of datasets
datasets=("very_large_file" "very_large_file1")

for dataset in "${datasets[@]}"; do
    echo "Testing dataset: $dataset"
    capture_and_process_stats $dataset "cp"
    capture_and_process_stats $dataset "io_uring"
done

echo "Tests completed."
