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

    # Monitor the process
    cpu_file="${dataset}_${method}_cpu.txt"
    sar -P ALL 1 > $cpu_file &  # Start sar to monitor CPU usage
    sar_pid=$!

    sleep 2  # Allow time for the process to start and stabilize

    # Monitor Memory and I/O statistics
    while kill -0 $copy_pid 2> /dev/null; do
        awk '/VmRSS/{print $2}' /proc/$copy_pid/status >> "${dataset}_${method}_mem.txt"
        awk '/read_bytes/{print $2}' /proc/$copy_pid/io >> "${dataset}_${method}_io_read.txt"
        awk '/write_bytes/{print $2}' /proc/$copy_pid/io >> "${dataset}_${method}_io_write.txt"
        sleep 1
    done

    kill $sar_pid  # Stop sar monitoring

    # Calculate elapsed time
    end_time=$(date +%s.%N)
    elapsed_time=$(echo "$end_time - $start_time" | bc)

    # Aggregate Memory Usage
    mem_usage=$(awk '{total+=$1; count++} END {if (count > 0) print total/count; else print "NA"}' "${dataset}_${method}_mem.txt")

    # Aggregate I/O Statistics
    io_read_avg=$(awk '{total+=$1; count++} END {if (count > 0) print total/count; else print "NA"}' "${dataset}_${method}_io_read.txt")
    io_write_avg=$(awk '{total+=$1; count++} END {if (count > 0) print total/count; else print "NA"}' "${dataset}_${method}_io_write.txt")

    # Calculate CPU Usage
    cpu_usage=$(awk -v pid=$copy_pid '$1 == "Average:" && $3 == pid {print $7, $8}' $cpu_file)

    # Verify the integrity of the copied data
    if diff -r $dataset ${dataset}_${method}_copy > /dev/null; then
        verification_status="Verification passed: $dataset and ${dataset}_${method}_copy are identical."
    else
        verification_status="Verification failed: $dataset and ${dataset}_${method}_copy differ."
    fi

    echo "$verification_status"
    echo "$dataset,$method,$elapsed_time,$mem_usage,$io_read_avg,$io_write_avg,$cpu_usage" >> $result_file
}

# List of datasets
datasets=("very_large_file" "very_large_file1")

for dataset in "${datasets[@]}"; do
    echo "Testing dataset: $dataset"
    capture_and_process_stats $dataset "cp"
    capture_and_process_stats $dataset "io_uring"
done

echo "Tests completed."
