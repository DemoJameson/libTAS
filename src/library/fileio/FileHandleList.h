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

#ifndef LIBTAS_FILEHANDLELIST_H_INCLUDED
#define LIBTAS_FILEHANDLELIST_H_INCLUDED

#include <utility>
#include <cstdio>

namespace libtas {

namespace FileHandleList {

/* Register an opened file and file descriptor/stream */
void openFile(const char* file, int fd);
void openFile(const char* file, FILE* f);

/* Open and register an unnamed pipe */
std::pair<int, int> createPipe(int flags = 0);

/* Return the file descriptor from a filename */
int fdFromFile(const char* file);

/* Register a file closing, and returns if we must actually close the file */
bool closeFile(int fd);

/* Scan list of file descriptors using /proc/self/fd, and add file descriptors
 * that were not present. */
void scanFileDescriptors();

/* Mark all files as tracked, and save their offset */
void trackAllFiles();

/* Recover the offset of all tracked files */
void recoverAllFiles();

/* Close all untracked files before restoring a savestate */
void closeUntrackedFiles();

}

}

#endif
