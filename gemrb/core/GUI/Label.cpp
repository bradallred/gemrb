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

#include "GUI/Label.h"

#include "win32def.h"

#include "GameData.h"
#include "Interface.h"
#include "Sprite2D.h"
#include "Variables.h"
#include "GUI/Window.h"

namespace GemRB {

Label::Label(const Region& frame, Font* font, const String& string)
	: Control(frame)
{
	ControlType = IE_GUI_LABEL;
	this->font = font;
	useRGB = false;
	palette = NULL;
	SetColor(ColorWhite, ColorBlack);

	SetAlignment(IE_FONT_ALIGN_CENTER|IE_FONT_ALIGN_MIDDLE);
	SetFlags(IgnoreEvents, OP_OR);
	SetText(string);
}

Label::~Label()
{
	gamedata->FreePalette( palette );
}
/** Draws the Control on the Output Display */
void Label::DrawSelf(Region rgn, const Region& /*clip*/)
{
	if (font && Text.length()) {
		font->Print( rgn, Text, useRGB ? palette: NULL, Alignment);
	}

	if (AnimPicture) {
		int xOffs = ( frame.w / 2 ) - ( AnimPicture->Width / 2 );
		int yOffs = ( frame.h / 2 ) - ( AnimPicture->Height / 2 );
		Region r( rgn.x + xOffs, rgn.y + yOffs, (int)(AnimPicture->Width), AnimPicture->Height );
		core->GetVideoDriver()->BlitSprite( AnimPicture.get(), r.x + xOffs, r.y + yOffs, &r );
	}

}
/** This function sets the actual Label Text */
void Label::SetText(const String& string)
{
	Text = string;
	if (Alignment == IE_FONT_ALIGN_CENTER
		&& core->HasFeature( GF_LOWER_LABEL_TEXT )) {
		StringToLower(Text);
	}
	MarkDirty();
}
/** Sets the Foreground Font Color */
void Label::SetColor(Color col, Color bac)
{
	gamedata->FreePalette( palette );
	palette = new Palette( col, bac );
	MarkDirty();
}

void Label::SetAlignment(unsigned char Alignment)
{
	if (!font || frame.h <= font->LineHeight) {
		// FIXME: is this a poor way of determinine if we are single line?
		Alignment |= IE_FONT_SINGLE_LINE;
	} else if (frame.h < font->LineHeight * 2) {
		Alignment |= IE_FONT_NO_CALC;
	}
	this->Alignment = Alignment;
	if (Alignment == IE_FONT_ALIGN_CENTER) {
		if (core->HasFeature( GF_LOWER_LABEL_TEXT )) {
			StringToLower(Text);
		}
	}
	MarkDirty();
}

/** Simply returns the pointer to the text, don't modify it! */
String Label::QueryText() const
{
	return Text;
}

}
