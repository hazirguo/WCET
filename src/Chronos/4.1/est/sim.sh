

# dump benchmark name
prog_name()
{
    echo $1
}



# get simulation time and ilp file for a benchmark
run_prog()
{
pipe_arg='-decode:width 2 -issue:width 2 -commit:width 2 -fetch:ifqsize 4 -ruu:size 8'
#bp_arg=' -bpred 2lev -bpred:2lev 1 128 2 1'
#ic_arg=' -cache:il1 il1:16:32:2:l -mem:lat 30 2'
bp_arg=' -bpred perfect'
ic_arg=' -cache:il1 none'
#misc_arg=' -res:ialu 2 -res:memport 2 -ptrace fft.trc 22000:23000'
misc_arg=' -res:ialu 2 -res:memport 2'
    args=$pipe_arg$bp_arg$ic_arg$misc_arg
    cd $1

if [ "1"  = "1" ]; then
    $sim $args $1 >& tmp_sim_results
    tail -5 tmp_sim_results | head -3 | awk '{printf("%d ", $5)}'
    echo
    rm tmp_sim_results
else
    $sim $args $1
fi
    cd ..
}

# get code size for analysis
code_size()
{
    cd $1
    arg=`head -1 $1.arg`
    cat $1.arg | awk '{printf("0x%s 0x%s\n", $1, $2)}' | awk --non-decimal-data \
    '{print ($2-$1)}'
    cd ..
}



#if [ "$1" == "" ]; then
#    main=$PWD/main
#else
#    main=$PWD/$1
#fi

#sim=$PWD/../simplesim-3.0/sim-safe
sim=$PWD/../simplesim-3.0/sim-outorder
args=
bench_path=../benchmarks
#bench_path=../bench-O0
cd $bench_path

if [ "$1" == "" ]; then
    for i in * ; do
	if [ -d $i -a $i != "CVS" ]; then
#echo -e -n $i "\t"
	    run_prog $i
	fi
    done
else
    run_prog $1
fi

cd ..
