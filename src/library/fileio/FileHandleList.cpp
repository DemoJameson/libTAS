/*
    Copyright 2015-2024 Clément Gallet <clement.gallet@ens-lyon.org>

    This file is part of libTAS.

    libTAS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    libTAS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with libTAS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "FileHandleList.h"
#include "FileHandle.h"

#include "logging.h"
#include "Utils.h"
#include "GlobalState.h"
#include "global.h"
#ifdef __linux__
#include "inputs/evdev.h"
#include "inputs/jsdev.h"
#endif

#include <cstdlib>
#include <forward_list>
#include <mutex>
#include <unistd.h> // lseek
#include <sys/ioctl.h>
#include <fcntl.h>
#include <dirent.h>

namespace libtas {

namespace FileHandleList {

/* Constructing this (as well as other elements elsewhere as local-scope static
 * pointer. This forces the list to be constructed when we need it
 * (a bit like the Singleton pattern). If we simply declare the list as a
 * static variable, then we will be using it before it has time to be
 * constructed (because some other libraries will initialize and open some files),
 * resulting in a crash. Also, we allocate it dynamically and never free it, so
 * that it has a chance to survive every other game code that may use it.
 */
static std::forward_list<FileHandle>& getFileList() {
    static std::forward_list<FileHandle>* filehandles = new std::forward_list<FileHandle>;
    return *filehandles;
}

/* Mutex to protect the file list */
static std::mutex& getFileListMutex() {
    static std::mutex* mutex = new std::mutex;
    return *mutex;
}

void openFile(const char* file, int fd)
{
    if (fd < 0)
        return;

    std::lock_guard<std::mutex> lock(getFileListMutex());
    auto& filehandles = getFileList();

    /* Check if we already registered the file */
    for (const FileHandle &fh : filehandles) {
        if (fh.fds[0] == fd) {
            LOG(LL_WARN, LCF_FILEIO, "Opened file descriptor %d was already registered!", fd);
            return;
        }
    }

    filehandles.emplace_front(file, fd);
}

void openFile(const char* file, FILE* f)
{
    if (!f)
        return;

    std::lock_guard<std::mutex> lock(getFileListMutex());
    auto& filehandles = getFileList();

    /* Check if we already registered the file */
    for (const FileHandle &fh : filehandles) {
        if (fh.stream == f) {
            LOG(LL_WARN, LCF_FILEIO, "Opened file %p was already registered!", f);
            return;
        }
    }

    filehandles.emplace_front(file, f);
}

std::pair<int, int> createPipe(int flags) {
    int fds[2];
#ifdef __linux__
    if (pipe2(fds, flags) != 0)
#else
    if (pipe(fds) != 0)
#endif
        return std::make_pair(-1, -1);

    fcntl(fds[1], F_SETFL, O_NONBLOCK);
    std::lock_guard<std::mutex> lock(getFileListMutex());
    getFileList().emplace_front(fds);
    return std::make_pair(fds[0], fds[1]);
}

int fdFromFile(const char* file)
{
    std::lock_guard<std::mutex> lock(getFileListMutex());
    auto& filehandles = getFileList();

    for (const FileHandle &fh : filehandles) {
        if (fh.isPipe())
            continue;
        if (0 == strcmp(fh.fileNameOrPipeContents, file)) {
            return fh.fds[0];
        }
    }
    return -1;
}

bool closeFile(int fd)
{
    if (fd < 0)
        return true;

    std::lock_guard<std::mutex> lock(getFileListMutex());
    auto& filehandles = getFileList();

    /* Check if we track the file */
    for (auto prev = filehandles.before_begin(), iter = filehandles.begin(); iter != filehandles.end(); prev = iter++) {
        if (iter->fds[0] == fd) {
            if (iter->tracked) {
                /* Just mark the file as closed, and tells to not close the file */
                iter->closed = true;
                return false;
            }
            else {
#ifdef __unix__
                if (!unref_evdev(iter->fds[0]) || !unref_jsdev(iter->fds[0])) {
                    return false;
                }
#endif
                if (iter->isPipe()) {
                    NATIVECALL(close(iter->fds[1]));
                }
                filehandles.erase_after(prev);
                return true;
            }
        }
    }

    LOG(LL_DEBUG, LCF_FILEIO, "Unknown file descriptor %d", fd);
    return true;
}

void scanFileDescriptors()
{
    std::lock_guard<std::mutex> lock(getFileListMutex());
    auto& filehandles = getFileList();

    struct dirent *dp;

    DIR *dir = opendir("/proc/self/fd/");
    int dir_fd = dirfd(dir);
    
    while ((dp = readdir(dir))) {
        if (dp->d_type != DT_LNK)
            continue;

        int fd = std::atoi(dp->d_name);
        
        /* Skip own dir file descriptor */
        if (fd == dir_fd)
            continue;
        
        /* Skip stdin/out/err */
        if (fd < 3)
            continue;
        
        /* Search if fd is already registered */
        bool is_registered = false;
        for (const FileHandle &fh : filehandles) {
            if ((fh.fds[0] == fd) || (fh.fds[1] == fd)) {
                is_registered = true;
                break;
            }
        }
        
        if (is_registered)
            continue;

        /* Get symlink */
        char buf[1024] = {};
        ssize_t buf_size = readlinkat(dir_fd, dp->d_name, buf, 1024);
        if (buf_size == -1) {
            LOG(LL_WARN, LCF_FILEIO, "Cound not get symlink to file fd %d", fd);
        }
        else if (buf_size == 1024) {
            /* Truncation occured */
            buf[1023] = '\0';
            LOG(LL_WARN, LCF_FILEIO, "Adding file with fd %d to file handle list failed because symlink was truncated: %s", fd, buf);
        }
        else {
            /* Don't add special files, such as sockets or pipes */
            if ((buf[0] == '/') && (0 != strncmp(buf, "/dev/", 5))) {
                LOG(LL_DEBUG, LCF_FILEIO, "Add file %s with fd %d to file handle list", buf, fd);
                filehandles.emplace_front(buf, fd);
            }
        }
    }
    closedir(dir);
}


