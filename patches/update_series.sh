#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <base>"
    exit 1
fi

base=$1

if [ -n "$CLEANUP_PATCHES" ]; then
    rm patches/*.patch
fi

echo "# Android Series" > patches/series
cat <<EOT > patches/series
#
# android-mainline patches
#
# Applies onto mainline $(git log -1 --format=%h $base) Linux $base
#
EOT

files=()  # keep track of used file names to deal with collisions

for sha1 in $(git rev-list $base.. --reverse); do

  # get the file name
  name=$(git show -s --format=%f $sha1)
  printf -v patch_file "%s.patch" $name

  # check for and work around collisions
  index=1
  while [[ " ${files[@]} " =~ " ${patch_file} " ]]; do
    ((index++))
    printf -v patch_file "%s-%d.patch" $name $index
  done
  files+=($patch_file)

  # write the actual patch file and update the series
  git format-patch $sha1 -1 --no-signoff    \
                            --keep-subject  \
                            --zero-commit   \
                            --no-signature  \
                            --stdout > patches/$patch_file
  echo $patch_file >> patches/series

done

