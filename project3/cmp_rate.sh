#!/usr/bin/env zsh
for file in *.trace;
  for n in 8 16 32 64;
    for r in 16 32 64 128 256 384 512 640
      ./vmsim -n $n -a nru -r $r ${file} > rate/${file}_${n}_${r}.txt 2> /dev/null
