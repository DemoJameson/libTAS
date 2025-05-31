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

#ifndef LIBTAS_MEMVALUE_H_INCLUDED
#define LIBTAS_MEMVALUE_H_INCLUDED

#include <cstdint>

enum RamType {
    RamUnsignedChar,
    RamChar,
    RamUnsignedShort,
    RamShort,
    RamUnsignedInt,
    RamInt,
    RamUnsignedLong,
    RamLong,
    RamFloat,
    RamDouble,
    RamArray,
    RamCString,
};

#define RAM_ARRAY_MAX_SIZE 15

typedef union {
    int8_t v_int8_t;
    uint8_t v_uint8_t;
    int16_t v_int16_t;
    uint16_t v_uint16_t;
    int32_t v_int32_t;
    uint32_t v_uint32_t;
    int64_t v_int64_t;
    uint64_t v_uint64_t;
    float v_float;
    double v_double;
    uint8_t v_array[RAM_ARRAY_MAX_SIZE+1];
    char v_cstr[RAM_ARRAY_MAX_SIZE+1];
} MemValueType;

namespace MemValue {
    
    /* Returns the size of a type index */
    int type_size(int type_index);
    
    /* Format a value to be shown */
    const char* to_string(const void* value, int value_type, bool hex);

    /* Format a value to be shown with specified array size for array type */
    const char* to_string(const void* value, int value_type, bool hex, int array_size);

    /* Extract a value from a string and type */
    MemValueType from_string(const char* str, int value_type, bool hex);
}

#endif
