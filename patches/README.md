# Updating the Android Mainline Patch Series

1. Checkout the current base commit of the series (see series file)

   $ git checkout v5.4-rc1

2. Create a symbolic link to the patches subdirectory of android-mainline

   $ ln -s /where/ever/the/repo/is/patches .

3. Import the series into git

   $ git quiltimport

4. Do any necessary additions or transformations

5. Update the series. Make sure you use the correct base commit (e.g. after a
   rebase)

   $ patches/update_series.sh v5.4-rc1

