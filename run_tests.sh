#!/bin/bash

# I/O AND TIME CAPTURE WORKS CORRECTLY
# Timestamp for results file
timestamp=$(date +"%Y%m%d_%H%M%S")
result_file="results_$timestamp.csv"
echo "Dataset,Method,Time,I/O Read (KB),I/O Write (KB)" > $result_file

capture_io_stats() {
    local pid=$1
    local io_file="/proc/$pid/io"
    local out_file="${2}_io.txt"
    > "$out_file"

    while kill -0 $pid 2> /dev/null; do
        if [ -f "$io_file" ]; then
            awk '/read_bytes/ {print $2}' $io_file >> "$out_file"
            awk '/write_bytes/ {print $2}' $io_file >> "$out_file"
        fi
        sleep 1
    done
}

process_io_data() {
    local file=$1
    local sum_read=0
    local sum_write=0
    local count=0

    while read -r read_bytes; read -r write_bytes; do
        sum_read=$((sum_read + read_bytes))
        sum_write=$((sum_write + write_bytes))
        count=$((count + 1))
    done < $file

    if [ $count -gt 0 ]; then
        echo "$((sum_read / count / 1024)) $((sum_write / count / 1024))"
    else
        echo "0 0"
    fi
}

capture_and_process_stats() {
    local dataset=$1
    local method=$2

    # Clear filesystem cache and sync
    sync
    echo 3 | sudo tee /proc/sys/vm/drop_caches > /dev/null
    sync

    echo "Starting $method for $dataset..."
    start_time=$(date +%s.%N)

    if [ "$method" == "cp" ]; then
        cp -r $dataset ${dataset}_${method}_copy &
    else
        ./io_uring_copy $dataset ${dataset}_${method}_copy &
    fi
    copy_pid=$!

    # Capture I/O statistics in the background
    capture_io_stats $copy_pid "${dataset}_${method}" &

    # Ensure all I/O operations are completed
    wait $copy_pid
    sync

    end_time=$(date +%s.%N)
    elapsed_time=$(echo "$end_time - $start_time" | bc)

    # Process I/O data
    io_data=$(process_io_data "${dataset}_${method}_io.txt")
    echo "$dataset,$method,$elapsed_time,$io_data" >> $result_file
}

datasets=("very_large_file" "very_large_file1")

for dataset in "${datasets[@]}"; do
    echo "Testing dataset: $dataset"
    capture_and_process_stats $dataset "cp"
    capture_and_process_stats $dataset "io_uring"
done

echo "Tests completed."
