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

    # Start monitoring CPU usage for the specific process
    pidstat -p $copy_pid 1 > "${dataset}_${method}_pidstat.txt" &

    # Monitor I/O statistics for the specific process
    iotop -b -p $copy_pid -d 1 -n 10 > "${dataset}_${method}_iotop.txt" &

    # Monitor memory usage
    while kill -0 $copy_pid 2> /dev/null; do
        ps -o rss= -p $copy_pid >> "${dataset}_${method}_mem.txt"
        sleep 1
    done

    # Wait for the copy process to complete
    wait $copy_pid

    end_time=$(date +%s.%N)
    elapsed_time=$(echo "$end_time - $start_time" | bc)

    # Wait a bit to ensure monitoring data is captured fully
    sleep 2

    # Kill the monitoring tools
    pkill -P $$

    # Extract CPU usage
    cpu_usage=$(awk '{if(NR>1)print $6" "$7}' "${dataset}_${method}_pidstat.txt" | awk '{u+=$1; s+=$2} END {print u/NR" "s/NR}')

    # Extract I/O statistics
    io_stat=$(awk '{read+=$4; write+=$6} END {print read/NR" "write/NR}' "${dataset}_${method}_iotop.txt")

    # Calculate average memory usage
    mem_usage=$(awk '{total+=$1; count++} END {print total/count}' "${dataset}_${method}_mem.txt")

    # Verify the integrity of the copied data
    if diff -r $dataset ${dataset}_${method}_copy > /dev/null; then
        verification_status="Verification passed: $dataset and ${dataset}_${method}_copy are identical."
    else
        verification_status="Verification failed: $dataset and ${dataset}_${method}_copy differ."
    fi

    echo "$verification_status"
    echo "$dataset,$method,$elapsed_time,$mem_usage,$io_stat,$cpu_usage" >> $result_file
}

# List of datasets
datasets=("very_large_file" "very_large_file1")

for dataset in "${datasets[@]}"; do
    echo "Testing dataset: $dataset"
    capture_and_process_stats $dataset "cp"
    capture_and_process_stats $dataset "io_uring"
done

echo "Tests completed.
