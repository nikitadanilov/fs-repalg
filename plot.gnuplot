#set terminal latex;
#set terminal epslatex color;
#set terminal png;
set terminal postscript landscape color;
#set terminal x11;
#clear;
set data style linespoints;
set noborder;

set grid;
set bmargin 4;

set xtics ("4K" 0, "8K" 1, "16K" 2, "32K" 3, "64K" 4, "128K" 5, "256K" 6, "512K" 7, "1M" 8, "2M" 9, "4M" 10, "8M" 11, "16M" 12, "32M" 13, "64M" 14, "128M" 15, "256M" 16, "512M" 17, "1G" 18, "2G" 19, "4G" 20, "8G" 21, "16G" 22, "32G" 23)

set xtics rotate;

#
# OPT     7
# FIFO    8
# FIFO2   12
# LRU     19
# ARC     9
# CAR     13
# WORST   1
# RANDOM  4
#
# LINUX   0
# 
# 2Q:25:10 30
#
#

set output "ps/modern-lru-opt.ps"
set key bottom;
plot \
     '2q.25.10.hits'  using (log($1)/log(2)):($2) title '2Q:25:10' lt 30, \
     'car.hits'    using (log($1)/log(2)):($2) title 'CAR' lt 13, \
     'arc.hits'    using (log($1)/log(2)):($2) title 'ARC' lt 9, \
     'lru.hits'    using (log($1)/log(2)):($2) title 'LRU' lt 19, \
     'opt.hits'    using (log($1)/log(2)):($2) title 'OPT' lt 7;

set output "ps/modern-lru-opt-16M-4G.ps"
plot [12:20] \
     '2q.25.10.hits'  using (log($1)/log(2)):($2) title '2Q:25:10' lt 30, \
     'car.hits'    using (log($1)/log(2)):($2) title 'CAR' lt 13, \
     'arc.hits'    using (log($1)/log(2)):($2) title 'ARC' lt 9, \
     'lru.hits'    using (log($1)/log(2)):($2) title 'LRU' lt 19, \
     'opt.hits'    using (log($1)/log(2)):($2) title 'OPT' lt 7;

set output "ps/all.ps"
plot \
     '2q.25.10.hits'  using (log($1)/log(2)):($2) title '2Q:25:10' lt 30, \
     'car.hits'    using (log($1)/log(2)):($2) title 'CAR' lt 13, \
     'arc.hits'    using (log($1)/log(2)):($2) title 'ARC' lt 9, \
     'random.hits' using (log($1)/log(2)):($2) title 'RANDOM' lt 4, \
     'lru.hits'    using (log($1)/log(2)):($2) title 'LRU' lt 19, \
     'fifo.hits'   using (log($1)/log(2)):($2) title 'FIFO' lt 8, \
     'fifo2.hits'  using (log($1)/log(2)):($2) title 'FIFO2' lt 12, \
     'linux.hits'  using (log($1)/log(2)):($2) title 'LINUX' lt 0, \
     'worst.hits'  using (log($1)/log(2)):($2) title 'WORST' lt 1, \
     'opt.hits'    using (log($1)/log(2)):($2) title 'OPT' lt 7;
#pause -1;

set output "ps/lru-linux-worst.ps"
plot \
     'lru.hits'    using (log($1)/log(2)):($2) title 'LRU' lt 19, \
     'linux.hits'  using (log($1)/log(2)):($2) title 'LINUX' lt 0, \
     'worst.hits'  using (log($1)/log(2)):($2) title 'WORST' lt 1;

set output "ps/fifo-linux-worst.ps"
plot \
     'fifo.hits'   using (log($1)/log(2)):($2) title 'FIFO' lt 8, \
     'linux.hits'  using (log($1)/log(2)):($2) title 'LINUX' lt 0, \
     'worst.hits'  using (log($1)/log(2)):($2) title 'WORST' lt 1;

set output "ps/fifo2-linux-worst.ps"
plot \
     'fifo2.hits'  using (log($1)/log(2)):($2) title 'FIFO2' lt 12, \
     'linux.hits'  using (log($1)/log(2)):($2) title 'LINUX' lt 0, \
     'worst.hits'  using (log($1)/log(2)):($2) title 'WORST' lt 1;

set output "ps/2q-linux-worst.ps"
plot \
     '2q.25.10.hits'  using (log($1)/log(2)):($2) title '2Q:25:10' lt 30, \
     'linux.hits'  using (log($1)/log(2)):($2) title 'LINUX' lt 0, \
     'worst.hits'  using (log($1)/log(2)):($2) title 'WORST' lt 1;

set output "ps/arc-linux-worst.ps"
plot \
     'arc.hits'    using (log($1)/log(2)):($2) title 'ARC' lt 9, \
     'linux.hits'  using (log($1)/log(2)):($2) title 'LINUX' lt 0, \
     'worst.hits'  using (log($1)/log(2)):($2) title 'WORST' lt 1;

