#!/usr/bin/bash
rel=../..
if [ x"$TOP" == x ]; then TOP=`pwd`/$rel; fi
. $rel/linux-ow.sh

if [ "$1" == "clean" ]; then
	do_clean
	rm -fv test.dsk v86kern.map
	exit 0
fi

if [ "$1" == "disk" ]; then
	# boot data disk
	make_msdos_data_disk test.dsk || exit 1
	mcopy -i test.dsk dos86s/test.exe ::test86.exe
	mcopy -i test.dsk dos386f/test.exe ::test386.exe
	mcopy -i test.dsk ../../dos4gw.dat ::dos4gw.exe
fi

if [[ "$1" == "build" || "$1" == "" ]]; then
	make_buildlist
	begin_bat

	what=all
	if [ x"$2" != x ]; then what="$2"; fi

	for name in $build_list; do
		do_wmake $name "$what" || exit 1
		bat_wmake $name "$what" || exit 1
	done

	end_bat
fi

