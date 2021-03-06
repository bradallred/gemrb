# -*-python-*-
# GemRB - Infinity Engine Emulator
# Copyright (C) 2003 The GemRB Project
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#


# GUIMA.py - scripts to control map windows from GUIMA and GUIWMAP winpacks

###################################################

import GemRB
import GUICommon
import GUICommonWindows
from GUIDefines import *

PosX = 0
PosY = 0

###################################################
def InitMapWindow (Window):
	Window.AddAlias("WIN_MAP")

	# World Map
	Button = Window.GetControl (0)
	Button.SetText (20429)
	Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, OpenWMapWindow)

	# Add Note
	Button = Window.GetControl (1)
	Button.SetText (4182)
	Button.SetVarAssoc ("ShowMapNotes", IE_GUI_MAP_SET_NOTE)

	# Note text
	Text = Window.GetControl (4)
	Text.SetText ("")

	Edit = Window.CreateTextEdit (6, 54, 353, 416, 25, "FONTDLG", "")

	# Map Control
	# ronote and usernote are the pins for the notes
	Map = Window.CreateMapControl (3, 24, 23, 480, 360, Text, "USERNOTE", "RONOTE")
	GemRB.SetVar ("ShowMapNotes", IE_GUI_MAP_VIEW_NOTES)
	Map.SetVarAssoc ("ShowMapNotes", IE_GUI_MAP_VIEW_NOTES)

	Map.SetEvent (IE_GUI_MAP_ON_PRESS, SetMapNote)

	MapTable = GemRB.LoadTable( "MAPNAME" )
	MapName = MapTable.GetValue (GemRB.GetCurrentArea (), "STRING")
	
	Label = Window.GetControl (0x10000005)
	Label.SetText (MapName)
	#Label.SetTextColor (255, 0, 0)
	# 2 - map name?
	# 3 - map bitmap?
	# 4 - ???

	# Done
	Button = Window.GetControl (5)
	Button.SetText (1403)
	Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, lambda: Window.Close())
	Button.MakeEscape()

	return

def OpenWMapWindow ():
	GUICommonWindows.DisableAnimatedWindows ()

	Travel = GemRB.GetVar ("Travel")

	Window = GemRB.LoadWindow (0, "GUIWMAP")

	WMap = Window.CreateWorldMapControl (4, 0, 62, 640, 418, Travel, "FONTDLG")
	WMap.SetTextColor (IE_GUI_WMAP_COLOR_BACKGROUND, {'r' : 0x84, 'g' : 0x4a, 'b' : 0x2c, 'a' : 0x00})
	WMap.SetTextColor (IE_GUI_WMAP_COLOR_NORMAL, {'r' : 0x20, 'g' : 0x20, 'b' : 0x00, 'a' : 0xff})
	WMap.SetTextColor (IE_GUI_WMAP_COLOR_SELECTED, {'r' : 0x20, 'g' : 0x20, 'b' : 0x00, 'a' : 0xff})
	WMap.SetTextColor (IE_GUI_WMAP_COLOR_NOTVISITED, {'r' : 0x20, 'g' : 0x20, 'b' : 0x00, 'a' : 0xa0})
	WMap.SetAnimation ("WMPTY")
	#center on current area
	WMap.Scroll (0,0)
	WMap.Focus()

	# Done
	Button = Window.GetControl (0)
	Button.SetText (1403)
	Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, lambda: Window.Close())

ToggleMapWindow = GUICommonWindows.CreateTopWinLoader(3, "GUIMA", GUICommonWindows.ToggleWindow, InitMapWindow, None, WINDOW_TOP|WINDOW_HCENTER)
OpenMapWindow = GUICommonWindows.CreateTopWinLoader(3, "GUIMA", GUICommonWindows.OpenWindowOnce, InitMapWindow, None, WINDOW_TOP|WINDOW_HCENTER)

def NoteChanged ():
	MapWindow = GemRB.GetView("WIN_MAP")

	#shift focus to the static label
	Label = MapWindow.GetControl (4)
	Label.Focus()

	Edit = MapWindow.GetControl (6)
	Edit.SetEvent (IE_GUI_EDIT_ON_DONE, None)
	Text = Edit.QueryText ()
	Edit.SetText ("")
	GemRB.SetMapnote (PosX, PosY, 0, Text)
	#the mapcontrol is a bit sluggish, update the label now
	Label.SetText (Text)
	return
	
def SetMapNote ():
	global PosX, PosY

	if GemRB.GetVar ("ShowMapNotes") != IE_GUI_MAP_SET_NOTE:
		return

	MapWindow = GemRB.GetView("WIN_MAP")

	Label = MapWindow.GetControl (4)

	Edit = MapWindow.GetControl (6)
	Edit.SetEvent (IE_GUI_EDIT_ON_DONE, NoteChanged)
	Edit.Focus()

	#copy the text from the static label into the editbox
	Edit.SetText (Label.QueryText())
	Label.SetText ("")

	PosX = GemRB.GetVar("MapControlX")
	PosY = GemRB.GetVar("MapControlY")
	Map = MapWindow.GetControl (3)
	GemRB.SetVar ("ShowMapNotes", IE_GUI_MAP_VIEW_NOTES)
	Map.SetVarAssoc ("ShowMapNotes", IE_GUI_MAP_VIEW_NOTES)
	return

###################################################
# End of file GUIMA.py

