space=" "
#directory contain source file
dir=$1
#current directory
src=$2
bin=$3
sim=$4
obj=$5
est=$src/est/est
dis=$src/gui/dis
#gcc objdump simprofile command
gcc=$bin/sslittle-na-sstrix-gcc
objdump=$bin/sslittle-na-sstrix-objdump
simprofile=$sim/sim-profile
#echo $gcc
cd $dir
for i in $ls *.c
do
    if [ -f $i ];
    then
       #echo $i
       file=$file$space$i
    fi
done
#echo $file
$gcc  -g -O2  $file -o  $obj
$dis          $obj
#$objdump -dl --prefix-address $obj >& $obj.dis
        
if [ -f "$obj.input" ]; 
then
  $simprofile -pcstat sim_num_insn $obj < $obj.input  >& $obj.out
else
  $simprofile -pcstat sim_num_insn $obj   >& $obj.out
fi
#perl $textprof $filename.dis $filename.out sim_num_insn_by_pc >& $filename.sim
$est -run CFG $obj
