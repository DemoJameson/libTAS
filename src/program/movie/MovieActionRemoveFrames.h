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

#ifndef LIBTAS_MOVIEACTIONREMOVEFRAMES_H_INCLUDED
#define LIBTAS_MOVIEACTIONREMOVEFRAMES_H_INCLUDED

#include "IMovieAction.h"
#include "../shared/inputs/AllInputs.h"

#include <vector>
#include <cstdint>

class MovieFileInputs;

class MovieActionRemoveFrames : public IMovieAction {
public:
    MovieActionRemoveFrames(uint64_t remove_from, uint64_t remove_to, MovieFileInputs* movie_inputs);

    void storeOldInputs() override;

    void undo() override;
    void redo() override;
    
private:
    std::vector<AllInputs> old_frames;
};

#endif
