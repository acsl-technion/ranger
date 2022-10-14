#!/bin/bash

if [ ! -e bin/libranger.so ]; then
    echo "libranger must be built before Minimap2 can be patched"
    exit 1
fi

if [[ -d minimap2 ]]; then
    msg="Minimap2 directory already exists. Do you wish to remove it? [Ny]"
    read -p "$msg" ans
    if [[ $ans != [Yy] ]]; then
        echo "Aborting"
        exit 1
    fi
fi

rm minimap2 -rf
git clone https://github.com/lh3/minimap2.git minimap2
git -C minimap2 checkout 05a8a45 2>/dev/null

echo "Applying libranger patch to Minimap2"
git -C minimap2 apply ../minimap-ranger.patch

echo "Copying libranger to Minimap2 directory.."
cp bin/*.so minimap2/
cp lib/libranger.h minimap2/

echo "Making Minimap2"
make -C minimap2 ranger

