#!/usr/bin/env zsh
for file in *.trace;
  for n in 8 16 32 64;
    for alg in 'opt' 'clock' 'nru' 'rand';
        ./vmsim -n $n -a ${alg} -r $(($n * 4)) ${file} > algo/${file}_${alg}_${n}.txt 2> /dev/null