void trackAllFiles()
{
    std::lock_guard<std::mutex> lock(getFileListMutex());

    for (FileHandle &fh : getFileList()) {
        LOG(LL_DEBUG, LCF_FILEIO, "Track file %s (fd=%d,%d)", fh.fileName(), fh.fds[0], fh.fds[1]);
        fh.tracked = true;
        /* Save the file offset */
        if (!fh.closed) {
            if (fh.isPipe()) {
                /* By now all the threads are suspended, so we don't have to worry about
                 * racing to empty the pipe and possibly blocking.
                 */
                int pipeSize;
                MYASSERT(ioctl(fh.fds[0], FIONREAD, &pipeSize) == 0);
                LOG(LL_DEBUG, LCF_FILEIO, "Save pipe size: %d", pipeSize);
                fh.size = pipeSize;
                if (fh.size > 0) {
                    std::free(fh.fileNameOrPipeContents);
                    fh.fileNameOrPipeContents = static_cast<char *>(std::malloc(fh.size));
                    Utils::readAll(fh.fds[0], fh.fileNameOrPipeContents, fh.size);
                }
            }
            else {
                if (fh.stream) {
                    fflush(fh.stream);
                    fdatasync(fh.fds[0]);
                    fh.fileOffset = ftello(fh.stream);
                    fseeko(fh.stream, 0, SEEK_END);
                    fh.size = ftello(fh.stream);
                    fseeko(fh.stream, fh.fileOffset, SEEK_SET);
                }
                else {
                    fdatasync(fh.fds[0]);
                    fh.fileOffset = lseek(fh.fds[0], 0, SEEK_CUR);
                    fh.size = lseek(fh.fds[0], 0, SEEK_END);
                    lseek(fh.fds[0], fh.fileOffset, SEEK_SET);
                }
                LOG(LL_DEBUG, LCF_FILEIO, "Save file offset %jd and size %jd", fh.fileOffset, fh.size);
            }
        }
    }
}

void recoverAllFiles()
{
    std::lock_guard<std::mutex> lock(getFileListMutex());

    for (FileHandle &fh : getFileList()) {

        if (! fh.tracked) {
            LOG(LL_ERROR, LCF_FILEIO, "File %s (fd=%d,%d) not tracked when recovering", fh.fileName(), fh.fds[0], fh.fds[1]);
            continue;
        }

        /* Skip closed files */
        if (fh.closed) {
            continue;
        }

        int offset = fh.fileOffset;
        ssize_t ret;
        if (fh.isPipe()) {
            /* Only recover if we have valid contents */
            if (!fh.fileNameOrPipeContents || fh.size < 0) {
                continue;
            }

            /* Empty the pipe */
            int pipesize;
            MYASSERT(ioctl(fh.fds[0], FIONREAD, &pipesize) == 0);
            if (pipesize != 0) {
                char* tmp = static_cast<char *>(std::malloc(pipesize));
                Utils::readAll(fh.fds[0], tmp, pipesize);
                std::free(tmp);
            }

            ret = Utils::writeAll(fh.fds[1], fh.fileNameOrPipeContents, fh.size);
            std::free(fh.fileNameOrPipeContents);
            fh.fileNameOrPipeContents = nullptr;
            fh.size = -1;
        }
        else {
            /* Only seek if we have a valid offset */
            if (fh.fileOffset == -1) {
                continue;
            }

            off_t current_size = -1;
            if (fh.stream) {
                fseeko(fh.stream, 0, SEEK_END);
                current_size = ftello(fh.stream);
                ret = fseeko(fh.stream, fh.fileOffset, SEEK_SET);
            }
            else {
                current_size = lseek(fh.fds[0], 0, SEEK_END);
                ret = lseek(fh.fds[0], fh.fileOffset, SEEK_SET);
            }
            if (current_size != fh.size) {
                LOG(LL_WARN, LCF_FILEIO, "Restore file %s (fd=%d) changed size from %jd to %jd", fh.fileName(), fh.fds[0], fh.size, current_size);
            }
            fh.fileOffset = -1;
        }

        if (ret == -1) {
            LOG(LL_ERROR, LCF_FILEIO, "Error recovering %jd bytes into file %s (fd=%d,%d)", offset, fh.fileName(), fh.fds[0], fh.fds[1]);
        }
        else {
            LOG(LL_DEBUG, LCF_FILEIO, "Restore file %s (fd=%d,%d) offset to %jd", fh.fileName(), fh.fds[0], fh.fds[1], offset);
        }
    }
}

void closeUntrackedFiles()
{
    std::lock_guard<std::mutex> lock(getFileListMutex());

    for (FileHandle &fh : getFileList()) {
        if (! fh.tracked) {
            if (fh.isPipe()) {
                NATIVECALL(close(fh.fds[0]));
                NATIVECALL(close(fh.fds[1]));
            }
            else {
                if (fh.stream)
                    NATIVECALL(fclose(fh.stream));
                else
                    NATIVECALL(close(fh.fds[0]));
            }
            /* We don't bother updating the file handle list, because it will be
             * replaced with the list from the loaded savestate.
             */
            LOG(LL_DEBUG, LCF_FILEIO, "Close untracked file %s (fd=%d,%d)", fh.fileName(), fh.fds[0], fh.fds[1]);
        }
    }
}

}

}
