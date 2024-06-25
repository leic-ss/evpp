/************************************************************************
Modifications Copyright 2020 ~ 2021.
Author: ZhangLei
Email: shanshenshi@126.com

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0
**************************************************************************/

#pragma once

#include "evpp/evlog.h"

#include <stdarg.h>
#include <time.h>
#include <stdio.h>
#include <stdint.h>
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


// #define CCLOG_LEVEL(level) EVLOG_LEVEL_##level, EVLOG_FILE_NAME(__FILE__), __LINE__, __FUNCTION__
// #define CCLOG_NUM_LEVEL(level) level, EVLOG_FILE_NAME(__FILE__), __LINE__, __FUNCTION__
// #define CCLOG_PRINT(level, ...) CCLOG_LOGGER.logMessage(CCLOG_LOG_LEVEL(level), __VA_ARGS__)
// #define CCLOG_LOG_BASE(level, ...) (CCLOG_LOG_LEVEL_##level>CCLOG_LOGGER._level) ? (void)0 : CCLOG_PRINT(level, __VA_ARGS__) 
// #define CCLOG_LOG_US(level, _fmt_, args...) \
//   ((CCLOG_LOG_LEVEL_##level>CCLOG_LOGGER._level) ? (void)0 : CCLOG_LOG_BASE(level, "[%ld][%ld][%ld] " _fmt_, \
//                                                             pthread_self(), nds:CCLogger::get_cur_tv().tv_sec, \
//                                                             nds::CCLogger::get_cur_tv().tv_usec, ##args))

// #define CCLOG_LOG(level, _fmt_, args...) ((CCLOG_LOG_LEVEL_##level>CCLOG_LOGGER._level) ? (void)0 : CCLOG_LOG_BASE(level, _fmt_, ##args))

namespace evpp {

using std::deque;
using std::string;

class CCLogger : public logger {
public:

    static const mode_t LOG_FILE_MODE = 0644;
    CCLogger();
    ~CCLogger();

    void rotateLog(const char *filename, const char *fmt = nullptr);

    void logMessage(int32_t level, const char *file, int32_t line, const char *function, const char *fmt, ...);

    void setLogLevel(const char *level);
    void setFileName(const char *filename, bool flag = false);
    int32_t getLogLevel() { return _level; }

    void checkFile(uint32_t cur_sec);
    void setCheck(bool v) { _check = v; }
    void setCheckInterval(uint32_t check_interval) { checkInterval = check_interval; }

    void setMaxFileSize( int64_t maxFileSize=0x40000000);

    void setMaxFileIndex( int32_t maxFileIndex= 0x0F);

    static inline struct timeval get_cur_tv()
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return tv;
    }

    static CCLogger* instance() { return &_logger; }

private:
    int32_t _fd;
    char *_name;
    bool _check;
    uint32_t checkInterval;
    uint32_t nextCheckTime; // sec

    size_t _maxFileIndex;
    int64_t _maxFileSize;
    bool _flag;
    int32_t _level;

public:
    static CCLogger _logger;

private:
    std::deque<std::string> _fileList;
    static const char *const _errstr[];
    pthread_mutex_t _fileSizeMutex;
    pthread_mutex_t _fileIndexMutex;
};

}