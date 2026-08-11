/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef AGS_ENGINE_MAIN_UPDATE_H
#define AGS_ENGINE_MAIN_UPDATE_H

namespace AGS3 {

enum MoveResult {
	kMoveResult_Abort     = -1,
	kMoveResult_Continue  = 0,
	kMoveResult_Done      = 1,
	kMoveResult_NextStage = 2
};

// Update MoveList of certain index, save current position. Returns result
// (see MoveResult enum), telling where it's ended on path.
// If still in the middle of a stage, then makes a step forward, that is -
// increments the progress by 1.0, which is not synced with a position immediately
// (will be next time it's called).
// If reached the stage's end, then does not make a step forward, but may still
// increment progress by the amount that was "unused" by the previous stage.
// If reached end of path, then *resets* mslot
// NOTE: in smooth move mode, when the next path's segment is reached,
// will carry over remaining progress onto the next segment. Otherwise won't.
// TODO: do not reset mslot in this function, reset externally instead.
MoveResult do_movelist_move(short &mslot, int &pos_x, int &pos_y, bool smooth_move);
// Recalculate derived (non-serialized) values in movelists
void restore_movelists();
// Update various things on the game frame (historical code mess...)
void update_stuff();

} // namespace AGS3

#endif
