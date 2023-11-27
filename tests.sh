#!/bin/bash

echo "Running tests..."

# File to store the results
result_file="results.csv"
echo "Dataset,Method,Time,CPU Usage,Memory Usage,I/O Read,I/O Write" > $result_file

for dataset in small_files large_files mixed_files nested_dirs; do
    echo "Testing dataset: $dataset"

    # Clear filesystem cache
    echo 3 | sudo tee /proc/sys/vm/drop_caches

    # Capture system stats before running cp
    vmstat 1 5 > ${dataset}_cp_vmstat.txt &
    iostat 1 5 > ${dataset}_cp_iostat.txt &

    # Test with cp -r
    cp_time=$(/usr/bin/time -f "%e" cp -r $dataset ${dataset}_cp_copy 2>&1)

    # Process system stats for cp
    cp_cpu=$(awk '{u+=$13; s+=$14} END {print u/NR " " s/NR}' ${dataset}_cp_vmstat.txt)
    cp_mem=$(awk '{free+=$4; buff+=$5; cache+=$6} END {print free/NR " " buff/NR " " cache/NR}' ${dataset}_cp_vmstat.txt)
    cp_io=$(awk '/^Device/ {getline; print $5 " " $6}' ${dataset}_cp_iostat.txt)

    echo "$dataset,cp,$cp_time,$cp_cpu,$cp_mem,$cp_io" >> $result_file

    # Clear filesystem cache again
    echo 3 | sudo tee /proc/sys/vm/drop_caches

    # Capture system stats before running io_uring copy
    vmstat 1 5 > ${dataset}_uring_vmstat.txt &
    iostat 1 5 > ${dataset}_uring_iostat.txt &

    # Test with io_uring copy program
    uring_time=$(/usr/bin/time -f "%e" ./io_uring_copy $dataset ${dataset}_uring_copy 2>&1)

    # Process system stats for io_uring
    uring_cpu=$(awk '{u+=$13; s+=$14} END {print u/NR " " s/NR}' ${dataset}_uring_vmstat.txt)
    uring_mem=$(awk '{free+=$4; buff+=$5; cache+=$6} END {print free/NR " " buff/NR " " cache/NR}' ${dataset}_uring_vmstat.txt)
    uring_io=$(awk '/^Device/ {getline; print $5 " " $6}' ${dataset}_uring_iostat.txt)

    echo "$dataset,io_uring,$uring_time,$uring_cpu,$uring_mem,$uring_io" >> $result_file
done

echo "Tests completed."
