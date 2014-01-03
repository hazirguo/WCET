DO_EST=0
DO_SOLVE=1


# dump benchmark name
prog_name()
{
    echo $1
}



# get simulation time and ilp file for a benchmark
run_prog()
{
    cd $1
    if [ $DO_SOLVE -eq 1 ]; then
#echo $1
#$solve $1.lp
$solve $1.lp | awk 'BEGIN{wcet=0; bm=0; cm=0} \
/objective/ { wcet=$NF} /bm/ { bm=$NF } /cm/ { cm=$NF } \
END{printf("%d %d %d\n", wcet, bm, cm)} '
#$solve $1.lp 2> /dev/null | awk '{if (NF == 4) printf("%d %d %d\n", $1, $3, $4)}'
    fi
    cd ..
}



# get code size for analysis
code_size()
{
    cd $1
    arg=`head -1 $1.arg`
    cat $1.arg | awk '{printf("0x%s 0x%s\n", $1, $2)}' | awk --non-decimal-data '{print $2-$1}'
    cd ..
}



#if [ "$1" == "" ]; then
#    main=$PWD/main
#else
#    main=$PWD/$1
#fi

est=$PWD/est
lp_solve_dir=$PWD/../lp_solve_5.5
solve="$lp_solve_dir/lp_solve -rxli $lp_solve_dir/libxli_CPLEX.so"
#solve=$PWD/solve
bench_path=../benchmarks
#bench_path=../bench-O0
cd $bench_path

if [ "$1" == "" ]; then

    for i in * ; do
	if [ -d $i -a $i != "CVS" ]; then
#echo -e -n $i "\t"
	    run_prog $i
	    #code_size $i
	fi
    done
else
    run_prog $1
fi

cd ..
