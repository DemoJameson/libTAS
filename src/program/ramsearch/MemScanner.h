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

#ifndef LIBTAS_MEMSCANNER_H_INCLUDED
#define LIBTAS_MEMSCANNER_H_INCLUDED

#include "CompareOperations.h"
#include "MemSection.h"

#include <QtCore/QObject>
#include <string>
#include <vector>
#include <cstdint>

/* Store a section of the game memory */
class MemScanner : public QObject {
    Q_OBJECT
    
    public:
        /* Initialize the memory scanner with the memory scan path */
        static void init(std::string path);

        /* First memory scan. Returns 0 if no error, or error code */
        int first_scan(int mem_flags, int type, int align, CompareType ct, CompareOperator co, MemValueType cv, MemValueType dv, uintptr_t begin_address, uintptr_t end_address);

        /* Generic memory scan method. Returns 0 if no error, or error code */
        int scan(bool first, CompareType ct, CompareOperator co, MemValueType cv, MemValueType dv);

        /* Returns the total size of results */
        uint64_t scan_size() const;

        /* Returns the total number of scan results */
        uint64_t scan_count() const;

        /* Returns the number of scan results showed to the user (may be zero
         * when too many results have been found) */
        uint64_t display_scan_count() const;

        /* Get the address of the scan result with index */
        uintptr_t get_address(int index) const;

        /* Get the previous value of the scan result with index */
        const MemValueType* get_previous_value(int index) const;

        /* Get the current value of the scan result with index */
        MemValueType get_current_value(int index) const;

        /* Clear all results */
        void clear();

        /* Array of all memory sections parsed from /proc/self/maps */
        std::vector<MemSection> memsections;
        
        const int THREAD_COUNT = 4;
        const uint64_t DISPLAY_THRESHOLD = 10000; // don't display results when above threshold
        
        static std::string memscan_path; // directory containing all scan files
        static std::string addresses_path; // output file containing all scan addresses
        static std::string values_path; // output file containing all scan values
        
        int value_type;
        int value_type_size;
        CompareType compare_type;
        CompareOperator compare_operator;
        MemValueType compare_value;
        MemValueType different_value;
        int alignment;
        bool is_stopped = false;
        
    private:
        bool last_scan_was_region = true;
        uint64_t total_size = 0; // total size of the last scan (in bytes)

        std::vector<char> addresses; // scan addresses shown to the user
        std::vector<char> old_values; // scan previous values shown to the user

    signals:
        /* Update the scan progress bar */
        void signalProgress(int);
};

#endif
