/************************************************************************
Modifications Copyright 2020 ~ 2021.
Author: ZhangLei
Email: shanshenshi@126.com

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0
**************************************************************************/

#include "logger.h"

#include <thread>

#include <string.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>

#define gettid() syscall(SYS_gettid)

namespace evpp
{

const char * const CCLogger::_errstr[] = {"ERRO","WARN","INFO","DEBG", "TRAC"};
static const uint32_t HDR_SIZE = 128;
static const uint32_t MSG_SIZE = 4*1024;

CCLogger CCLogger::_logger;

CCLogger::CCLogger() {
    _fd = fileno(stderr);
    _level = EVLOG_LEVEL_INFO;
    _name = nullptr;
    _check = false;
    checkInterval = 60;
    nextCheckTime = 0;
    _maxFileSize = 0x20000000;
    _maxFileIndex = 0x0F;
    pthread_mutex_init(&_fileSizeMutex, nullptr );
    pthread_mutex_init(&_fileIndexMutex, nullptr );
    _flag = false;
}

CCLogger::~CCLogger() {
    if (_name != nullptr) {
        free(_name);
        _name = nullptr;
        close(_fd);
    }
    pthread_mutex_destroy(&_fileSizeMutex);
    pthread_mutex_destroy(&_fileIndexMutex);
}

void CCLogger::setLogLevel(const char *level)
{
    if (level == nullptr) return;
    int l = sizeof(_errstr)/sizeof(char*);
    for (int i=0; i<l; i++) {
        if (strcasecmp(level, _errstr[i]) == 0) {
            _level = i;
            break;
        }
    }
}

void CCLogger::setFileName(const char *filename, bool flag)
{
    bool need_closing = false;
    if (_name) {
        need_closing = true;
        free(_name);
        _name = NULL;
    }
    _name = strdup(filename);
    int fd = open(_name, O_RDWR | O_CREAT | O_APPEND | O_LARGEFILE, LOG_FILE_MODE);
    _flag = flag;
    if (!_flag)
    {
      dup2(fd, _fd);
      dup2(fd, 1);
      if (_fd != 2) dup2(fd, 2);
      if (fd != _fd) close(fd);
    }
    else
    {
      if (need_closing)
      {
        close(_fd);
      }
      _fd = fd;
    }
}

static char NEWLINE[1] = {'\n'};

void CCLogger::logMessage(int level,const char *file, int line, const char *function, const char *fmt, ...) {
    if (level>_level) return;

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    ::localtime_r((const time_t*)&tv.tv_sec, &tm);

    checkFile(tv.tv_sec);

    // thread_local std::thread::id tid = std::this_thread::get_id();
    //thread_local uint32_t tid_hash = std::hash<std::thread::id>{}(tid) % 0x10000;
    // thread_local pthread_t tid = pthread_self();
    static thread_local uint32_t tid = gettid();
    static thread_local uint32_t tid_hash = tid % 0x7FFFFFFF;

    char head[HDR_SIZE] = {0};
    char data1[MSG_SIZE] = {0};

    uint32_t avail_len = MSG_SIZE;
    va_list args;
    va_start(args, fmt);
    uint32_t data_size = vsnprintf(data1, MSG_SIZE, fmt, args);
    data_size = (data_size < MSG_SIZE) ? data_size : MSG_SIZE;
    va_end(args);
    avail_len = MSG_SIZE - data_size;

    uint32_t last_slash = 0;
    for (uint32_t ii=0; file && file[ii] != 0; ++ii) {
        if (file[ii] == '/' || file[ii] == '\\') last_slash = ii;
    }

    if (file && line && avail_len) {
        uint32_t cur_len = snprintf( data1 + data_size, avail_len, "\t[%s:%d, %s()]",
                                     file + ((last_slash)?(last_slash+1):0),
                                     line, function );
        cur_len = (cur_len < avail_len) ? cur_len : avail_len;
        data_size += cur_len;
    }

    if (avail_len == 0) {
        // remove trailing '\n'
        while (data1[data_size-1] == '\n') data_size --;
        data1[data_size-1] = '\0';
    }

    uint32_t head_size = snprintf(head,128,"%04d-%02d-%02d %02d:%02d:%02d.%06ld [%04x] [%-4s] ",
                                  tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
                                  tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec,
                                  tid_hash, _errstr[level]);
    head_size = (head_size < 128) ? head_size : 128;

    struct iovec vec[3];
    vec[0].iov_base = head;
    vec[0].iov_len = head_size;
    vec[1].iov_base = data1;
    vec[1].iov_len = data_size;
    vec[2].iov_base = NEWLINE;
    vec[2].iov_len = sizeof(NEWLINE);
    if (data_size > 0) {
      ::writev(_fd, vec, 3);
    }

    if ( _maxFileSize ) {
        pthread_mutex_lock(&_fileSizeMutex);
        off_t offset = ::lseek(_fd, 0, SEEK_END);
        if ( offset < 0 ){
            // we got an error , ignore for now
        } else {
            if ( static_cast<int64_t>(offset) >= _maxFileSize ) {
                rotateLog(nullptr);
            }
        }
        pthread_mutex_unlock(&_fileSizeMutex);
    }

    return;
}

void CCLogger::rotateLog(const char *filename, const char *fmt) 
{
    if (filename == nullptr && _name != nullptr) {
        filename = _name;
    }
    char wf_filename[256];
    if (filename != nullptr) {
      snprintf(wf_filename, sizeof(wf_filename), "%s.wf", filename);
    }

    if (access(filename, R_OK) == 0) {
        char oldLogFile[256];
        char old_wf_log_file[256];
        time_t t;
        time(&t);
        struct tm tm;
        localtime_r((const time_t*)&t, &tm);
        if (fmt != nullptr) {
            char tmptime[256] = {0};
            strftime(tmptime, sizeof(tmptime), fmt, &tm);
            snprintf(oldLogFile, sizeof(oldLogFile), "%s.%s", filename, tmptime);
            snprintf(old_wf_log_file, sizeof(old_wf_log_file), "%s.%s", wf_filename, tmptime);
        } else {
            sprintf(oldLogFile, "%s.%04d%02d%02d%02d%02d%02d",
                filename, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
            snprintf(old_wf_log_file, sizeof(old_wf_log_file), "%s.%04d%02d%02d%02d%02d%02d",
              wf_filename, tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
              tm.tm_hour, tm.tm_min, tm.tm_sec);
        }
        if ( _maxFileIndex > 0 ) {
            pthread_mutex_lock(&_fileIndexMutex);
            if ( _fileList.size() >= _maxFileIndex ) 
            {
                std::string oldFile = _fileList.front();
                _fileList.pop_front();
                unlink( oldFile.c_str());
            }
            _fileList.push_back(oldLogFile);
            pthread_mutex_unlock(&_fileIndexMutex);
        }
        rename(filename, oldLogFile);
    }
    int fd = open(filename, O_RDWR | O_CREAT | O_APPEND | O_LARGEFILE, LOG_FILE_MODE);
    if (!_flag) {
        dup2(fd, _fd);
        dup2(fd, 1);
        if (_fd != 2) dup2(fd, 2);
        close(fd);
    } else {
        if (_fd != 2) {
            close(_fd);
        }
        _fd = fd;
    }
}

void CCLogger::checkFile(uint32_t cur_sec)
{
    if (!_check || !_name) return ;
    if (cur_sec <= nextCheckTime) return ;

    nextCheckTime += cur_sec + checkInterval;

    struct stat stFile;
    struct stat stFd;

    fstat(_fd, &stFd);
    int err = stat(_name, &stFile);
    if ((err == -1 && errno == ENOENT)
        || (err == 0 && (stFile.st_dev != stFd.st_dev || stFile.st_ino != stFd.st_ino))) {
        int fd = open(_name, O_RDWR | O_CREAT | O_APPEND | O_LARGEFILE, LOG_FILE_MODE);
        if (!_flag) {
          dup2(fd, _fd);
          dup2(fd, 1);
          if (_fd != 2) dup2(fd, 2);
          close(fd);
        } else {
          if (_fd != 2) {
            close(_fd);
          }
          _fd = fd;
        }
    }
}

void CCLogger::setMaxFileSize( int64_t maxFileSize)
{
                                           // 1GB
    if ( maxFileSize < 0x0 || maxFileSize > 0x40000000) {
        maxFileSize = 0x40000000;//1GB
    }
    _maxFileSize = maxFileSize;
}

void CCLogger::setMaxFileIndex( int maxFileIndex )
{
    if ( maxFileIndex < 0x00 ) {
        maxFileIndex = 0x0F;
    }
    if ( maxFileIndex > 0x400 ) {//1024
        maxFileIndex = 0x400;//1024
    }
    _maxFileIndex = maxFileIndex;
}
}
