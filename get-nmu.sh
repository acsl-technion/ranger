#!/bin/bash

# Get libnuevomatchup
zip="libnuevomatchup-x86-64-linux-1.0.5.zip"
url="https://alonrashelbach.files.wordpress.com/2022/04/$zip"
if [ ! -e libnmu ]; then
    wget $url >&2
    unzip -d libnmu $zip >&2
    mv $zip libnmu/ >&2
fi

cp libnmu/libnuevomatchup.avx2.so libnmu/libnuevomatchup.so >&2

