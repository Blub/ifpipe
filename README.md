ifpipe v1.0
===========

This is a simple tool to pipe tap interfaces over standard output/input. Mostly
useful when socat's TUN ioctls throw an EPERM and you can't be bothered to
figure out how to use GOPEN with ioctl-bin instead. (Oh yes, I saw that :-P)
