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

#include "GUI/Control.h"
#include "GUI/GUIScriptInterface.h"
#include "GUI/Window.h"

#include "win32def.h"

#include "ControlAnimation.h"
#include "Interface.h"
#include "Sprite2D.h"
#include "Variables.h"

#include <cstdio>
#include <cstring>

namespace GemRB {

unsigned int Control::ActionRepeatDelay = 250;

const Control::ValueRange Control::MaxValueRange = std::make_pair(0, std::numeric_limits<ieDword>::max());

Control::Control(const Region& frame)
: View(frame) // dont pass superview to View constructor
{
	inHandler = 0;
	VarName[0] = 0;
	Value = 0;
	SetValueRange(MaxValueRange);

	animation = NULL;
	ControlType = IE_GUI_INVALID;

	actionTimer = NULL;
	repeatDelay = 0;
}

Control::~Control()
{
	assert(InHandler() == false);

	ClearActionTimer();

	delete animation;
}

bool Control::IsOpaque() const
{
	 return AnimPicture && AnimPicture->HasTransparency() == false;
}

void Control::SetText(const String* string)
{
	SetText((string) ? *string : L"");
}

void Control::SetAction(ControlEventHandler handler)
{
	actions[ACTION_DEFAULT] = handler;
}

void Control::SetAction(ControlEventHandler handler, Control::Action type, EventButton button,
						Event::EventMods mod, short count)
{
	ActionKey key(type, mod, button, count);
	if (handler) {
		actions[key] = handler;
	} else {
		// delete the entry if there is one instead of setting it to NULL
		ActionIterator it = actions.find(key);
		if (it != actions.end()) {
			actions.erase(it);
		}
	}
}

void Control::SetActionInterval(unsigned int interval)
{
	repeatDelay = interval;
	if (actionTimer) {
		actionTimer->SetInverval(repeatDelay);
	}
}

bool Control::SupportsAction(Action action)
{
	return SupportsAction(ActionKey(action));
}

bool Control::SupportsAction(const ActionKey& key)
{
	return actions.count(key);
}

bool Control::PerformAction()
{
	return PerformAction(ACTION_DEFAULT);
}

bool Control::PerformAction(Action action)
{
	return PerformAction(ActionKey(action));
}

bool Control::PerformAction(const ActionKey& key)
{
	if (IsDisabled()) {
		return false;
	}
	
	ActionIterator it = actions.find(key);
	if (it != actions.end()) {
		if (inHandler) {
			Log(ERROR, "Control", "Executing nested event handler. This is undefined behavior and may blow up.");
		}

		if (!window) {
			Log(WARNING, "Control", "Executing event handler for a control with no window. This most likely indicates a programming or scripting error.");
		}

		++inHandler;
		// TODO: detect caller errors, trap them???
		// TODO: add support for callbacks that return a bool?
		(it->second)(this);
		--inHandler;
		assert(inHandler >= 0);

		return true;
	}
	return false;
}

void Control::FlagsChanged(unsigned int /*oldflags*/)
{
	if (actionTimer && (flags&Disabled)) {
		ClearActionTimer();
	}
}

void Control::UpdateState(const char* varname, unsigned int val)
{
	if (strnicmp(VarName, varname, MAX_VARIABLE_LENGTH-1) == 0) {
		UpdateState(val);
	}
}

void Control::SetFocus()
{
	window->SetFocused(this);
	MarkDirty();
}

bool Control::IsFocused()
{
	return window->FocusedView() == this;
}

void Control::SetValue(ieDword val)
{
	ieDword oldVal = Value;
	Value = Clamp(val, range.first, range.second);

	if (oldVal != Value) {
		if (VarName[0] != 0) {
			core->GetDictionary()->SetAt( VarName, Value );
		}
		PerformAction(ValueChange);
		MarkDirty();
	}
}

void Control::SetValueRange(ValueRange r)
{
	range = r;
	SetValue(Value); // update the value if it falls outside the range
}

void Control::SetValueRange(ieDword min, ieDword max)
{
	SetValueRange(ValueRange(min, max));
}

void Control::SetAnimPicture(Sprite2D* newpic)
{
	AnimPicture = newpic;
	MarkDirty();
}

void Control::ClearActionTimer()
{
	if (actionTimer) {
		actionTimer->Invalidate();
		actionTimer = NULL;
	}
}

Timer* Control::StartActionTimer(const ControlEventHandler& action, unsigned int delay)
{
	class RepeatControlEventHandler : public Callback<void, void> {
		const ControlEventHandler action;
		Control* ctrl;

	public:
		RepeatControlEventHandler(const ControlEventHandler& handler, Control* c)
		: action(handler), ctrl(c) {}

		void operator()() const {
			// update the timer to use the actual repeatDelay
			ctrl->SetActionInterval(ctrl->repeatDelay);

			if (ctrl->VarName[0] != 0) {
				ieDword val = ctrl->GetValue();
				core->GetDictionary()->SetAt( ctrl->VarName, val );
				ctrl->window->RedrawControls( ctrl->VarName, val );
			}

			return action(ctrl);
		}
	};

	EventHandler h = new RepeatControlEventHandler(action, this);
	// always start the timer with ActionRepeatDelay
	// this way we have consistent behavior for the initial delay prior to switching to a faster delay
	return &core->SetTimer(h, (delay) ? delay : ActionRepeatDelay);
}
	
bool Control::HitTest(const Point& p) const
{
	if (!(flags & (IgnoreEvents | Invisible))) {
		return View::HitTest(p);
	}
	return false;
}

bool Control::OnMouseUp(const MouseEvent& me, unsigned short mod)
{
	ActionKey key(Click, mod, me.button, me.repeats);
	if (SupportsAction(key)) {
		PerformAction(key);
		ClearActionTimer();
	} else if (me.repeats > 1) {
		// also try a single-click in case there is no doubleclick handler
		// and there is never a triple+ click handler
		MouseEvent me2(me);
		me2.repeats = 1;
		OnMouseUp(me2, mod);
	}
	return true; // always handled
}

bool Control::OnMouseDown(const MouseEvent& me, unsigned short mod)
{
	ActionKey key(Click, mod, me.button, me.repeats);
	if (repeatDelay && SupportsAction(key)) {
		actionTimer = StartActionTimer(actions[key]);
	}
	return true; // always handled
}

void Control::OnMouseEnter(const MouseEvent& /*me*/, const DragOp*)
{
	PerformAction(HoverBegin);
}

void Control::OnMouseLeave(const MouseEvent& /*me*/, const DragOp*)
{
	PerformAction(HoverEnd);
}

bool Control::OnTouchDown(const TouchEvent& /*te*/, unsigned short /*mod*/)
{
	MethodCallback<Control, Control*, void>* cb = new MethodCallback<Control, Control*, void>(this, &Control::HandleTouchActionTimer);
	ControlEventHandler ceh(cb);
	actionTimer = StartActionTimer(ceh, 500); // TODO: this time value should be configurable
	return true; // always handled
}

bool Control::OnTouchUp(const TouchEvent& te, unsigned short mod)
{
	if (actionTimer) {
		// touch up before timer triggered
		// send the touch down+up events
		ClearActionTimer();
		View::OnTouchDown(te, mod);
		View::OnTouchUp(te, mod);
		return true;
	}
	return false; // touch was already handled as a long press
}

void Control::HandleTouchActionTimer(Control* ctrl)
{
	assert(ctrl == this);
	assert(actionTimer);

	ClearActionTimer();

	// long press action (GEM_MB_MENU)
	// TODO: we could save the mod value from OnTouchDown to support modifiers to the touch, but we dont have a use ATM
	ActionKey key(Click, 0, GEM_MB_MENU, 1);
	PerformAction(key);
}

ViewScriptingRef* Control::CreateScriptingRef(ScriptingId id, ResRef group)
{
	return new ControlScriptingRef(this, id, group);
}

}
