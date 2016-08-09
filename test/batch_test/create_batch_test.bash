#!/bin/bash

dir="$1"
outfile="batch_test.batch"

cat >"$outfile" <<HEADER_END
#NEXUS

begin mrbayes;
	set swapseed=1 seed=1 nowarn=yes autoclose=yes;

HEADER_END

n=0
for nex in "$dir"/*.nex; do
    if grep -q -i "data;" "$nex"; then
        n=$(( n + 1 ))

        cat <<DATASET_END
	[Data set # $n]
	execute $nex;
DATASET_END

        if ! grep -q -i "lset" "$nex"; then
            cat <<LSET_END
	lset rates=invgamma nst=6;
LSET_END
        fi

        cat <<ANALYSIS_END
	mcmc ng=1000 checkfr=100 file=crap;
	mcmc ng=2000 append=yes file=crap;
	sumt;
	sump;

ANALYSIS_END
    fi
done >>"$outfile"

cat >>"$outfile" <<FOOTER_END
end;
FOOTER_END
