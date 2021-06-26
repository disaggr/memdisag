#!/bin/bash
set -e

shmem=$1

export LD_LIBRARY_PATH=$PWD/arrow_build/release

echo "Compiling benchmarks"
g++ -I.local/include -Larrow_build/release bench_setup.cc -lplasma -larrow -O3 -o bench_setup

n1=1000		#1000
n2=500		#10000
n3=200		#100000
n4=100		#1000000
n5=50     #10000000
n6=10     #100000000

echo "Running benchmarks..."

repetitions=100

RESULTS_DIR=results/remote_results

mkdir -p $RESULTS_DIR

if [[ "$2" == 1 ]]; then
size=1000
./bench_setup /tmp/plasma $shmem $n1 $size

elif [[ "$2" == 2 ]]; then
size=10000
./bench_setup /tmp/plasma $shmem $n2 $size >> $RESULTS_DIR/benchmark-$repetitions.$n2.$size.result

elif [[ "$2" == 3 ]]; then
size=100000
./bench_setup /tmp/plasma $shmem $n3 $size >> $RESULTS_DIR/benchmark-$repetitions.$n3.$size.result

elif [[ "$2" == 4 ]]; then
size=1000000
./bench_setup /tmp/plasma $shmem $n4 $size >> $RESULTS_DIR/benchmark-$repetitions.$n4.$size.result

elif [[ "$2" == 5 ]]; then
size=10000000
./bench_setup /tmp/plasma $shmem $n5 $size >> $RESULTS_DIR/benchmark-$repetitions.$n5.$size.result

elif [[ "$2" == 6 ]]; then
size=100000000
./bench_setup /tmp/plasma $shmem $n6 $size >> $RESULTS_DIR/benchmark-$repetitions.$n6.$size.result
fi

rm bench_setup

echo "Done"
exit 0