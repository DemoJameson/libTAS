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

    Most of the code taken from DMTCP <http://dmtcp.sourceforge.net/>
*/

#ifndef LIBTAS_SAVESTATESAVING_H
#define LIBTAS_SAVESTATESAVING_H

#include "MemArea.h"
#include "../external/lz4.h"

namespace libtas {

struct StateHeader;

class SaveStateSaving
{
public:
    SaveStateSaving(int pagemapfd, int pagesfd, int selfpagemapfd);

    /* Import an area and fill some missing members */
    void processArea(Area* area);
    
    /* Saving the page flag */
    void savePageFlag(char flag);
    
    /* Save the entire memory page and the associated page flag */
    size_t queuePageSave(char* addr);
    
    /* Finish processing a memory area */
    size_t finishSave();

private:

    /* Flush the queue of noncompressed data, and returns the number of written bytes */
    size_t flushSave();
    
    /* Flush the queue of compressed data, and returns the number of written bytes */
    size_t flushCompressedSave();

    enum {
        PAGEMAP_CHUNK = 4096
    };

    /* Chunk of savestate pagemap values */
    char ss_pagemaps[PAGEMAP_CHUNK];

    /* Current index in the savestate pagemap array */
    int ss_pagemap_i = 0;

    LZ4_stream_t lz4s;

    /* File descriptors */
    int pmfd, pfd, spmfd;

    /* Address and size of the memory segment that is queued to be saved */
    char* queued_addr;
    size_t queued_size;

    /* Address and size of the whole memory section available to store
     * compressed pages. */
    char* queued_compressed_base_addr;
    int queued_compressed_max_size;

    /* Target address and size of the compressed memory segments that are queued to be saved */
    char* queued_target_addr;
    int queued_compressed_size;
};
}

#endif
