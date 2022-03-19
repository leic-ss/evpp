/************************************************************************
Modifications Copyright 2020 ~ 2021.
Author: ZhangLei
Email: shanshenshi@126.com

Original Copyright:
See URL: https://github.com/datatechnology/cornerstone
See URL: https://github.com/eBay/NuRaft

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0
**************************************************************************/

#ifndef _EV_LOGGER_HXX_
#define _EV_LOGGER_HXX_

#include <string>

#include <stdarg.h>
#include <stdint.h>
#include <time.h>
#include <stdio.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <deque>
#include <string>
#include <pthread.h>
#include <sys/time.h>
#include <string.h>

namespace evpp {

#ifndef _BASE_LOGGER_
#define _BASE_LOGGER_

#define EVLOG_FILE_NAME(x) strrchr( (x),'/')?strrchr( (x) ,'/')+1:(x)

#define EVLOG_LEVEL_ERROR 0
#define EVLOG_LEVEL_WARN  1
#define EVLOG_LEVEL_INFO  2
#define EVLOG_LEVEL_DEBUG 3
#define EVLOG_LEVEL_TRACE 4

// printf style log macro
#define _log_(level, l, ...)        \
    if (l && l->getLogLevel() >= level) \
        (l)->logMessage(level, EVLOG_FILE_NAME(__FILE__), __LINE__, __FUNCTION__, __VA_ARGS__)

#define _log_err(l, ...)    _log_(EVLOG_LEVEL_ERROR,   l, __VA_ARGS__)
#define _log_warn(l, ...)   _log_(EVLOG_LEVEL_WARN,    l, __VA_ARGS__)
#define _log_info(l, ...)   _log_(EVLOG_LEVEL_INFO,    l, __VA_ARGS__)
#define _log_debug(l, ...)  _log_(EVLOG_LEVEL_DEBUG,   l, __VA_ARGS__)
#define _log_trace(l, ...)  _log_(EVLOG_LEVEL_TRACE,   l, __VA_ARGS__)

#endif

class logger {
public:
    virtual void logMessage(int32_t level, const char *file, int32_t line, const char *function, const char *fmt, ...) = 0;

    virtual void setLogLevel(const char *level) = 0;

    virtual void setFileName(const char *filename, bool flag = false) = 0;

    virtual void setMaxFileSize( int64_t maxFileSize=0x40000000) = 0;

    virtual void setMaxFileIndex( int32_t maxFileIndex= 0x0F) = 0;

    virtual int32_t getLogLevel() = 0;
};

}

#endif //_LOGGER_HXX_
