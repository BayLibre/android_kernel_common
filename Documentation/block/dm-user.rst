.. SPDX-License-Identifier: GPL-2.0

=======
dm-user
=======

What is dm-user?
================

dm-user is a device mapper target that allows block accesses to be satisfied by
an otherwise unprivileged daemon running in userspace.

BIO Lifecycle
============

| "dd if=/dev/mapper/user ...             " | dm-user block server
|                                           |
|                                           | >sys_read()
|                                           |  >dev_read()
|                                           |   [sleep on c->wq]
|                                           |
| >sys_read()                               |
|  [FIXME: blk and dm calls]                |
|  >user_map()                              |
|   [enqueue message]                       |
|   [wake up c->wq]                         |
|  <user_map()                              |   [woken up]
| [sleep on BIO completion]                 |   [copy message to user]
|                                           |  <dev_read()
|                                           | <sys_read()
|                                           | 
|                                           | [obtain request data]
|                                           | 
|                                           | >sys_write()
|                                           |  >dev_write()
|                                           |   [copy message from user]
|                                           |   [complete BIO]
|  [woken up on BIO completion]             |  <dev_write()
| <sys_read()                               | <sys_write()
|                                           | 
| [write and loop]                          | [presumably loop for more messages]

Creating a dm-user Target
=========================

dm-user targets are created the same way as any other device mapper target: the
standard device mapper control device and ioctl() calls are used to create a
table with at least one target of the "user" type.  Like all other targets this
table entry needs a start/size pair.  The additional required argument is the
name of the control device that will be associated with this target.

````
user <start sector> <number of sectors> <path to control device>
````

so for example

````
dmsetup create blk <<EOF
0 1024 user 0 1024 ctl
EOF
dmsetup resume blk
````

will create a new device mapper block device availiable at `/dev/mapper/blk`,
consisting entirely of a single target which can be controlled via a stream of
messages passed over `/dev/dm-user/ctl`.

Kernel - userspace interface
============================

tools/testing/selftests/dm-user contains a handful of test daemons.
functional/simple-read-all.c 
