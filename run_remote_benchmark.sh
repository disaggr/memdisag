#!/bin/bash
set -e

shmem=$1

export LD_LIBRARY_PATH=$PWD/arrow_build/release

if [ ! -f bench_remote.cc ]; then
  echo "Compiling benchmarks"
  g++ -I.local/include -Larrow_build/release bench_remote.cc -lplasma -larrow -O3 -o bench_remote
fi

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
echo "" > $RESULTS_DIR/benchmark-$repetitions.$n1.$size.result
for (( i=0; i<$repetitions; i++ ))
do
  ./bench_remote /tmp/plasma2 $shmem $n1 $size >> $RESULTS_DIR/benchmark-$repetitions.$n1.$size.result
  offset=$(($offset+$n1))
done

elif [[ "$2" == 2 ]]; then
size=10000
echo "" > $RESULTS_DIR/benchmark-$repetitions.$n2.$size.result
for (( i=0; i<$repetitions; i++ ))
do
  ./bench_remote /tmp/plasma2 $shmem $n2 $size >> $RESULTS_DIR/benchmark-$repetitions.$n2.$size.result
  offset=$(($offset+$n2))
done

elif [[ "$2" == 3 ]]; then
size=100000
echo "" > $RESULTS_DIR/benchmark-$repetitions.$n3.$size.result
for (( i=0; i<$repetitions; i++ ))
do
  ./bench_remote /tmp/plasma2 $shmem $n3 $size >> $RESULTS_DIR/benchmark-$repetitions.$n3.$size.result
  offset=$(($offset+$n3))
done

elif [[ "$2" == 4 ]]; then
size=1000000
echo "" > $RESULTS_DIR/benchmark-$repetitions.$n4.$size.result
for (( i=0; i<$repetitions; i++ ))
do
  ./bench_remote /tmp/plasma2 $shmem $n4 $size >> $RESULTS_DIR/benchmark-$repetitions.$n4.$size.result
  offset=$(($offset+$n4))
done

elif [[ "$2" == 5 ]]; then
size=10000000
echo "" > $RESULTS_DIR/benchmark-$repetitions.$n5.$size.result
for (( i=0; i<$repetitions; i++ ))
do
  ./bench_remote /tmp/plasma2 $shmem $n5 $size >> $RESULTS_DIR/benchmark-$repetitions.$n5.$size.result
  offset=$(($offset+$n5))
done

elif [[ "$2" == 6 ]]; then
size=100000000
echo "" > $RESULTS_DIR/benchmark-$repetitions.$n6.$size.result
for (( i=0; i<$repetitions; i++ ))
do
  ./bench_remote /tmp/plasma2 $shmem $n6 $size >> $RESULTS_DIR/benchmark-$repetitions.$n6.$size.result
  offset=$(($offset+$n6))
done
fi

rm bench_remote

echo "Done"
echo "Results written to files ($RESULTS_DIR/benchmark-<cycles>.<num_size>.<object_size>)"