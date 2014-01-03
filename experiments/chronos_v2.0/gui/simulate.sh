#current path
rundir=$1
#benchmark path
benchdir=$2
#benchmark name
file=$3
#est directory
estdir=$rundir/est
#sim-outorder directoy
simdir=$4
#object file name
obj=$file
sim=$simdir/sim-outorder
cd $estdir
if [ -f "sim-processor.opt" ]; 
then
    echo "config"
    cd $benchdir
    if [ -f $file.input ];
    then
         echo "with input"
         $sim -config "$estdir/sim-processor.opt"  $obj < $file.input > $obj.sim
    else
         echo "no input"
         $sim -config "$estdir/sim-processor.opt" $obj  > $obj.sim
    fi
else
    echo  "no config"
    cd $benchdir
    if [ -f $file.input ];
    then
        echo "with input"
        $sim $obj < $file.input > $obj.sim
    else
        echo "no input"
        $sim $obj  > $obj.sim
    fi
fi 


