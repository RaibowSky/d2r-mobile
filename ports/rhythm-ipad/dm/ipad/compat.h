#pragma once
#ifndef __ASSEMBLER__
#include <stdbool.h>
#include <stdatomic.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/xattr.h>
#ifndef ENODATA
#define ENODATA ENOATTR
#endif

#endif
