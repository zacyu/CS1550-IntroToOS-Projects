#!/bin/bash

MOUNT_DIR=$1

if [ -d "${MOUNT_DIR}" ] ; then
  echo "Testing in directory ${MOUNT_DIR}";
else
  echo "${MOUNT_DIR} is not a directory";
  exit 1
fi

echo "Testing adding a subdirectory with overlength name:"
mkdir ${MOUNT_DIR}/dirtoolong

echo "Testing making too many subdirectories:"

for i in {1..30}
do
  echo " - Making subdirectory dir_${i}"
  mkdir ${MOUNT_DIR}/dir_${i}
done

echo "Testing adding a file with no extension:"
> ${MOUNT_DIR}/dir_1/file

echo "Testing adding a file with overlength name:"
> ${MOUNT_DIR}/dir_1/filetoolong.txt

echo "Testing adding a file with overlength extension:"
> ${MOUNT_DIR}/dir_1/file.text

echo "Testing adding too many files:"
for i in {1..20}
do
  echo "Hello World ${i}!" > ${MOUNT_DIR}/dir_1/hello_${i}.txt
done

echo "Testing adding a medium file:"
echo " - Writing 500 lines of Hello World to ${MOUNT_DIR}/dir_2/hello.txt"
for i in {1..500}
do
  echo "Hello World on Line ${i}!" >> ${MOUNT_DIR}/dir_2/hello.txt
done

echo "Testing adding large files:"
dd if=/dev/zero of=100k.bin count=100 bs=1024 status=none
for i in {3..10}
do
  echo " - Writing in ${MOUNT_DIR}/dir_${i}"
  for j in {1..10}
  do
    echo "  - Writing file large_${j}.txt"
    echo "Large File ${i}_${j} of 100 KB" > ${MOUNT_DIR}/dir_${i}/large_${j}.txt
    cat 100k.bin >> ${MOUNT_DIR}/dir_${i}/large_${j}.txt
  done
done
