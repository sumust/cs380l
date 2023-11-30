#!/bin/bash

timestamp=$(date +"%Y%m%d_%H%M%S")
result_file="results_$timestamp.csv"
echo "Dataset,Method,Time,I/O Read (KB),I/O Write (KB),CPU User Time (%),CPU System Time (%)" > $result_file

capture_io_and_cpu_stats() {
    local pid=$1
    local dataset=$2
    local method=$3
    local io_file="/proc/$pid/io"
    local stat_file="/proc/$pid/stat"
    local total_cpu_file="/proc/stat"
    local io_out_file="${dataset}_${method}_io.txt"
    local cpu_out_file="${dataset}_${method}_cpu.txt"
    > "$io_out_file"
    > "$cpu_out_file"

    local total_cpu_time_before=$(awk '{print $2 + $3 + $4 + $5 + $6 + $7 + $8}' $total_cpu_file)
    while kill -0 $pid 2> /dev/null; do
        if [ -f "$io_file" ]; then
            awk '/read_bytes/ {print $2}' $io_file >> "$io_out_file"
            awk '/write_bytes/ {print $2}' $io_file >> "$io_out_file"
        fi
        if [ -f "$stat_file" ]; then
            awk '{print $14 " " $15}' $stat_file >> "$cpu_out_file"
        fi
        sleep 1
    done
    local total_cpu_time_after=$(awk '{print $2 + $3 + $4 + $5 + $6 + $7 + $8}' $total_cpu_file)

    # Calculate total CPU time elapsed
    echo "$total_cpu_time_before $total_cpu_time_after" >> "$cpu_out_file"
}

process_io_and_cpu_data() {
    local io_file=$1
    local cpu_file=$2
    local total_cpu_file=$3

    # Process I/O data
    local sum_read=0
    local sum_write=0
    local count=0
    while read -r read_bytes; read -r write_bytes; do
        sum_read=$((sum_read + read_bytes))
        sum_write=$((sum_write + write_bytes))
        count=$((count + 1))
    done < $io_file
    local avg_read=$((sum_read / count / 1024))
    local avg_write=$((sum_write / count / 1024))

    # Process CPU data
    local sum_user=0
    local sum_system=0
    local total_cpu_before
    local total_cpu_after
    while read -r user_time system_time; do
        sum_user=$((sum_user + user_time))
        sum_system=$((sum_system + system_time))
    done < $cpu_file
    read total_cpu_before total_cpu_after < $cpu_file
    local total_cpu_time=$((total_cpu_after - total_cpu_before))
    local avg_user_cpu=$(bc <<< "scale=2; 100 * $sum_user / $total_cpu_time")
    local avg_system_cpu=$(bc <<< "scale=2; 100 * $sum_system / $total_cpu_time")

    echo "$avg_read $avg_write $avg_user_cpu $avg_system_cpu"
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

    # Capture I/O and CPU statistics in the background
    capture_io_and_cpu_stats $copy_pid $dataset $method &

    # Ensure all I/O operations are completed
    wait $copy_pid
    sync

    end_time=$(date +%s.%N)
    elapsed_time=$(echo "$end_time - $start_time" | bc)

    # Process I/O and CPU data
    io_cpu_data=$(process_io_and_cpu_data "${dataset}_${method}_io.txt" "${dataset}_${method}_cpu.txt" "/proc/stat")
    echo "$dataset,$method,$elapsed_time,$io_cpu_data" >> $result_file

    # Verification of the copied data
    if diff -r $dataset ${dataset}_${method}_copy > /dev/null; then
        echo "Verification passed: $dataset and ${dataset}_${method}_copy are identical."
    else
        echo "Verification failed: $dataset and ${dataset}_${method}_copy differ."
    fi
}

datasets=("very_large_file" "very_large_file1" "nested_dirs")

for dataset in "${datasets[@]}"; do
    echo "Testing dataset: $dataset"
    capture_and_process_stats $dataset "cp"
    capture_and_process_stats $dataset "io_uring"
done

echo "Tests completed."