set output "ps/2q-var-lru-opt.ps"
set key bottom;
plot \
     '2q.25.10.hits'  using (log($1)/log(2)):($2) title '2Q:25:10', \
     '2q.25.20.hits'  using (log($1)/log(2)):($2) title '2Q:25:20', \
     '2q.25.30.hits'  using (log($1)/log(2)):($2) title '2Q:25:30', \
     '2q.25.40.hits'  using (log($1)/log(2)):($2) title '2Q:25:40', \
     '2q.25.50.hits'  using (log($1)/log(2)):($2) title '2Q:25:50', \
     'lru.hits'    using (log($1)/log(2)):($2) title 'LRU' lt 19, \
     'opt.hits'    using (log($1)/log(2)):($2) title 'OPT' lt 7;

set output "ps/sfifo-var-lru.ps"
set key bottom;
plot \
     'sfifo.0.hits'  using (log($1)/log(2)):($2) title 'SFIFO:0', \
     'sfifo.1.hits'  using (log($1)/log(2)):($2) title 'SFIFO:1', \
     'sfifo.2.hits'  using (log($1)/log(2)):($2) title 'SFIFO:2', \
     'sfifo.3.hits'  using (log($1)/log(2)):($2) title 'SFIFO:3', \
     'sfifo.4.hits'  using (log($1)/log(2)):($2) title 'SFIFO:4', \
     'sfifo.5.hits'  using (log($1)/log(2)):($2) title 'SFIFO:5', \
     'sfifo.6.hits'  using (log($1)/log(2)):($2) title 'SFIFO:6', \
     'sfifo.7.hits'  using (log($1)/log(2)):($2) title 'SFIFO:7', \
     'sfifo.8.hits'  using (log($1)/log(2)):($2) title 'SFIFO:8', \
     'sfifo.9.hits'  using (log($1)/log(2)):($2) title 'SFIFO:9', \
     'sfifo.10.hits'  using (log($1)/log(2)):($2) title 'SFIFO:10', \
     'sfifo.11.hits'  using (log($1)/log(2)):($2) title 'SFIFO:11', \
     'sfifo.15.hits'  using (log($1)/log(2)):($2) title 'SFIFO:15', \
     'sfifo.20.hits'  using (log($1)/log(2)):($2) title 'SFIFO:12', \
     'lru.hits'    using (log($1)/log(2)):($2) title 'LRU' lt 19;

set output "ps/old-opt-worst.ps"
set key bottom;
plot \
     'worst.hits'  using (log($1)/log(2)):($2) title 'WORST' lt 1, \
     'random.hits' using (log($1)/log(2)):($2) title 'RANDOM' lt 4, \
     'lru.hits'    using (log($1)/log(2)):($2) title 'LRU' lt 19, \
     'fifo.hits'   using (log($1)/log(2)):($2) title 'FIFO' lt 8, \
     'fifo2.hits'  using (log($1)/log(2)):($2) title 'FIFO2' lt 12, \
     'opt.hits'    using (log($1)/log(2)):($2) title 'OPT' lt 7;
#pause -1;

set output "ps/old-opt-2M-32M.ps"
plot [9:13] \
     'random.hits' using (log($1)/log(2)):($2) title 'RANDOM' lt 4, \
     'lru.hits'    using (log($1)/log(2)):($2) title 'LRU' lt 19, \
     'fifo.hits'   using (log($1)/log(2)):($2) title 'FIFO' lt 8, \
     'fifo2.hits'  using (log($1)/log(2)):($2) title 'FIFO2' lt 12, \
     'linux.hits'  using (log($1)/log(2)):($2) title 'LINUX' lt 0, \
     'opt.hits'    using (log($1)/log(2)):($2) title 'OPT' lt 7;
#pause -1;

set output "ps/old-opt-16M-4G.ps"
plot [12:20] \
     'random.hits' using (log($1)/log(2)):($2) title 'RANDOM' lt 4, \
     'lru.hits'    using (log($1)/log(2)):($2) title 'LRU' lt 19, \
     'fifo.hits'   using (log($1)/log(2)):($2) title 'FIFO' lt 8, \
     'fifo2.hits'  using (log($1)/log(2)):($2) title 'FIFO2' lt 12, \
     'linux.hits'  using (log($1)/log(2)):($2) title 'LINUX' lt 0, \
     'opt.hits'    using (log($1)/log(2)):($2) title 'OPT' lt 7;
#pause -1;

set output "ps/old-opt-256M-4G.ps"
plot [16:20] \
     'random.hits' using (log($1)/log(2)):($2) title 'RANDOM' lt 4, \
     'lru.hits'    using (log($1)/log(2)):($2) title 'LRU' lt 19, \
     'fifo.hits'   using (log($1)/log(2)):($2) title 'FIFO' lt 8, \
     'fifo2.hits'  using (log($1)/log(2)):($2) title 'FIFO2' lt 12, \
     'linux.hits'  using (log($1)/log(2)):($2) title 'LINUX' lt 0, \
     'opt.hits'    using (log($1)/log(2)):($2) title 'OPT' lt 7;
