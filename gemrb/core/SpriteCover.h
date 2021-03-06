/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2005 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 */

#ifndef SPRITECOVER_H
#define SPRITECOVER_H

#include "Polygon.h"
#include "Sprite2D.h"

namespace GemRB {

class GEM_EXPORT SpriteCover {
	Sprite2D* mask;
public:
	Point wp; // world coords for which the cover has been computed
	int dither;

	SpriteCover(const Point& wp, const Region& rgn, int dither = 0);
	~SpriteCover();

	const Sprite2D* GetMask() const { return mask; }
	bool Covers(int x, int y, int xpos, int ypos, int width, int height) const;

	void AddPolygon(Wall_Polygon* poly);
};


}

#endif
