/*
    Copyright 2015-2020 Clément Gallet <clement.gallet@ens-lyon.org>

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

#ifndef LIBTAS_TYPEINDEX_H_INCLUDED
#define LIBTAS_TYPEINDEX_H_INCLUDED

#include "RamWatch.h"

/* This looks aaawwwful */
template <typename T> static inline int type_index() {return 0;}
template <> inline int type_index<unsigned char>() {return RamWatch::RamUnsignedChar;}
template <> inline int type_index<char>() {return RamWatch::RamChar;}
template <> inline int type_index<unsigned short>() {return RamWatch::RamUnsignedShort;}
template <> inline int type_index<short>() {return RamWatch::RamShort;}
template <> inline int type_index<unsigned int>() {return RamWatch::RamUnsignedInt;}
template <> inline int type_index<int>() {return RamWatch::RamInt;}
template <> inline int type_index<uint64_t>() {return RamWatch::RamUnsignedLong;}
template <> inline int type_index<int64_t>() {return RamWatch::RamLong;}
template <> inline int type_index<float>() {return RamWatch::RamFloat;}
template <> inline int type_index<double>() {return RamWatch::RamDouble;}

#endif
