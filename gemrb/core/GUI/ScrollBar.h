/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 */

/**
 * @file ScrollBar.h
 * Declares ScrollBar widget for paging in long text windows.
 * This does not include scales and sliders, which are of Slider class.
 * @author The GemRB Project
 */

#ifndef SCROLLBAR_H
#define SCROLLBAR_H

#include "GUI/Control.h"

#include "Sprite2D.h"

#include "exports.h"

namespace GemRB {

class Sprite2D;

#define UP_PRESS	 0x0001
#define DOWN_PRESS   0x0010
#define SLIDER_GRAB  0x0100
#define SLIDER_HORIZONTAL 0x1000

/**
 * @class ScrollBar
 * Widget displaying scrollbars for paging in long text windows
 */
class GEM_EXPORT ScrollBar : public Control, public View::Scrollable {
public:
	enum IMAGE_TYPE {
		IMAGE_UP_UNPRESSED = 0,
		IMAGE_UP_PRESSED,
		IMAGE_DOWN_UNPRESSED,
		IMAGE_DOWN_PRESSED,
		IMAGE_TROUGH,
		IMAGE_SLIDER,

		IMAGE_COUNT
	};

	int StepIncrement;

	ScrollBar(const Region& frame, Sprite2D*[IMAGE_COUNT]);
	ScrollBar(const ScrollBar& sb);
	ScrollBar& operator=(const ScrollBar& sb);

	bool IsOpaque() const;

	/* scroll so the slider is centered at point p */
	void ScrollDelta(const Point& p);
	void ScrollTo(const Point& p);
	void ScrollUp();
	void ScrollDown();
	void ScrollBySteps(int steps);

	/** refreshes scrollbar if associated with VarName */
	void UpdateState(unsigned int Sum);
	bool TracksMouseDown() const { return true; }

private: //Private attributes
	/** Images for drawing the Scroll Bar */
	Holder<Sprite2D> Frames[IMAGE_COUNT];
	/** Scroll Bar Status */
	unsigned short State;

private:
	template <typename T>
	void Init(T images) {
		ControlType = IE_GUI_SCROLLBAR;
		State = 0;
		StepIncrement = 1;
		Size s = frame.Dimensions();

		for(int i=0; i < IMAGE_COUNT; i++) {
			Frames[i] = images[i];
			assert(Frames[i]);
			s.w = std::max(s.w, Frames[i]->Width);
		}

		SetValueRange(0, SliderPxRange());
		SetFrameSize(s);
	}

	void DrawSelf(Region drawFrame, const Region& clip);
	Point AxisPosFromValue() const;
	int GetFrameHeight(int frame) const;
	/** Range of the slider in pixels. The height - buttons - slider */
	int SliderPxRange() const;
	
protected:
	/** Mouse Button Down */
	bool OnMouseDown(const MouseEvent& /*me*/, unsigned short Mod);
	/** Mouse Button Up */
	bool OnMouseUp(const MouseEvent& /*me*/, unsigned short Mod);
	/** Mouse Drag Event */
	bool OnMouseDrag(const MouseEvent&);
	/** Mouse Wheel Scroll Event */
	bool OnMouseWheelScroll(const Point& delta);
	
	bool OnKeyPress(const KeyboardEvent& /*Key*/, unsigned short /*Mod*/);
};

}

#endif