#pause -1;

set output "ps/old-vs-worst.ps"
set key top;
plot \
     'random.worst' using (log($1)/log(2)):($2-$3) title 'RANDOM' lt 4, \
     'lru.worst'    using (log($1)/log(2)):($2-$3) title 'LRU' lt 19, \
     'fifo.worst'   using (log($1)/log(2)):($2-$3) title 'FIFO' lt 8, \
     'fifo2.worst'  using (log($1)/log(2)):($2-$3) title 'FIFO2' lt 12, \
     'linux.worst'  using (log($1)/log(2)):($2-$3) title 'LINUX' lt 0, \
     'opt.worst'    using (log($1)/log(2)):($2-$3) title 'OPT' lt 7;
#pause -1;

set output "ps/old-vs-opt.ps"
set key bottom;
plot \
     'worst.opt'  using (log($1)/log(2)):(100.0 - $3+$2) title 'WORST' lt 1, \
     'random.opt' using (log($1)/log(2)):(100.0 - $3+$2) title 'RANDOM' lt 4, \
     'lru.opt'    using (log($1)/log(2)):(100.0 - $3+$2) title 'LRU' lt 19, \
     'linux.opt'  using (log($1)/log(2)):(100.0 - $3+$2) title 'LINUX' lt 0, \
     'fifo.opt'   using (log($1)/log(2)):(100.0 - $3+$2) title 'FIFO' lt 8, \
     'fifo2.opt'  using (log($1)/log(2)):(100.0 - $3+$2) title 'FIFO2' lt 12;
#pause -1;

set output "ps/all-vs-opt.ps"
set key bottom;
plot \
     'random.opt'   using (log($1)/log(2)):(100.0 - $3+$2) title 'RANDOM' lt 4,\
     'lru.opt'      using (log($1)/log(2)):(100.0 - $3+$2) title 'LRU' lt 19, \
     'fifo.opt'     using (log($1)/log(2)):(100.0 - $3+$2) title 'FIFO' lt 8, \
     'fifo2.opt'    using (log($1)/log(2)):(100.0 - $3+$2) title 'FIFO2' lt 12, \
     '2q.25.10.opt' using (log($1)/log(2)):(100.0 - $3+$2) title '2Q:25:10' lt 30, \
     'car.opt'      using (log($1)/log(2)):(100.0 - $3+$2) title 'CAR' lt 13, \
     'linux.opt'    using (log($1)/log(2)):(100.0 - $3+$2) title 'LINUX' lt 0, \
     'arc.opt'      using (log($1)/log(2)):(100.0 - $3+$2) title 'ARC' lt 9;
#pause -1;

set output "ps/all-vs-worst.ps"
set key top;
plot \
     'random.worst' using (log($1)/log(2)):($2-$3) title 'RANDOM' lt 4, \
     'lru.worst'    using (log($1)/log(2)):($2-$3) title 'LRU' lt 19, \
     'fifo.worst'   using (log($1)/log(2)):($2-$3) title 'FIFO' lt 8, \
     'fifo2.worst'  using (log($1)/log(2)):($2-$3) title 'FIFO2' lt 12, \
     '2q.25.10.worst' using (log($1)/log(2)):($2-$3) title '2Q:25:10' lt 30, \
     'car.worst'    using (log($1)/log(2)):($2-$3) title 'CAR' lt 13, \
     'linux.worst'  using (log($1)/log(2)):($2-$3) title 'LINUX' lt 0, \
     'arc.worst'    using (log($1)/log(2)):($2-$3) title 'ARC' lt 9, \
     'opt.worst'    using (log($1)/log(2)):($2-$3) title 'OPT' lt 7;

set output "ps/all-vs-random.ps"
set key top;
plot \
     'lru.random'    using (log($1)/log(2)):($2-$3) title 'LRU' lt 19, \
     'fifo.random'   using (log($1)/log(2)):($2-$3) title 'FIFO' lt 8, \
     'fifo2.random'  using (log($1)/log(2)):($2-$3) title 'FIFO2' lt 12, \
     '2q.25.10.random' using (log($1)/log(2)):($2-$3) title '2Q:25:10' lt 30, \
     'car.random'    using (log($1)/log(2)):($2-$3) title 'CAR' lt 13, \
     'linux.random'  using (log($1)/log(2)):($2-$3) title 'LINUX' lt 0, \
     'arc.random'    using (log($1)/log(2)):($2-$3) title 'ARC' lt 9, \
     'opt.random'    using (log($1)/log(2)):($2-$3) title 'OPT' lt 7;
