#!/bin/bash
set -e

shmem=$1

export LD_LIBRARY_PATH=$PWD/arrow_build/release

echo "Compiling benchmarks"
g++ -I.local/include -Larrow_build/release bench_local.cc -lplasma -larrow -O3 -o bench_local

n1=1000		#1000 bytes
n2=500		#10000
n3=200		#100000
n4=100		#1000000
n5=50     #10000000
n6=10     #100000000

echo "Running benchmarks..."

repetitions=100

RESULTS_DIR=results/local_results

mkdir -p $RESULTS_DIR

echo "Benchmark 1"
size=1000
echo "" > $RESULTS_DIR/benchmark-$repetitions.$n1.$size.result
for (( i=0; i<$repetitions; i++ ))
do
  ./bench_local /tmp/plasma $shmem $n1 $size >> $RESULTS_DIR/benchmark-$repetitions.$n1.$size.result
  offset=$(($offset+$n1))
done

echo "Benchmark 2"
size=10000
echo "" > $RESULTS_DIR/benchmark-$repetitions.$n2.$size.result
for (( i=0; i<$repetitions; i++ ))
do
  ./bench_local /tmp/plasma $shmem $n2 $size >> $RESULTS_DIR/benchmark-$repetitions.$n2.$size.result
  offset=$(($offset+$n2))
done

echo "Benchmark 3"
size=100000
echo "" > $RESULTS_DIR/benchmark-$repetitions.$n3.$size.result
for (( i=0; i<$repetitions; i++ ))
do
  ./bench_local /tmp/plasma $shmem $n3 $size >> $RESULTS_DIR/benchmark-$repetitions.$n3.$size.result
  offset=$(($offset+$n3))
done

echo "Benchmark 4"
size=1000000
echo "" > $RESULTS_DIR/benchmark-$repetitions.$n4.$size.result
for (( i=0; i<$repetitions; i++ ))
do
  ./bench_local /tmp/plasma $shmem $n4 $size >> $RESULTS_DIR/benchmark-$repetitions.$n4.$size.result
  offset=$(($offset+$n4))
done

echo "Benchmark 5"
size=10000000
echo "" > $RESULTS_DIR/benchmark-$repetitions.$n5.$size.result
for (( i=0; i<$repetitions; i++ ))
do
  ./bench_local /tmp/plasma $shmem $n5 $size >> $RESULTS_DIR/benchmark-$repetitions.$n5.$size.result
  offset=$(($offset+$n5))
done

echo "Benchmark 6"
size=100000000
echo "" > $RESULTS_DIR/benchmark-$repetitions.$n6.$size.result
for (( i=0; i<$repetitions; i++ ))
do
  ./bench_local /tmp/plasma $shmem $n6 $size >> $RESULTS_DIR/benchmark-$repetitions.$n6.$size.result
  offset=$(($offset+$n6))
done

rm bench_local

echo "Done"
echo "Results written to files ($RESULTS_DIR/benchmark-<cycles>.<num_size>.<object_size>)"