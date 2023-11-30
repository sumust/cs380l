#!/bin/bash

# Timestamp for results file
timestamp=$(date +"%Y%m%d_%H%M%S")
result_file="results_$timestamp.csv"
echo "Dataset,Method,Time,I/O Read (KB),I/O Write (KB)" > $result_file

run_experiment() {
    local dataset=$1
    local method=$2

    # Clear filesystem cache and sync
    sync
    echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null
    sync

    echo "Starting $method for $dataset..."
    start_time=$(date +%s.%N)

    # Execute the copying command and capture its PID
    if [ "$method" == "cp" ]; then
        cp -r $dataset ${dataset}_${method}_copy &
    else
        ./io_uring_copy $dataset ${dataset}_${method}_copy &
    fi
    local copy_pid=$!

    # Wait for the copy process to complete
    wait $copy_pid

    end_time=$(date +%s.%N)
    local elapsed_time=$(echo "$end_time - $start_time" | bc)

    # Calculate I/O stats in KB
    local io_read_kb=$(awk '/read_bytes/{print $2/1024}' /proc/$copy_pid/io)
    local io_write_kb=$(awk '/write_bytes/{print $2/1024}' /proc/$copy_pid/io)

    # Verification of copied data
    if diff -r $dataset ${dataset}_${method}_copy > /dev/null; then
        local verification_status="Verification passed"
    else
        local verification_status="Verification failed"
    fi

    echo "$verification_status"
    echo "$dataset,$method,$elapsed_time,$io_read_kb,$io_write_kb" >> $result_file
}

# List of datasets
datasets=("very_large_file" "very_large_file1")

for dataset in "${datasets[@]}"; do
    echo "Testing dataset: $dataset"
    run_experiment $dataset "cp"
    run_experiment $dataset "io_uring"
done

echo "Tests completed."
