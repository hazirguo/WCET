
# dump benchmark name
prog_name()
{
    echo $1
}



# get simulation time and ilp file for a benchmark
run_prog()
{
    cd $1
    $main $1.lp 1 >&  tmp_solve_time
    cat tmp_solve_time | awk '{if (NF == 1) print $1}'
    rm tmp_solve_time
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

main=$PWD/solve
bench_path=$PWD/../benchmarks
cd $bench_path

if [ "$1" == "" ]; then

    for i in * ; do
	if [ -d $i ]; then
	    run_prog $i
	    #code_size $i
	fi
    done
else
    run_prog $1
#cd $1
#$main "$1"512.lp 1 >&  tmp_solve_time
#cat tmp_solve_time | awk '{if (NF == 1) print $1}'
#rm tmp_solve_time
#cd ..
fi

cd ..
