#! /bin/sh
instance=1
algo=reno
file1=${instance}_${algo}_1_bw.txt
file2=${instance}_${algo}_2_bw.txt
file3=${instance}_${algo}_3_bw.txt
output=${algo}-${instance}
gnuplot<<!
set xlabel "time/s" 
set ylabel "rate/kbps"
set xrange [0:300]
set yrange [0:3000]
set term "png"
set output "${output}-bw-ability.png"
plot "${file1}" u 1:2 title "flow1" with lines lw 2 lc 1,\
"${file2}" u 1:2 title "flow2" with lines lw 2 lc 2,\
"${file3}" u 1:2 title "flow3" with lines lw 2 lc 3
set output
exit
!

file1=${instance}_${algo}_1_owd.txt
file2=${instance}_${algo}_2_owd.txt
file3=${instance}_${algo}_3_owd.txt
gnuplot<<!
set xlabel "time/s" 
set ylabel "delay/ms"
set xrange [0:300]
set yrange [50:200]
set term "png"
set output "${output}-delay.png"
plot "${file1}" u 1:3 title "flow1" with lines lw 2,\
"${file2}" u 1:3 title "flow2" with lines lw 2,\
"${file3}" u 1:3 title "flow3" with lines lw 2
set output
exit
!


