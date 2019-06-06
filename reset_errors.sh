#!/usr/bin/env bash
for i in 1 2 3 4 5 6
do
  echo "joint $i"
  echo "error register before reset:"
  rosservice call /prbt/driver/get_object "node: 'prbt_joint_$i'
object: '1001'
cached: false"
  echo "error count before reset:"
  rosservice call /prbt/driver/get_object "node: 'prbt_joint_$i'
object: '1003sub0'
cached: false"
  echo "resetting:"
  rosservice call /prbt/driver/set_object "node: 'prbt_joint_$i'
object: '1003sub0'
value: '0'
cached: false"
  echo "error count after reset:"
  rosservice call /prbt/driver/get_object "node: 'prbt_joint_$i'
object: '1003sub0'
cached: false"
done

rosservice call /prbt/driver/recover
