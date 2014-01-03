sol=$1.sol
ilp=$1.ilp
dir=$2
cd $dir
rm -f $sol; cplex < $ilp >/dev/null 2>/dev/null; cat $sol | sed '/Log/d' | sed '/^$/d'

