#!/bin/bash
algos=(reno)
loss=(l10 l20 l30 l40 l50)
#echo "algo：$1";
#python pro-loss.py --algo=$1
#python pro-owd.py --algo=$1
#python pro-jain.py --algo=$1
#python pro-utilit.py --algo=$1
for algo in ${algos[@]}
do
python pro-loss.py --algo=$algo
python pro-owd.py --algo=$algo
python pro-utilit.py --algo=$algo
python pro-rate.py --algo=$algo
done
for l in ${loss[@]}
do
    for element in ${algos[@]}
    do
    algo=$element$l
    python pro-loss.py --algo=$algo
    python pro-owd.py --algo=$algo
    python pro-utilit.py --algo=$algo
    python pro-rate.py --algo=$algo
    done
done


