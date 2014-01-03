# dump benchmark name
prog_name()
{
    echo $1
}



# get simulation time and ilp file for a benchmark
run_prog()
{
pipe_arg='-decode:width 2 -issue:width 2 -commit:width 2 -fetch:ifqsize 4 -ruu:size 16'
#bp_arg=' -bpred 2lev -bpred:2lev 1 128 2 1'
ic_arg=' -cache:il1 il1:16:32:1:l -mem:lat 30 2'
bp_arg=' -bpred perfect'
#ic_arg=' -cache:il1 none'
    args=$pipe_arg$bp_arg$ic_arg
    cd $1
    rm $1.lp
    time -p $est $args $1 
#grep total_cons $1.lp
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

est=$PWD/est
solve=$PWD/solve
bench_path=../benchmarks
#bench_path=../bench-O0
cd $bench_path

if [ "$1" == "" ]; then

    for i in * ; do
	if [ -d $i -a $i != "CVS" ]; then
	    run_prog $i >& aa
	    grep real aa | awk '{print $2}'
	    rm aa
	fi
    done
else
    run_prog $1
fi

cd ..
