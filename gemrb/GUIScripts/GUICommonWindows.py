# -*-python-*-
# GemRB - Infinity Engine Emulator
# Copyright (C) 2003-2004 The GemRB Project
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#


# GUICommonWindows.py - functions to open common
# windows in lower part of the screen
###################################################

import GemRB
from GUIDefines import *
from ie_stats import *
from ie_modal import *
from ie_action import *
from ie_slots import SLOT_QUIVER
from ie_restype import RES_2DA
from GameCheck import MAX_PARTY_SIZE
import GameCheck
import GUICommon
import CommonTables
import LUCommon
import InventoryCommon
if not GameCheck.IsPST():
  import Spellbook  ##not used in pst - YET

FRAME_PC_SELECTED = 0
FRAME_PC_TARGET   = 1

PortraitWindow = None
OptionsWindow = None
ActionsWindow = None
CurrentWindow = None
ActionBarControlOffset = 0
ScreenHeight = GemRB.GetSystemVariable (SV_HEIGHT)

if GameCheck.IsIWD2():
	MageSpellsKey = 'Spellbook'
	CharacterStatsKey = 'Character_Record'
elif GameCheck.IsPST():
	MageSpellsKey = 'Mage_Spells'
	CharacterStatsKey = 'Character_Stats'
else:
	MageSpellsKey = 'Wizard_Spells'
	CharacterStatsKey = 'Character_Record'

#The following tables deal with the different control indexes and string refs of each game
#so that actual interface code can be game neutral
#the dictionary keys match entries in keymap.2da
AITip = {"Deactivate" : 15918, "Enable" : 15917}
if GameCheck.IsPST(): #Torment
	import GUIClasses
	TimeWindow = None
	PortWindow = None
	MenuWindow = None
	MainWindow = None
	DiscWindow = None
	AITip = {	"Deactivate" : 41631,	"Enable" : 41646 }
	OptionTip = { #dictionary to the stringrefs in each games dialog.tlk
	'Inventory' : 41601,'Map': 41625, MageSpellsKey : 41624, 'Priest_Spells': 4709, CharacterStatsKey : 4707,'Journal': 41623,
	'Options' : 41626,'Rest': 41628,'Follow': 41647,'Expand': 41660,'Toggle_AI' : 1,'Return_To_Game' : 1,'Party' : 1
	}
	OptionControl = { #dictionary to the control indexes in the window (.CHU)
	'Inventory' : 1, 'Map' : 2, MageSpellsKey : 3, 'Priest_Spells': 7, CharacterStatsKey : 5, 'Journal': 6,
	'Options' : 8, 'Rest': 9, 'Follow': 0, 'Expand': 10, 'Toggle_AI': 4,
	'Return_To_Game': 0, 'Party' : 8 , 'Time': 9 #not in pst
	}
elif GameCheck.IsIWD2(): #Icewind Dale 2
	OptionTip = {
	'Inventory' : 16307, 'Map': 16310, MageSpellsKey : 16309, CharacterStatsKey : 16306, 'Journal': 16308,
	'Options' : 16311, 'Rest': 11942, 'Follow': 41647, 'Expand': 41660, 'Toggle_AI' : 1,'Return_To_Game' : 16313,  'Party' : 16312,
	'SelectAll': 10485
	}
	OptionControl = {
	'Inventory' : 5, 'Map' : 7, CharacterStatsKey : 8, 'Journal': 6,
	'Options' : 9, 'Rest': 12, 'Follow': 0, 'Expand': 10, 'Toggle_AI': 14,
	'Return_To_Game': 0, 'Party' : 13,  'Time': 10, #not in pst
	MageSpellsKey: 4, 'SelectAll': 11
	}
else: # Baldurs Gate, Icewind Dale
	OptionTip = {
	'Inventory' : 16307, 'Map': 16310, MageSpellsKey : 16309, 'Priest_Spells': 14930, CharacterStatsKey : 16306, 'Journal': 16308,
	'Options' : 16311, 'Rest': 11942, 'Follow': 41647,  'Expand': 41660, 'Toggle_AI' : 1, 'Return_To_Game' : 16313, 'Party' : 16312
	}
	OptionControl = {
	'Inventory' : 3, 'Map' : 1, MageSpellsKey : 5, 'Priest_Spells': 6, CharacterStatsKey : 4, 'Journal': 2,
	'Options' : 7, 'Rest': 9, 'Follow': 0, 'Expand': 10, 'Toggle_AI': 6,
	'Return_To_Game': 0, 'Party' : 8, 'Time': 9 #not in pst
	}

# Generic option button init. Pass it the options window. Index is a key to the dicts,
# IsPage means whether the game should mark the button selected
def InitOptionButton(Window, Index, IsPage=True, HotKey=True):
	Button = Window.GetControl (OptionControl[Index])
	if not Button:
		print "InitOptionButton cannot find the button: " + Index
		return

	Button.SetTooltip (OptionTip[Index])
	if HotKey:
		Button.SetHotKey (Index, True)
	if IsPage:
		Button.SetVarAssoc ("SelectedWindow", OptionControl[Index])
		Button.SetFlags(IE_GUI_BUTTON_RADIOBUTTON, OP_OR)
	return Button

##these defaults don't seem to break the games other than pst
def SetupMenuWindowControls (Window, Gears=None, CloseWindowCallback=None):
	"""Binds all of the basic controls and windows to the options pane."""
	
	# needed for all the Open*Window callbacks in the OptionsWindow
	import GUIJRNL
	import GUIMA
	import GUIINV
	import GUIOPT
	if GameCheck.IsIWD2():
		# one spellbook for all spell types
		import GUISPL
	else:
		import GUIMG
		import GUIPR
	import GUIREC

	global OptionsWindow, ActionBarControlOffset

	OptionsWindow = Window

	bg1 = GameCheck.IsBG1()
	bg2 = GameCheck.IsBG2()
	iwd1 = GameCheck.IsIWD1()
	how = GameCheck.HasHOW()
	iwd2 = GameCheck.IsIWD2()
	pst = GameCheck.IsPST()
	#store these instead of doing 50 calls...

	if iwd2: # IWD2 has one spellbook to rule them all
		ActionBarControlOffset = 6 #portrait and action window were merged

		Button = InitOptionButton(Window, MageSpellsKey, True)

		# AI
		Button = InitOptionButton(Window, 'Toggle_AI')
		AIPress (0) #this initialises the state and tooltip

		# Select All
		Button = InitOptionButton(Window, 'SelectAll', True, False)
		Button.SetEvent(IE_GUI_BUTTON_ON_PRESS, GUICommon.SelectAllOnPress)
	elif pst: #pst has these three controls here instead of portrait pane
		# (Un)Lock view on character
		Button = InitOptionButton(Window, 'Follow', True, False)  # or 41648 Unlock ...
		Button.SetEvent(IE_GUI_BUTTON_ON_PRESS, OnLockViewPress)
		# AI
		Button = InitOptionButton(Window, 'Toggle_AI')
		AIPress(0) #this initialises the state and tooltip

		# Message popup FIXME disable on non game screen...
		Button = InitOptionButton(Window, 'Expand', True, False)# or 41661 Close ...

	else: ## pst lacks this control here. it is on the clock. iwd2 seems to skip it
		# Return to Game
		Button = InitOptionButton(Window,'Return_To_Game', True, False)
		Button.SetEvent(IE_GUI_BUTTON_ON_PRESS, CloseTopWindow)
		Button.MakeEscape()
		if bg1:
			# enabled BAM isn't present in .chu, defining it here
			Button.SetSprites ("GUILSOP", 0,16,17,28,16)
		if iwd1:
			# disabled/selected frame isn't present in .chu, defining it here
			Button.SetSprites ("GUILSOP", 0,16,17,16,16)

	# Party managment / character arbitration. Distinct form reform party window.
	if not pst:
		Button = Window.GetControl (OptionControl['Party'])
		Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, None) #TODO: OpenPartyWindow
		if bg1 or bg2:
			Button.SetState (IE_GUI_BUTTON_DISABLED)
			Button.SetFlags (IE_GUI_BUTTON_NO_IMAGE, OP_OR)
		else:
			Button.SetTooltip (OptionTip['Party'])

	# Map
	Button = InitOptionButton(Window, 'Map', True)
	if bg1:
		Button.SetSprites ("GUILSOP", 0,0,1,20,0)
	if iwd1:
		Button.SetSprites ("GUILSOP", 0,0,1,20,20)

	# Journal
	Button = InitOptionButton(Window, 'Journal', True)
	if bg1:
		Button.SetSprites ("GUILSOP", 0,4,5,22,4)
	if iwd1:
		Button.SetSprites ("GUILSOP", 0,4,5,22,22)

	# Inventory
	Button = InitOptionButton(Window, 'Inventory', True)
	if bg1:
		Button.SetSprites ("GUILSOP", 0,2,3,21,2)
	if iwd1:
		Button.SetSprites ("GUILSOP", 0,2,3,21,21)

	# Records
	Button = InitOptionButton(Window, CharacterStatsKey, True)
	if bg1:
		Button.SetSprites ("GUILSOP", 0,6,7,23,6)
	if iwd1:
		Button.SetSprites ("GUILSOP", 0,6,7,23,23)

	if not iwd2: # All Other Games Have Fancy Distinct Spell Pages
		# Mage
		Button = InitOptionButton(Window, MageSpellsKey, True)
		if bg1:
			Button.SetSprites ("GUILSOP", 0,8,9,24,8)
		if iwd1:
			Button.SetSprites ("GUILSOP", 0,8,9,24,24)

		# Priest
		Button = InitOptionButton(Window, 'Priest_Spells', True)
		if bg1:
			Button.SetSprites ("GUILSOP", 0,10,11,25,10)
		if iwd1:
			Button.SetSprites ("GUILSOP", 0,10,11,25,25)

	# Options
	Button = InitOptionButton(Window, 'Options', True)
	if bg1:
		Button.SetSprites ("GUILSOP", 0,12,13,26,12)
	if iwd1:
		Button.SetSprites ("GUILSOP", 0,12,13,26,26)


	# pause button
	if Gears:
		# Pendulum, gears, sun/moon dial (time)
		# FIXME: display all animations: CPEN, CGEAR, CDIAL
		if how: # how doesn't have this in the right place
			pos = ScreenHeight - 71
			Window.CreateButton (OptionControl['Time'], 6, pos, 64, 71)

		Button = Window.GetControl (OptionControl['Time'])
		if bg2:
			Label = Button.CreateLabel (0x10000009, "NORMAL", "", IE_FONT_SINGLE_LINE)
			Label.SetAnimation ("CPEN")

		Button.SetAnimation ("CGEAR")
		Button.SetState (IE_GUI_BUTTON_ENABLED)
		Button.SetFlags (IE_GUI_BUTTON_PICTURE|IE_GUI_BUTTON_ANIMATED|IE_GUI_BUTTON_NORMAL, OP_SET)
		Button.SetEvent(IE_GUI_BUTTON_ON_PRESS, GUICommon.GearsClicked)
		if iwd2:
			Button.SetState (IE_GUI_BUTTON_LOCKED) #no button depression, timer is an inset stone planet
			rb = OptionControl['Rest']
		else:
			rb = 11
		UpdateClock ()
	else:
		rb = OptionControl['Rest']
		if iwd2:
			UpdateClock ()

	# Rest
	Button = Window.GetControl (rb)
	if Button:
		Button.SetTooltip (OptionTip['Rest'])
		Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, RestPress)

	return

def OnLockViewPress ():
	Button = OptionsWindow.GetControl (0)
	GemRB.GameControlSetScreenFlags (SF_CENTERONACTOR | SF_ALWAYSCENTER, OP_XOR)

	# no way to get the screen flags
	if OnLockViewPress.counter % 2:
		# unlock
		Button.SetTooltip (41648)
		Button.SetState(IE_GUI_BUTTON_SELECTED)#dont ask
	else:
		# lock
		Button.SetTooltip (41647)
		Button.SetState(IE_GUI_BUTTON_NORMAL)
	OnLockViewPress.counter += 1

	return

OnLockViewPress.counter = 1

def PortraitPress ():
	"""Toggles the portraits pane """
	PP = GemRB.GetGUIFlags () & GS_PORTRAITPANE
	if PP:
		GemRB.GameSetScreenFlags (GS_PORTRAITPANE, OP_NAND)
	else:
		GemRB.GameSetScreenFlags (GS_PORTRAITPANE, OP_OR)
	return

def AIPress (toggle=1):
	"""Toggles the party AI or refreshes the button state if toggle = 0"""

	if GameCheck.IsPST() or GameCheck.IsIWD2():
		OptionsWindow = GemRB.GetView("OPTWIN")
		Button = OptionsWindow.GetControl (OptionControl['Toggle_AI'])
	else:
		PortraitWindow = GemRB.GetView("PORTWIN")
		Button = PortraitWindow.GetControl (OptionControl['Toggle_AI'])

	#print "AIPress: GS_PARTYAI was:", GemRB.GetGUIFlags () & GS_PARTYAI, "at toggle:", toggle
	if toggle:
		GemRB.GameSetScreenFlags (GS_PARTYAI, OP_XOR)

	AI = GemRB.GetGUIFlags () & GS_PARTYAI
	if AI:
		GemRB.SetVar ("AI", 0)
		Button.SetTooltip (AITip['Deactivate'])
		Button.SetState(IE_GUI_BUTTON_SELECTED)
	else:
		GemRB.SetVar ("AI", GS_PARTYAI)
		Button.SetTooltip (AITip['Enable'])
		Button.SetState(IE_GUI_BUTTON_NORMAL)

	#force redrawing, in case a hotkey triggered this function
	Button.SetVarAssoc ("AI", GS_PARTYAI)
	return

## The following four functions are for the action bar
## they are currently unused in pst
def EmptyControls ():
	if GameCheck.IsPST():
		return
	Selected = GemRB.GetSelectedSize()
	if Selected==1:
		pc = GemRB.GameGetFirstSelectedActor ()
		#init spell list
		GemRB.SpellCast (pc, -1, 0)

	GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	for i in range (12):
		Button = CurrentWindow.GetControl (i+ActionBarControlOffset)
		Button.SetVisible (False)
	return

def SelectFormationPreset ():
	"""Choose the default formation."""
	GemRB.GameSetFormation (GemRB.GetVar ("Value"), GemRB.GetVar ("Formation") )
	GroupControls ()
	return

def SetupFormation ():
	"""Opens the formation selection section."""
	for i in range (12):
		Button = CurrentWindow.GetControl (i+ActionBarControlOffset)
		Button.SetFlags (IE_GUI_BUTTON_NORMAL, OP_SET)
		Button.SetSprites ("GUIBTBUT",0,0,1,2,3)
		Button.SetBAM ("FORM%x"%i,0,0,-1)
		Button.SetVarAssoc ("Value", i)
		Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, SelectFormationPreset)
		Button.SetState (IE_GUI_BUTTON_UNPRESSED)
	return

def GroupControls ():
	"""Sections that control group actions."""
	GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	Button = CurrentWindow.GetControl (ActionBarControlOffset)
	if GameCheck.IsBG2():
		Button.SetActionIcon (globals(), 7, 1) #talk icon
	else:
		Button.SetActionIcon (globals(), 14, 1)#guard icon
	Button = CurrentWindow.GetControl (1+ActionBarControlOffset)
	Button.SetActionIcon (globals(), 15, 2)
	Button = CurrentWindow.GetControl (2+ActionBarControlOffset)
	Button.SetActionIcon (globals(), 21, 3)
	Button = CurrentWindow.GetControl (3+ActionBarControlOffset)
	Button.SetActionIcon (globals(), -1, 4)
	Button = CurrentWindow.GetControl (4+ActionBarControlOffset)
	Button.SetActionIcon (globals(), -1, 5)
	Button = CurrentWindow.GetControl (5+ActionBarControlOffset)
	Button.SetActionIcon (globals(), -1, 6)
	Button = CurrentWindow.GetControl (6+ActionBarControlOffset)
	Button.SetActionIcon (globals(), -1, 7)
	GemRB.SetVar ("Formation", GemRB.GameGetFormation ())
	for i in range (5):
		Button = CurrentWindow.GetControl (7+ActionBarControlOffset+i)
		Button.SetState (IE_GUI_BUTTON_ENABLED)
		idx = GemRB.GameGetFormation (i)
		Button.SetFlags (IE_GUI_BUTTON_RADIOBUTTON|IE_GUI_BUTTON_NORMAL, OP_SET)
		# kill the previous sprites or they show through
		Button.SetSprites ("GUIBTBUT",0,0,1,2,3)
		Button.SetBAM ("FORM%x"%idx,0,0,-1)
		Button.SetVarAssoc ("Formation", i)
		Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, GUICommon.SelectFormation)
		Button.SetEvent (IE_GUI_BUTTON_ON_RIGHT_PRESS, SetupFormation)
		Button.SetTooltip (4935)
		# 0x90 = F1 key
		Button.SetHotKey (chr(7+i+0x90), 0, True)
	return

def OpenActionsWindowControls (Window):
	global ActionsWindow

	ActionsWindow = Window
	# 1280 and higher don't have this control
	if not Window.GetControl (62):
		UpdateActionsWindow ()
		return
	# Gears (time) when options pane is down
	Button = Window.GetControl (62)
	Label = Button.CreateLabel (0x1000003e, "NORMAL", "", IE_FONT_SINGLE_LINE)

	# FIXME: display all animations
	Label.SetAnimation ("CPEN")
	Button.SetAnimation ("CGEAR")
	Button.SetState (IE_GUI_BUTTON_ENABLED)
	Button.SetFlags (IE_GUI_BUTTON_PICTURE|IE_GUI_BUTTON_ANIMATED|IE_GUI_BUTTON_NORMAL, OP_SET)
	Button.SetEvent(IE_GUI_BUTTON_ON_PRESS, GUICommon.GearsClicked)
	UpdateActionsWindow ()
	return

def SetupClockWindowControls (Window):
	global ActionsWindow

	ActionsWindow = Window
	# time button
	Button = Window.GetControl (0)
	Button.SetAnimation ("WMTIME")
	Button.SetState (IE_GUI_BUTTON_LOCKED)
	Button.SetFlags (IE_GUI_BUTTON_PICTURE | IE_GUI_BUTTON_ANIMATED, OP_SET)
	Button.SetEvent(IE_GUI_BUTTON_ON_PRESS, GUICommon.GearsClicked)
	Button.SetEvent(IE_GUI_MOUSE_ENTER_BUTTON, UpdateClock)
	SetPSTGamedaysAndHourToken ()
	Button.SetTooltip (GemRB.GetString(65027))

	# 41627 - Return to the Game World
	Button = Window.GetControl (2)
	Button.SetEvent(IE_GUI_BUTTON_ON_PRESS, CloseTopWindow)
	Button.SetTooltip (41627)

	# Select all characters
	Button = Window.GetControl (1)
	Button.SetTooltip (41659)

	# Abort current action
	Button = Window.GetControl (3)
	Button.SetTooltip (41655)

	# Formations
	Button = Window.GetControl (4)
	Button.SetTooltip (44945)

	return

##not used in pst - not sure any items have abilities, but it is worth making a note to find out
def SelectItemAbility():
	pc = GemRB.GameGetFirstSelectedActor ()
	slot = GemRB.GetVar ("Slot")
	ability = GemRB.GetVar ("Ability")
	GemRB.SetupQuickSlot (pc, 0, slot, ability)
	GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	return

#in pst only nordom has bolts and they show on the same floatmenu as quickweapons, so needs work here
def SelectQuiverSlot():
	pc = GemRB.GameGetFirstSelectedActor ()
	slot = GemRB.GetVar ("Slot")
	slot_item = GemRB.GetSlotItem (pc, slot)
	# HACK: implement SetEquippedAmmunition instead?
	if not GemRB.IsDraggingItem ():
		item = GemRB.GetItem (slot_item["ItemResRef"])
		GemRB.DragItem (pc, slot, item["ItemIcon"]) #, 0, 0)
		GemRB.DropDraggedItem (pc, slot)
	GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	return

#this doubles up as an ammo selector (not yet used in pst)
def SetupItemAbilities(pc, slot, only):
	slot_item = GemRB.GetSlotItem(pc, slot)
	if not slot_item:
		# CHIV: Could configure empty quickslots from the game screen ala spells heres
		return

	item = GemRB.GetItem (slot_item["ItemResRef"])
	Tips = item["Tooltips"]
	Locations = item["Locations"]

	# clear buttons here
	EmptyControls()

	# check A: whether ranged weapon and B: whether to bother at all
	ammotype = 0
	if item["Type"] == CommonTables.ItemType.GetRowIndex ("BOW"):
		ammotype = CommonTables.ItemType.GetRowIndex ("ARROW")
	elif item["Type"] == CommonTables.ItemType.GetRowIndex ("XBOW"):
		ammotype = CommonTables.ItemType.GetRowIndex ("BOLT")
	elif item["Type"] == CommonTables.ItemType.GetRowIndex ("SLING"):
		ammotype = CommonTables.ItemType.GetRowIndex ("BULLET")

	ammoSlotCount = 0
	if ammotype:
		ammoslots = GemRB.GetSlots(pc, SLOT_QUIVER, 1)
		currentammo = GemRB.GetEquippedAmmunition (pc)
		currentbutton = None
		for i in range (12):
			Button = CurrentWindow.GetControl (i+ActionBarControlOffset)
			if i < len(ammoslots):
				ammoslot = GemRB.GetSlotItem (pc, ammoslots[i])
				st = GemRB.GetSlotType (ammoslots[i])
				ammoitem = GemRB.GetItem (ammoslot['ItemResRef']) # needed to show the ammo count
				Tips = ammoitem["Tooltips"]
				# if this item is valid ammo and was really found in a quiver slot
				if ammoitem['Type'] == ammotype and st["Type"] == SLOT_QUIVER:
					ammoSlotCount += 1
					Button.SetFlags (IE_GUI_BUTTON_RADIOBUTTON|IE_GUI_BUTTON_ALIGN_BOTTOM|IE_GUI_BUTTON_ALIGN_RIGHT, OP_SET)
					Button.SetSprites ("GUIBTBUT", 0, 0,1,3,5)
					Button.SetItemIcon (ammoslot['ItemResRef'])
					Button.SetText (str(ammoslot["Usages0"]))
					Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, SelectQuiverSlot)
					Button.SetEvent (IE_GUI_BUTTON_ON_RIGHT_PRESS, SelectQuiverSlot)
					Button.SetVarAssoc ("Slot", ammoslots[i])
					if Tips[0] != -1:
						Button.SetTooltip (Tips[0])
					if currentammo == ammoslots[i]:
						currentbutton = Button

		if currentbutton:
			currentbutton.SetState (IE_GUI_BUTTON_SELECTED)

	# skip when there is only one choice
	if ammoSlotCount == 1:
		ammoSlotCount = 0

	# reset back to the main action bar if there are no extra headers or quivers
	reset = not ammoSlotCount

	# check for item abilities and skip irrelevant headers
	# for quick weapons only show weapon headers
	# for quick items only show the opposite
	# for scrolls only show the first (second header is for learning)
	# So depending on the context Staff of Magi will have none or 2
	if item["Type"] == CommonTables.ItemType.GetRowIndex ("SCROLL"):
		Tips = ()

	# skip launchers - handled above
	# gesen bow (bg2:bow19) has just a projectile header (not bow) for its special attack
	# TODO: we should append it to the list of ammo as a usability improvement
	if only == UAW_QWEAPONS and ammoSlotCount > 1:
		Tips = ()

	if len(Tips) > 0:
		reset = False
		rmax = min(len(Tips), 12-ammoSlotCount)

		# for mixed items, only show headers if there is more than one appropriate one
		weaps = sum(map(lambda i: i==ITEM_LOC_WEAPON, Locations))
		if only == UAW_QWEAPONS and weaps == 1 and ammoSlotCount <= 1:
			rmax = 0
			reset = True
		abils = sum(map(lambda i: i==ITEM_LOC_EQUIPMENT, Locations))
		if only == UAW_QITEMS and abils == 1:
			rmax = 0
			reset = True

		for i in range (rmax):
			if only == UAW_QITEMS:
				if Locations[i] != ITEM_LOC_EQUIPMENT:
					continue
			elif only == UAW_QWEAPONS:
				if Locations[i] != ITEM_LOC_WEAPON:
					continue
			Button = CurrentWindow.GetControl (i+ActionBarControlOffset+ammoSlotCount)
			Button.SetFlags (IE_GUI_BUTTON_RADIOBUTTON|IE_GUI_BUTTON_NORMAL, OP_SET)
			Button.SetSprites ("GUIBTBUT", 0, 0,1,2,5)
			Button.SetItemIcon (slot_item['ItemResRef'], i+6)
			Button.SetText ("")
			Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, SelectItemAbility)
			Button.SetEvent (IE_GUI_BUTTON_ON_RIGHT_PRESS, SelectItemAbility)
			Button.SetVarAssoc ("Ability", i)
			Button.SetState (IE_GUI_BUTTON_ENABLED)
			if Tips[i] != -1:
				Button.SetTooltip ("F%d - %s" %(i, GemRB.GetString (Tips[i])))

	if reset:
		GemRB.SetVar ("ActionLevel", UAW_STANDARD)
		UpdateActionsWindow ()
	return

# iwd2 spell book/class selection
def SetupBookSelection ():
	pc = GemRB.GameGetFirstSelectedActor ()

	# get all the books that still have non-depleted memorisations
	# we need this list, so we can avoid holes in the action bar
	usableBooks = []
	for i in range (IE_IWD2_SPELL_SONG):
		bookClass = i
		if i == IE_IWD2_SPELL_INNATE: # shape stat comes later (8 vs 10)
			bookClass = IE_IWD2_SPELL_SHAPE

		enabled = False
		if i <= (IE_IWD2_SPELL_DOMAIN + 1): # booktypes up to and including domain + shapes
			spellCount = len(Spellbook.GetUsableMemorizedSpells (pc, bookClass))
			enabled = spellCount > 0
		if enabled:
			usableBooks.append(i)

	# if we have only one or only cleric+domain, skip to the spells
	bookCount = len(usableBooks)
	if bookCount == 1 or (bookCount == 2 and IE_IWD2_SPELL_CLERIC in usableBooks):
		GemRB.SetVar ("ActionLevel", UAW_SPELLS_DIRECT)
		UpdateActionsWindow ()
		return

	for i in range (12):
		Button = CurrentWindow.GetControl (i+ActionBarControlOffset)
		if i >= len(usableBooks):
			Button.SetActionIcon (globals(), -1)
			continue
		Button.SetActionIcon (globals(), 40+usableBooks[i])
	return

#you can change this for custom skills, this is the original engine
skillbar=(ACT_STEALTH, ACT_SEARCH, ACT_THIEVING, ACT_WILDERNESS, ACT_TAMING, 100, 100, 100, 100, 100, 100, 100)
def SetupSkillSelection ():
	pc = GemRB.GameGetFirstSelectedActor ()
	CurrentWindow.SetupControls( globals(), pc, ActionBarControlOffset, skillbar)
	return

def UpdateActionsWindow ():
	"""Redraws the actions section of the window."""
	global CurrentWindow, OptionsWindow, PortraitWindow
	global level, TopIndex

	if GameCheck.IsPST():
		return
	if GameCheck.IsIWD2():
		CurrentWindow = PortraitWindow
		ActionBarControlOffset = 6 # set it here too, since we get called before menu setup
	else:
		CurrentWindow = ActionsWindow
		ActionBarControlOffset = 0

	if CurrentWindow == None:
		return

	if not GameCheck.IsIWD2():
		UpdateClock ()

	Selected = GemRB.GetSelectedSize()

	#setting up the disabled button overlay (using the second border slot)
	for i in range (12):
		Button = CurrentWindow.GetControl (i+ActionBarControlOffset)
		if GameCheck.IsBG1():
			color = {'r' : 0, 'g' : 254, 'b' :0, 'a' : 255}
			Button.SetBorder (0, color, 0, 0, Button.GetInsetFrame(6,6,4,4))

		color = {'r' : 50, 'g' : 30, 'b' :10, 'a' : 120}
		Button.SetBorder (1, color, 0, 1)
		Button.SetFont ("NUMBER")
		Button.SetText ("")
		Button.SetTooltip("")

	if Selected == 0:
		EmptyControls ()
		return
	if Selected > 1:
		GroupControls ()
		return

	#we are sure there is only one actor selected
	pc = GemRB.GameGetFirstSelectedActor ()

	level = GemRB.GetVar ("ActionLevel")
	TopIndex = GemRB.GetVar ("TopIndex")
	if level == UAW_STANDARD:
		#this is based on class
		CurrentWindow.SetupControls (globals(), pc, ActionBarControlOffset)
	elif level == UAW_EQUIPMENT:
		CurrentWindow.SetupEquipmentIcons(globals(), pc, TopIndex, ActionBarControlOffset)
	elif level == UAW_SPELLS or level == UAW_SPELLS_DIRECT: #spells

		if GameCheck.IsIWD2():
			if level == UAW_SPELLS:
				# set up book selection if appropriate
				SetupBookSelection ()
				return
			# otherwise just throw everything in a single list
			# everything but innates, songs and shapes
			type = (1<<IE_IWD2_SPELL_INNATE) - 1
		else:
			type = (1<<IE_SPELL_TYPE_PRIEST) + (1<<IE_SPELL_TYPE_WIZARD)
		GemRB.SetVar ("Type", type)
		Spellbook.SetupSpellIcons(CurrentWindow, type, TopIndex, ActionBarControlOffset)
	elif level == UAW_INNATES: #innates
		if GameCheck.IsIWD2():
			type = (1<<IE_IWD2_SPELL_INNATE) + (1<<IE_IWD2_SPELL_SHAPE)
		else:
			type = 1<<IE_SPELL_TYPE_INNATE
		GemRB.SetVar ("Type", type)
		Spellbook.SetupSpellIcons(CurrentWindow, type, TopIndex, ActionBarControlOffset)
	elif level == UAW_QWEAPONS or level == UAW_QITEMS: #quick weapon or quick item ability selection
		SetupItemAbilities(pc, GemRB.GetVar("Slot"), level)
	elif level == UAW_ALLMAGE: #all known mage spells
		GemRB.SetVar ("Type", -1)
		Spellbook.SetupSpellIcons(CurrentWindow, -1, TopIndex, ActionBarControlOffset)
	elif level == UAW_SKILLS: # iwd2 skills
		SetupSkillSelection()
	elif level == UAW_QSPELLS: # quickspells, but with innates too
		if GameCheck.IsIWD2():
			type = (1<<IE_IWD2_SPELL_INNATE) - 1
			type += (1<<IE_IWD2_SPELL_INNATE) + (1<<IE_IWD2_SPELL_SHAPE)
		else:
			type = (1<<IE_SPELL_TYPE_PRIEST) + (1<<IE_SPELL_TYPE_WIZARD) + (1<<IE_SPELL_TYPE_INNATE)
		GemRB.SetVar ("Type", type)
		Spellbook.SetupSpellIcons(CurrentWindow, type, TopIndex, ActionBarControlOffset)
	elif level == UAW_QSHAPES: # shapes selection
		type = 1<<IE_IWD2_SPELL_SHAPE
		GemRB.SetVar ("Type", type)
		Spellbook.SetupSpellIcons(CurrentWindow, type, TopIndex, ActionBarControlOffset)
	elif level == UAW_QSONGS: # songs selection
		type = 1<<IE_IWD2_SPELL_SONG
		GemRB.SetVar ("Type", type)
		Spellbook.SetupSpellIcons(CurrentWindow, type, TopIndex, ActionBarControlOffset)
	elif level == UAW_BOOK: # spellbook selection
		type = GemRB.GetVar ("Type")
		Spellbook.SetupSpellIcons(CurrentWindow, type, TopIndex, ActionBarControlOffset)
	elif level == UAW_2DASPELLS: # spells from a 2da (fx_select_spell)
		if GameCheck.IsIWD2():
			# everything but innates, songs and shapes
			type = (1<<IE_IWD2_SPELL_INNATE) - 1
		else:
			type = (1<<IE_SPELL_TYPE_PRIEST) + (1<<IE_SPELL_TYPE_WIZARD)
		GemRB.SetVar ("Type", type)
		Spellbook.SetupSpellIcons (CurrentWindow, type, TopIndex, ActionBarControlOffset)
	else:
		print "Invalid action level:", level
		GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	return

def ActionQWeaponPressed (which):
	"""Selects the given quickslot weapon if possible."""

	pc = GemRB.GameGetFirstSelectedActor ()
	qs = GemRB.GetEquippedQuickSlot (pc, 1)

	#38 is the magic slot
	if ((qs==which) or (qs==38)) and GemRB.GameControlGetTargetMode() != TARGET_MODE_ATTACK:
		GemRB.GameControlSetTargetMode (TARGET_MODE_ATTACK, GA_NO_DEAD|GA_NO_SELF|GA_NO_HIDDEN)
	else:
		GemRB.GameControlSetTargetMode (TARGET_MODE_NONE)
		GemRB.SetEquippedQuickSlot (pc, which, -1)

	CurrentWindow.SetupControls (globals(), pc, ActionBarControlOffset)
	UpdateActionsWindow ()
	return

# TODO: implement full weapon set switching instead
def ActionQWeaponRightPressed (action):
	"""Selects the used ability of the quick weapon."""
	pc = GemRB.GameGetFirstSelectedActor ()
	GemRB.SetVar ("Slot", action)
	GemRB.SetVar ("ActionLevel", UAW_QWEAPONS)
	UpdateActionsWindow ()
	return

###############################################
# quick icons for spells, innates, songs, shapes

def ActionQSpellPressed (which):
	pc = GemRB.GameGetFirstSelectedActor ()

	GemRB.SpellCast (pc, -2, which)
	UpdateActionsWindow ()
	return

def ActionQSpellRightPressed (which):
	GemRB.SetVar ("QSpell", which)
	GemRB.SetVar ("TopIndex", 0)
	GemRB.SetVar ("ActionLevel", UAW_QSPELLS)
	UpdateActionsWindow ()
	return

suf = ["", "Right"]
# this function is used to generate various action bar actions
# that should be available in 9 slots (iwd2)
def GenerateButtonActions(num, name, g, right=0, offset=0):
	dec = "def Action" + name + str(num+1) + suf[right] + "Pressed():\n"
	dec += "\tAction" + name + suf[right] + "Pressed(" + str(num+offset) + ")"
	exec(dec, g) # pass on the same global dict, so we remain in the top scope

for i in range(9):
	GenerateButtonActions(i, "QSpec", globals(), 0)
	GenerateButtonActions(i, "QSpec", globals(), 1)
	GenerateButtonActions(i, "QSpell", globals(), 0)
	GenerateButtonActions(i, "QSpell", globals(), 1)
	GenerateButtonActions(i, "QShape", globals(), 0)
	GenerateButtonActions(i, "QShape", globals(), 1)
	GenerateButtonActions(i, "QSong", globals(), 0)
	GenerateButtonActions(i, "QSong", globals(), 1)
for i in range(4):
	GenerateButtonActions(i, "QWeapon", globals(), 0)
	GenerateButtonActions(i, "QWeapon", globals(), 1, 10)

def ActionQSpecPressed (which):
	ActionQSpellPressed (which)

def ActionQSpecRightPressed (which):
	GemRB.SetVar ("QSpell", which)
	GemRB.SetVar ("TopIndex", 0)
	GemRB.SetVar ("ActionLevel", UAW_INNATES)
	UpdateActionsWindow ()

def ActionQShapePressed (which):
	ActionQSpellPressed (which)

def ActionQShapeRightPressed (which):
	GemRB.SetVar ("QSpell", which)
	GemRB.SetVar ("TopIndex", 0)
	GemRB.SetVar ("ActionLevel", UAW_QSHAPES)
	UpdateActionsWindow ()

def ActionQSongPressed (which):
	SelectBardSong (which) # TODO: verify parameter once we have actionbar customisation
	ActionBardSongPressed ()

def ActionQSongRightPressed (which):
	GemRB.SetVar ("QSpell", which)
	GemRB.SetVar ("TopIndex", 0)
	GemRB.SetVar ("ActionLevel", UAW_QSONGS)
	UpdateActionsWindow ()

# can't pass the globals dictionary from another module
def SetActionIconWorkaround(Button, action, function):
	Button.SetActionIcon (globals(), action, function)

#no check needed because the button wouldn't be drawn if illegal
def ActionLeftPressed ():
	"""Scrolls the actions window left.

	Used primarily for spell selection."""

	TopIndex = GemRB.GetVar ("TopIndex")
	if TopIndex>10:
		TopIndex -= 10
	else:
		TopIndex = 0
	GemRB.SetVar ("TopIndex", TopIndex)
	UpdateActionsWindow ()
	return

#no check needed because the button wouldn't be drawn if illegal
def ActionRightPressed ():
	"""Scrolls the action window right.

	Used primarily for spell selection."""

	pc = GemRB.GameGetFirstSelectedActor ()
	TopIndex = GemRB.GetVar ("TopIndex")
	Type = GemRB.GetVar ("Type")
	print "Type:", Type
	#Type is a bitfield if there is no level given
	#This is to make sure cleric/mages get all spells listed
	if GemRB.GetVar ("ActionLevel") == UAW_ALLMAGE:
		if Type == 3:
			Max = len(Spellbook.GetKnownSpells (pc, IE_SPELL_TYPE_PRIEST) + Spellbook.GetKnownSpells (pc, IE_SPELL_TYPE_WIZARD))
		else:
			Max = GemRB.GetKnownSpellsCount (pc, Type, -1) # this can handle only one type at a time
	else:
		Max = GemRB.GetMemorizedSpellsCount(pc, Type, -1, 1)
	print "Max:",Max
	TopIndex += 10
	if TopIndex > Max - 10:
		if Max>10:
			if TopIndex > Max:
				TopIndex = Max - 10
		else:
			TopIndex = 0
	GemRB.SetVar ("TopIndex", TopIndex)
	UpdateActionsWindow ()
	return

def ActionMeleePressed ():
	""" switches to the most damaging melee weapon"""
	#get the party Index
	pc = GemRB.GameGetFirstSelectedPC ()
	GemRB.ExecuteString("EquipMostDamagingMelee()", pc)
	return

def ActionRangePressed ():
	""" switches to the most damaging ranged weapon"""
	#get the party Index
	pc = GemRB.GameGetFirstSelectedPC ()
	GemRB.ExecuteString("EquipRanged()", pc)
	return

def ActionShapeChangePressed ():
	GemRB.SetVar ("ActionLevel", UAW_QSHAPES)
	GemRB.SetVar ("TopIndex", 0)
	UpdateActionsWindow ()
	return

def SelectBardSong (which):
	pc = GemRB.GameGetFirstSelectedActor ()
	songs = Spellbook.GetKnownSpells (pc, IE_IWD2_SPELL_SONG)
	# "which" is a mashup of the spell index with it's type
	idx = which % ((1<<IE_IWD2_SPELL_SHAPE)*100)
	qsong = songs[idx]['SpellResRef']
	# the effect needs to be set each tick, so we use FX_DURATION_INSTANT_PERMANENT==1 timing mode
	# GemRB.SetModalState can also set the spell, but it wouldn't persist
	GemRB.ApplyEffect (pc, 'ChangeBardSong', 0, idx, qsong, "", "", "", 1)

def ActionBardSongRightPressed ():
	"""Selects a bardsong."""
	GemRB.SetVar ("ActionLevel", UAW_QSONGS)
	GemRB.SetVar ("TopIndex", 0)
	UpdateActionsWindow ()
	return

def ActionBardSongPressed ():
	"""Toggles the battle song."""

	#get the global ID
	pc = GemRB.GameGetFirstSelectedActor ()
	GemRB.SetModalState (pc, MS_BATTLESONG)
	GemRB.PlaySound ("act_01")
	GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	UpdateActionsWindow ()
	return

def ActionSearchPressed ():
	"""Toggles detect traps."""

	#get the global ID
	pc = GemRB.GameGetFirstSelectedActor ()
	GemRB.SetModalState (pc, MS_DETECTTRAPS)
	GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	UpdateActionsWindow ()
	return

def ActionStealthPressed ():
	"""Toggles stealth."""
	pc = GemRB.GameGetFirstSelectedActor ()
	GemRB.SetModalState (pc, MS_STEALTH)
	GemRB.PlaySound ("act_07")
	GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	UpdateActionsWindow ()
	return

def ActionTurnPressed ():
	"""Toggles turn undead."""
	pc = GemRB.GameGetFirstSelectedActor ()
	GemRB.SetModalState (pc, MS_TURNUNDEAD)
	GemRB.PlaySound ("act_06")
	GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	UpdateActionsWindow ()
	return

def ActionTamingPressed ():
	GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	UpdateActionsWindow ()
	return

def ActionWildernessPressed ():
	pc = GemRB.GameGetFirstSelectedActor ()
	GemRB.ApplyEffect (pc, "Reveal:Tracks", 0, 0)
	GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	UpdateActionsWindow ()
	return

def ActionUseItemPressed ():
	GemRB.SetVar ("TopIndex", 0)
	GemRB.SetVar ("ActionLevel", UAW_EQUIPMENT)
	UpdateActionsWindow ()
	return

def ActionCastPressed ():
	"""Opens the spell choice scrollbar."""
	GemRB.SetVar ("QSpell", -1)
	GemRB.SetVar ("TopIndex", 0)
	GemRB.SetVar ("ActionLevel", UAW_SPELLS)
	UpdateActionsWindow ()
	return

def ActionQItemPressed (action):
	"""Uses the given quick item."""
	pc = GemRB.GameGetFirstSelectedActor ()
	#quick slot
	GemRB.UseItem (pc, -2, action, -1)
	return

def ActionQItem1Pressed ():
	ActionQItemPressed (ACT_QSLOT1)
	return

def ActionQItem2Pressed ():
	ActionQItemPressed (ACT_QSLOT2)
	return

def ActionQItem3Pressed ():
	ActionQItemPressed (ACT_QSLOT3)
	return

def ActionQItem4Pressed ():
	ActionQItemPressed (ACT_QSLOT4)
	return

def ActionQItem5Pressed ():
	ActionQItemPressed (ACT_QSLOT5)
	return

def ActionQItemRightPressed (action):
	"""Selects the used ability of the quick item."""
	pc = GemRB.GameGetFirstSelectedActor ()
	GemRB.SetVar ("Slot", action)
	GemRB.SetVar ("ActionLevel", UAW_QITEMS)
	UpdateActionsWindow ()
	return

def ActionQItem1RightPressed ():
	ActionQItemRightPressed (19)

def ActionQItem2RightPressed ():
	ActionQItemRightPressed (20)

def ActionQItem3RightPressed ():
	ActionQItemRightPressed (21)

def ActionQItem4RightPressed ():
	ActionQItemRightPressed (22)

def ActionQItem5RightPressed ():
	ActionQItemRightPressed (23)

def ActionInnatePressed ():
	"""Opens the innate spell scrollbar."""
	GemRB.SetVar ("QSpell", -1)
	GemRB.SetVar ("TopIndex", 0)
	GemRB.SetVar ("ActionLevel", UAW_INNATES)
	UpdateActionsWindow ()
	return

def ActionSkillsPressed ():
	GemRB.SetVar ("TopIndex", 0)
	GemRB.SetVar ("ActionLevel", UAW_SKILLS)
	UpdateActionsWindow ()
	return

def TypeSpellPressed (type):
	GemRB.SetVar ("Type", 1<<type)
	GemRB.SetVar ("ActionLevel", UAW_BOOK)
	UpdateActionsWindow ()
	return

def ActionBardSpellPressed ():
	TypeSpellPressed(IE_IWD2_SPELL_BARD)
	return

def ActionClericSpellPressed ():
	TypeSpellPressed(IE_IWD2_SPELL_CLERIC)
	return

def ActionDruidSpellPressed ():
	TypeSpellPressed(IE_IWD2_SPELL_DRUID)
	return

def ActionPaladinSpellPressed ():
	TypeSpellPressed(IE_IWD2_SPELL_PALADIN)
	return

def ActionRangerSpellPressed ():
	TypeSpellPressed(IE_IWD2_SPELL_RANGER)
	return

def ActionSorcererSpellPressed ():
	TypeSpellPressed(IE_IWD2_SPELL_SORCERER)
	return

def ActionWizardSpellPressed ():
	TypeSpellPressed(IE_IWD2_SPELL_WIZARD)
	return

def ActionDomainSpellPressed ():
	TypeSpellPressed(IE_IWD2_SPELL_DOMAIN)
	return

def ActionWildShapesPressed ():
	TypeSpellPressed(IE_IWD2_SPELL_SHAPE)
	return

def SpellShiftPressed ():
	Spell = GemRB.GetVar ("Spell") # spellindex from spellbook jumbled with booktype
	Type =  Spell // 1000
	SpellIndex = Spell % 1000

	# try spontaneous casting
	pc = GemRB.GameGetFirstSelectedActor ()
	ClassRowName = GUICommon.GetClassRowName (pc)
	SponCastTableName = CommonTables.ClassSkills.GetValue (ClassRowName, "SPONCAST")
	if SponCastTableName != "*":
		SponCastTable = GemRB.LoadTable (SponCastTableName, 1)
		if not SponCastTable:
			print "SpellShiftPressed: skipping, non-existent spontaneous casting table used! ResRef:", SponCastTableName
			SpellPressed ()
			return

		# determine the column number (spell variety) depending on alignment
		CureOrHarm = GemRB.GetPlayerStat (pc, IE_ALIGNMENT)
		if CureOrHarm % 16 == 3: # evil
			CureOrHarm = 1
		else:
			CureOrHarm = 0

		# get the unshifted booktype
		BaseType = 0
		tmp = Type
		while tmp > 1:
			tmp = tmp>>1
			BaseType += 1

		# figure out the spell's details
		# TODO: find a simpler way
		Spell = None
		MemorisedSpells = Spellbook.GetSpellinfoSpells (pc, BaseType)
		for spell in MemorisedSpells:
			if spell['SpellIndex']%(255000) == SpellIndex: # 255 is the engine value of Type
				Spell = spell
				break

		# rownames==level; col1: good+neutral; col2: evil resref
		Level = Spell['SpellLevel']
		ReplacementSpell = SponCastTable.GetValue (Level-1, CureOrHarm).upper()
		if ReplacementSpell != Spell['SpellResRef'].upper():
			SpellIndex = GemRB.PrepareSpontaneousCast (pc, Spell['SpellResRef'], Spell['BookType'], Level, ReplacementSpell)
			GemRB.SetVar ("Spell", SpellIndex+1000*Type)
			if GameCheck.IsIWD2():
				GemRB.DisplayString (39742, 0xffffff, pc) # Spontaneous Casting

	# proceed as if nothing happened
	SpellPressed ()

# This is the endpoint for spellcasting, finally calling SpellCast. This always happens at least
# twice though, the second time to reset the action bar (more if wild magic or subspell selection is involved).
# Spell and Type (spellbook type) are set during the spell bar construction/use, which is in turn
# affected by ActionLevel (see UpdateActionsWindow of this module).
# Keep in mind, that the core resets Type and/or ActionLevel in the case of subspells (fx_select_spell).
def SpellPressed ():
	"""Prepares a spell to be cast."""

	pc = GemRB.GameGetFirstSelectedActor ()

	Spell = GemRB.GetVar ("Spell")
	Type = GemRB.GetVar ("Type")

	if Type == 1<<IE_IWD2_SPELL_SONG:
		SelectBardSong (Spell)
		ActionBardSongPressed()
		return

	GemRB.GameControlSetTargetMode (TARGET_MODE_CAST)
	if Type != -1:
		Type = Spell // 1000
	Spell = Spell % 1000
	slot = GemRB.GetVar ("QSpell")
	if slot>=0:
		#setup quickspell slot
		#if spell has no target, return
		#otherwise continue with casting
		Target = GemRB.SetupQuickSpell (pc, slot, Spell, Type)
		#sabotage the immediate casting of self targeting spells
		if Target == 5 or Target == 7:
			Type = -1
			GemRB.GameControlSetTargetMode (TARGET_MODE_NONE)

	if Type==-1:
		GemRB.SetVar ("ActionLevel", UAW_STANDARD)
		GemRB.SetVar("Type", 0)
	GemRB.SpellCast (pc, Type, Spell)
	if GemRB.GetVar ("Type")!=-1:
		GemRB.SetVar ("ActionLevel", UAW_STANDARD)
		#init spell list
		GemRB.SpellCast (pc, -1, 0)
	GemRB.SetVar ("TopIndex", 0)
	UpdateActionsWindow ()
	return

def EquipmentPressed ():
	pc = GemRB.GameGetFirstSelectedActor ()

	GemRB.GameControlSetTargetMode (TARGET_MODE_CAST)
	Item = GemRB.GetVar ("Equipment")
	#equipment index
	GemRB.UseItem (pc, -1, Item, -1)
	GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	UpdateActionsWindow ()
	return

######################

#End of features that need adding to pst

######################

# NOTE: the following two features are only used in pst
# which=INVENTORY|STATS|FMENU
def GetActorPortrait (actor, which):
	#return GemRB.GetPlayerPortrait( actor, which)

	# only the lowest byte is meaningful here (OneByteAnimID)
	anim_id = GemRB.GetPlayerStat (actor, IE_ANIMATION_ID) & 255
	row = "0x%02X" %anim_id

	return CommonTables.Pdolls.GetValue (row, which)


def UpdateAnimation ():
	if not GemRB.HasResource ("ANIMS", RES_2DA, 1):
		# FIXME: make a simpler version for non-pst too
		# this is a callback from the core on EF_UPDATEANIM!
		return

	pc = GemRB.GameGetSelectedPCSingle ()

	disguise = GemRB.GetGameVar ("APPEARANCE")
	if disguise == 2: #dustman
		animid = "DR"
	elif disguise == 1: #zombie
		animid = "ZO"
	else:
		slot = GemRB.GetEquippedQuickSlot (pc)
		item = GemRB.GetSlotItem (pc, slot )
		animid = ""
		if item:
			item = GemRB.GetItem(item["ItemResRef"])
			if item:
				animid = item["AnimationType"]

	BioTable = GemRB.LoadTable ("BIOS")
	Specific = GemRB.GetPlayerStat (pc, IE_SPECIFIC)
	AvatarName = BioTable.GetRowName (Specific+1)
	AnimTable = GemRB.LoadTable ("ANIMS")
	if animid=="":
		animid="*"
	value = AnimTable.GetValue (animid, AvatarName)
	if value<0:
		return
	GemRB.SetPlayerStat (pc, IE_ANIMATION_ID, value)
	return

# NOTE: the following 4 functions are only used in iwd2
def GetActorRaceTitle (actor):
	import IDLUCommon
	RaceIndex = IDLUCommon.GetRace (actor)
	RaceTitle = CommonTables.Races.GetValue (RaceIndex, 2)
	return RaceTitle

# NOTE: this function is called with the primary classes
def GetKitIndex (actor, ClassIndex):
	Kit = GemRB.GetPlayerStat (actor, IE_KIT)

	KitIndex = -1
	ClassName = CommonTables.Classes.GetRowName (ClassIndex)
	ClassID = CommonTables.Classes.GetValue (ClassName, "ID")
	# skip the primary classes
	# start at the first original kit - in iwd2 both classes and kits are in the same table
	KitOffset = CommonTables.Classes.FindValue ("CLASS", 7)
	for ci in range (KitOffset, CommonTables.Classes.GetRowCount ()):
		RowName = CommonTables.Classes.GetRowName (ci)
		BaseClass = CommonTables.Classes.GetValue (RowName, "CLASS")
		if BaseClass == ClassID and Kit & CommonTables.Classes.GetValue (RowName, "ID"):
			#FIXME: this will return the last kit only, check if proper multikit return values are needed
			KitIndex = ci

	if KitIndex == -1:
		return 0

	return KitIndex

def GetActorClassTitle (actor, ClassIndex):
	ClassTitle = GemRB.GetPlayerStat (actor, IE_TITLE1)
	if ClassTitle:
		return ClassTitle

	KitIndex = GetKitIndex (actor, ClassIndex)
	if KitIndex == 0:
		ClassName = CommonTables.Classes.GetRowName (ClassIndex)
	else:
		ClassName = CommonTables.Classes.GetRowName (KitIndex)
	ClassTitle = CommonTables.Classes.GetValue (ClassName, "NAME_REF")

	if ClassTitle == "*":
		return 0
	return ClassTitle

# overriding the one in GUICommon, since we use a different table and animations
def GetActorPaperDoll (actor):
	level = GemRB.GetPlayerStat (actor, IE_ARMOR_TYPE)
	return GemRB.GetAvatarsValue (actor, level)


SelectionChangeHandler = None
SelectionChangeMultiHandler = None ##relates to floatmenu

def SetSelectionChangeHandler (handler):
	"""Updates the selection handler."""

	global SelectionChangeHandler

	# Switching from walking to non-walking environment:
	# set the first selected PC in walking env as a selected
	# in nonwalking env
	#if (not SelectionChangeHandler) and handler:
	if (not SelectionChangeHandler) and handler:
		sel = GemRB.GameGetFirstSelectedPC ()
		if sel:
			GemRB.GameSelectPCSingle (sel)

	SelectionChangeHandler = handler

	# redraw selection on change main selection | single selection
	# SelectionChanged ()
	return

def SetSelectionChangeMultiHandler (handler):
	global SelectionChangeMultiHandler
	SelectionChangeMultiHandler = handler
	#SelectionChanged ()

def CloseTopWindow ():
	window = GemRB.GetView("WIN_TOP")
	if window:
		window.Close()

# TODO: this really looks like (most of?) it only applies to the inventory window
# it woudl be better to move that to GUIINV
def TopWindowClosed(window):
	GameWin = GemRB.GetView("GAMEWIN")
	GameWin.SetDisabled(False)

	GemRB.LeaveContainer()
	if GemRB.IsDraggingItem () == 1:
		pc = GemRB.GameGetSelectedPCSingle ()
		#store the item in the inventory before window is closed
		GemRB.DropDraggedItem (pc, -3)
		#dropping on ground if cannot store in inventory
		if GemRB.IsDraggingItem () == 1:
			GemRB.DropDraggedItem (pc, -2)
	
	#don't go back to multi selection mode when going to the store screen
	if not GemRB.GetVar ("Inventory"):
		SetSelectionChangeHandler (None)

	SelectionChanged()

if GameCheck.IsIWD2():
	DefaultWinPos = WINDOW_TOP|WINDOW_HCENTER
else:
	DefaultWinPos = WINDOW_CENTER

def CreateTopWinLoader(id, pack, loader, initer = None, selectionHandler = None, pos = DefaultWinPos):
	def ret ():
		window = loader(id, pack, pos)

		if window:
			if initer:
				initer(window)
	
			if selectionHandler:
				selectionHandler(window)

			window.SetFlags(WF_ALPHA_CHANNEL, OP_NAND)
			SetTopWindow (window, selectionHandler)
			window.SetOnClose(TopWindowClosed)
			GameWin = GemRB.GetView("GAMEWIN")
			GameWin.SetDisabled(True)

		return window
	
	return ret

def SetTopWindow (window, selectionHandler = None):
	topwin = GemRB.GetView("WIN_TOP")
	if topwin == window:
		return
	
	if topwin:
		topwin.Close()

	if window:
		window.AddAlias("WIN_TOP")
		window.SetFlags(WF_BORDERLESS|IE_GUI_VIEW_IGNORE_EVENTS, OP_OR)
		window.Focus()

		if selectionHandler:
			selectionHandler = lambda win=window, fn=selectionHandler: fn(win)

		SetSelectionChangeHandler (selectionHandler)
	else:
		SetSelectionChangeHandler (None)

def OpenWindowOnce(id, pack, pos=WINDOW_CENTER):
	window = GemRB.GetView(pack, id)
	if window:
		window.Focus()
		return None
	else:
		return GemRB.LoadWindow(id, pack, pos)

def ToggleWindow(id, pack, pos=WINDOW_CENTER):
	window = GemRB.GetView(pack, id)
	if window:
		window.Close()
		return None
	else:
		return GemRB.LoadWindow(id, pack, pos)

# returns buttons and a numerical index
# does nothing new in pst, iwd2 due to layout
# in the rest, it will enable extra button generation for higher resolutions
# Mode determines arrangment direction, horizontal being for party reform and potentially save/load
def GetPortraitButtonPairs (Window, ExtraSlots=0, Mode="vertical"):
	pairs = {}

	if not Window:
		return pairs

	oldSlotCount = 6 + ExtraSlots

	for i in range(min(oldSlotCount, MAX_PARTY_SIZE + ExtraSlots)): # the default chu/game limit or less
		pairs[i] = Window.GetControl (i)

	# nothing left to do
	PartySize = GemRB.GetPartySize ()
	if PartySize <= oldSlotCount:
		return pairs

	if GameCheck.IsIWD2() or GameCheck.IsPST():
		Mode = "horizontal"
		print "Parties larger than 6 are currently not supported in IWD2 and PST! Using 6 ..."
		return pairs

	# GUIWORLD doesn't have a separate portraits window, so we need to skip
	# all this magic when reforming an overflowing party
	if GemRB.GetPartySize () > MAX_PARTY_SIZE:
		return pairs

	# generate new buttons by copying from existing ones
	firstButton = pairs[0]
	firstRect = firstButton.GetFrame ()
	buttonHeight = firstRect["h"]
	buttonWidth = firstRect["w"]
	xOffset = firstRect["x"]
	yOffset = firstRect["y"]
	windowRect = Window.GetFrame()
	windowHeight = windowRect["h"]
	windowWidth = windowRect["w"]
	limit = limitStep = 0
	scale = 0
	portraitGap = 0
	if Mode ==  "horizontal":
		xOffset += 3*buttonWidth  # width of other controls in party reform; we'll draw on the other side (atleast in guiw8, guiw10 has no need for this)
		maxWidth = windowWidth - xOffset
		limit = maxWidth
		limitStep = buttonWidth
	else:
		# reduce it by existing slots + 0 slots in framed views (eg. inventory) and
		# 1 in main game control (for spacing and any other controls below (ai/select all in bg2))
		maxHeight = windowHeight - buttonHeight*6 - buttonHeight/2
		#print "GetPortraitButtonPairs:", ScreenHeight, windowHeight, maxHeight
		if windowHeight != ScreenHeight:
			maxHeight += buttonHeight/2
		limit = maxHeight
		# for framed views, limited to 6, we downscale the buttons to fit, clipping their portraits
		if maxHeight < buttonHeight:
			unused = 20 # remaining unused space below the portraits
			scale = 1
			portraitGap = buttonHeight
			buttonHeight = (buttonHeight*6 + unused) / PartySize
			portraitGap = portraitGap - buttonHeight - 2 # 2 for a quasi border
			limit = windowHeight - buttonHeight*6 + unused
		limitStep = buttonHeight

	for i in range(len(pairs), PartySize):
		if limitStep > limit:
			raise SystemExit, "Not enough window space for so many party members (portraits), bailing out! %d vs width/height of %d/%d" %(limit, buttonWidth, buttonHeight)
		nextID = 1000 + i
		control = Window.GetControl (nextID)
		if control:
			pairs[i] = control
			continue
		if Mode ==  "horizontal":
			Window.CreateButton (nextID, xOffset+i*buttonWidth, yOffset, buttonWidth, buttonHeight)
		else:
			# vertical
			Window.CreateButton (nextID, xOffset, i*buttonHeight+yOffset+i*2*scale, buttonWidth, buttonHeight)

		button = Window.GetControl (nextID)
		button.SetSprites ("GUIRSPOR", 0, 0, 1, 0, 0)

		pairs[i] = button
		limit -= limitStep

	# move the buttons back up, to combine the freed space
	if scale:
		for i in range(oldSlotCount):
			button = pairs[i]
			button.SetSize (buttonWidth, buttonHeight)
			if i == 0:
				continue # don't move the first portrait
			rect = button.GetRect ()
			x = rect["X"]
			y = rect["Y"]
			button.SetPos (x, y-portraitGap*i)

	return pairs

def OpenInventoryWindowClick (pcID):
	import GUIINV
	GemRB.GameSelectPC (pcID, True, SELECT_REPLACE)
	GUIINV.OpenInventoryWindow ()
	return

def OpenPortraitWindow (needcontrols=0, pos=WINDOW_RIGHT|WINDOW_VCENTER):
	global PortraitWindow

	#take care, this window is different in how/iwd
	if GameCheck.HasHOW() and needcontrols:
		PortraitWindow = Window = GemRB.LoadWindow (26, GUICommon.GetWindowPack(), pos)
	else:
		PortraitWindow = Window = GemRB.LoadWindow (1, GUICommon.GetWindowPack(), pos)

	PortraitWindow.AddAlias("PORTWIN")
	PortraitWindow.SetFlags(WF_BORDERLESS|IE_GUI_VIEW_IGNORE_EVENTS, OP_OR)

	if needcontrols and not GameCheck.IsPST(): #not in pst
		print "DEBUG:GUICommonWindows.OpenPortraitWindow:NEEDCONTROLS ON"
		# 1280 and higher don't have this control
		Button = Window.GetControl (8)
		if Button:
			if GameCheck.IsIWD():
				# Rest (iwd)
				Button.SetTooltip (11942)
				Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, RestPress)
			else:
				Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, MinimizePortraits)
		else:
			if GameCheck.HasHOW():
				# Rest (how)
				pos = ScreenHeight - 37
				Button = Window.CreateButton (8, 6, pos, 55, 37)
				Button.SetSprites ("GUIRSBUT", 0,0,1,0,0)
				Button.SetTooltip (11942)
				Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, RestPress)

				pos = pos - 37
				Window.CreateButton (6, 6, pos, 27, 36)

		# AI
		Button = Window.GetControl (6)
		#fixing a gui bug, and while we are at it, hacking it to be easier
		Button.SetSprites ("GUIBTACT", 0, 46, 47, 48, 49)
		Button = InitOptionButton(Window, 'Toggle_AI', AIPress)
		AIPress(0) #this initialises the state and tooltip

		#Select All
		if GameCheck.HasHOW():
			Button = Window.CreateButton (7, 33, pos, 27, 36)
			Button.SetSprites ("GUIBTACT", 0, 50, 51, 50, 51)
		else:
			Button = Window.GetControl (7)
		Button.SetTooltip (10485)
		Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, GUICommon.SelectAllOnPress)
	else:
		# Rest
		if not GameCheck.IsIWD2():
			Button = Window.GetControl (6)
			if Button:
				Button.SetTooltip (11942)
				Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, RestPress)

	PortraitButtons = GetPortraitButtonPairs (Window)
	for i, Button in PortraitButtons.iteritems():
		pcID = i + 1
		
		if not GameCheck.IsPST():
			fontref = "STATES2"
			if GameCheck.IsIWD1() or GameCheck.IsIWD2():
				fontref = "STATES"
			
			Button.SetFont (fontref)
			# label for status flags (dialog, store, level up)
			align = IE_FONT_ALIGN_TOP | IE_FONT_ALIGN_CENTER | IE_FONT_SINGLE_LINE
			label = Button.CreateLabel(200 + i, fontref, "", align) #level up icon is on the right
			label.SetFrame(Button.GetInsetFrame(4))

		if needcontrols or GameCheck.IsIWD2():
			Button.SetEvent (IE_GUI_BUTTON_ON_RIGHT_PRESS, ButtonIndexBinder (OpenInventoryWindowClick, pcID))
		else:
			Button.SetEvent (IE_GUI_BUTTON_ON_RIGHT_PRESS, ButtonIndexBinder(PortraitButtonOnPress, pcID))

		Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, ButtonIndexBinder(PortraitButtonOnPress, pcID))
		Button.SetEvent (IE_GUI_BUTTON_ON_SHIFT_PRESS, ButtonIndexBinder(PortraitButtonOnShiftPress, pcID))
		Button.SetEvent (IE_GUI_BUTTON_ON_DRAG_DROP, ButtonIndexBinder(InventoryCommon.OnDropItemToPC, pcID))

		if GameCheck.IsIWD1() or GameCheck.IsIWD2():
			# overlay a label, so we can display the hp with the correct font. Regular button label
			#   is used by effect icons
			HPLabel = Button.CreateLabel(100+i, "NUMFONT", "", IE_FONT_ALIGN_TOP|IE_FONT_ALIGN_LEFT|IE_FONT_SINGLE_LINE)
			HPLabel.SetUseRGB (True)

		# unlike other buttons, this one lacks extra frames for a selection effect
		# so we create it and shift it to cover the grooves of the image
		# except iwd2's second frame already has it incorporated (but we miscolor it)
		yellow = {'r' : 255, 'g' : 255, 'b' : 0, 'a' : 255}
		green = {'r' : 0, 'g' : 255, 'b' : 0, 'a' : 255}
		if GameCheck.IsIWD2():
			Button.SetBorder (FRAME_PC_SELECTED, green)
			Button.SetBorder (FRAME_PC_TARGET, yellow, 0, 0, Button.GetInsetFrame(2,2,3,3))
		elif GameCheck.IsPST():
			Button.SetBorder (FRAME_PC_SELECTED, green, 0, 0, Button.GetInsetFrame(1,1,2,2))
			Button.SetBorder (FRAME_PC_TARGET, yellow, 0, 0, Button.GetInsetFrame(3,3,4,4))
			Button.SetBAM ("PPPANN", 0, 0, -1) # NOTE: just a dummy, won't be visible
			ButtonHP = Window.GetControl (6 + i)
			ButtonHP.SetEvent (IE_GUI_BUTTON_ON_PRESS, ButtonIndexBinder(PortraitButtonHPOnPress, pcID))
		else:
			Button.SetBorder (FRAME_PC_SELECTED, green, 0, 0, Button.GetInsetFrame(4,3,4,3))
			Button.SetBorder (FRAME_PC_TARGET, yellow, 0, 0, Button.GetInsetFrame(2,2,3,3))

	UpdatePortraitWindow ()
	SelectionChanged ()
	return Window

def UpdatePortraitWindow ():
	"""Updates all of the portraits."""

	Window = PortraitWindow

	pc = GemRB.GameGetSelectedPCSingle ()
	Inventory = GemRB.GetVar ("Inventory")

	PortraitButtons = GetPortraitButtonPairs (Window)
	for i, Button in PortraitButtons.iteritems():
		pcID = i + 1
		if GameCheck.IsPST():
			UpdateAnimatedPortrait(Window, i)
			continue

		pic = GemRB.GetPlayerPortrait (pcID, 1)["Sprite"]
		if Inventory and pc != pcID:
			pic = None

		if pic and GemRB.GetPlayerStat(pcID, IE_STATE_ID) & STATE_DEAD:
			import GUISTORE
			# dead pcs are hidden in all stores but temples
			if GUISTORE.StoreWindow and not GUISTORE.StoreHealWindow:
				pic = None

		if not pic:
			Button.SetFlags (IE_GUI_BUTTON_NO_IMAGE, OP_SET)
			Button.SetState (IE_GUI_BUTTON_DISABLED)
			Button.SetText ("")
			Button.SetTooltip ("")
			continue

		portraitFlags = IE_GUI_BUTTON_PICTURE | IE_GUI_BUTTON_HORIZONTAL | IE_GUI_BUTTON_ALIGN_LEFT | \
						IE_GUI_BUTTON_MULTILINE | IE_GUI_BUTTON_ALIGN_BOTTOM
		# TODO: recheck if this resetting is needed for all or for none
		if GameCheck.IsIWD2():
			Button.SetEvent (IE_GUI_BUTTON_ON_RIGHT_PRESS, ButtonIndexBinder (OpenInventoryWindowClick, pcID))
			Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, ButtonIndexBinder(PortraitButtonOnPress, pcID))

		Button.SetFlags (portraitFlags, OP_SET)

		Button.SetState (IE_GUI_BUTTON_LOCKED)
		Button.SetPicture (pic, "NOPORTSM")

		# character - 1 == bam cycle
		talk = store = flag = blank = ' '
		if GameCheck.IsBG2():
			# as far as I can tell only BG2 has icons for talk or store
			flag = blank = chr(238)
			talk = 154 # dialog icon
			store = 155 # shopping icon

			if pc == pcID:
				if GemRB.GetStore()!=None:
					flag = chr(store)
			# talk icon
			if GemRB.GameGetSelectedPCSingle(1) == pcID:
				flag = chr(talk)

		if LUCommon.CanLevelUp (pcID):
			flag = flag + blank + chr(255)
		elif GameCheck.IsIWD1() or GameCheck.IsIWD2():
			ratio_str, color = GUICommon.SetupDamageInfo (pcID, Button, Window)
			HPLabel = Window.GetControl (100+i)
			HPLabel.SetText (ratio_str)
			HPLabel.SetTextColor (color)

		#add effects on the portrait
		effects = GemRB.GetPlayerStates (pcID)

		numCols = 4 if GameCheck.IsIWD2() else 3
		numEffects = len(effects)

		states = ""
		# calculate the partial row
		idx = numEffects % numCols
		states = effects[0:idx]
		# now do any rows that are full
		for x in range(idx, numEffects):
			if (x - idx) % numCols == 0:
				states = states + "\n"
			states = states + effects[x]

		# FIXME: hack, check shouldn't be needed
		FlagLabel = Window.GetControl (200 + i)
		if FlagLabel:
			if flag != blank:
				FlagLabel.SetText (flag.ljust(3, blank))
			else:
				FlagLabel.SetText ("")
		Button.SetText(states)
	return

def UpdateAnimatedPortrait (Window,i):
	"""Selects the correct portrait cycle depending on character state"""
	# note: there are actually two portraits per chr, eg PPPANN (static), WMPANN (animated)
	Button = Window.GetControl (i)
	ButtonHP = Window.GetControl (6 + i)
	pic = GemRB.GetPlayerPortrait (i+1, 0)["ResRef"]
	if not pic:
		Button.SetFlags (IE_GUI_BUTTON_NO_IMAGE, OP_SET)
		ButtonHP.SetFlags (IE_GUI_BUTTON_NO_IMAGE, OP_SET)
		return

	state = GemRB.GetPlayerStat (i+1, IE_STATE_ID)
	hp = GemRB.GetPlayerStat (i+1, IE_HITPOINTS)
	hp_max = GemRB.GetPlayerStat (i+1, IE_MAXHITPOINTS)
	if state & STATE_DEAD:
			cycle = 9
	elif state & STATE_HELPLESS:
			cycle = 8
	elif state & STATE_PETRIFIED:
			cycle = 7
	elif state & STATE_PANIC:
			cycle = 6
	elif state & STATE_POISONED:
			cycle = 2
	elif hp<hp_max/2:
		cycle = 4
	else:
		cycle = 0

	Button.SetFlags (IE_GUI_BUTTON_PICTURE | IE_GUI_BUTTON_ANIMATED | IE_GUI_BUTTON_MULTILINE, OP_SET)
	if cycle<6:
		Button.SetFlags (IE_GUI_BUTTON_PLAYRANDOM, OP_OR)

	Button.SetAnimation (pic, cycle)
	ButtonHP.SetFlags(IE_GUI_BUTTON_PICTURE, OP_SET)

	if hp_max<1:
		ratio = 0.0
	else:
		ratio = (hp + 0.0) / hp_max
		if ratio > 1.0: ratio = 1.0

	r = int (255 * (1.0 - ratio))
	g = int (255 * ratio)

	ButtonHP.SetText ("%d / %d" %(hp, hp_max))
	ButtonHP.SetTextColor ({'r' : r, 'g' : g, 'b' : 0}, False)
	ButtonHP.SetBAM ('FILLBAR', 0, 0, -1)
	ButtonHP.SetPictureClipping (ratio)

	#print "PORTRAIT DEBUG:"
	#print "state: " + str(state) + " hp: " + str(hp) + " hp_max: " + str(hp_max) + "ratio: " + str(ratio) + " cycle: " + str(cycle) + " state: " + str(state)

	if GemRB.GetVar('Health Bar Settings') & (1 << i):
		op = OP_OR
	else:
		op = OP_NAND
	ButtonHP.SetFlags (IE_GUI_BUTTON_PICTURE | IE_GUI_BUTTON_NO_TEXT, op)

	return

def ButtonIndexBinder (fn, idx):
	# returned function must take no parameters
	return lambda: fn(idx)

def PortraitButtonOnPress (i):
	"""Selects the portrait individually."""

	if GemRB.GameControlGetTargetMode() != TARGET_MODE_NONE:
		GemRB.ActOnPC (i)
		return

	if (not SelectionChangeHandler):
		if GemRB.GameIsPCSelected (i):
			GemRB.GameControlSetScreenFlags (SF_CENTERONACTOR, OP_OR)
		GemRB.GameSelectPC (i, True, SELECT_REPLACE)
	else:
		GemRB.GameSelectPCSingle (i)
		SelectionChanged ()
	return

def PortraitButtonOnShiftPress (i):
	"""Handles selecting multiple portaits with shift."""

	if (not SelectionChangeHandler):
		sel = GemRB.GameIsPCSelected (i)
		sel = not sel
		GemRB.GameSelectPC (i, sel)
	else:
		GemRB.GameSelectPCSingle (i)
		SelectionChanged ()
	return

def PortraitButtonHPOnPress (i): ##pst hitpoint display
	Window = PortraitWindow

	i = GemRB.GetVar ('PressedPortraitHP')
	hbs = GemRB.GetVar('Health Bar Settings')
	ButtonHP = Window.GetControl (5 + i)

	if hbs & (1 << (i-1)):
		op = OP_NAND
	else:
		op = OP_OR

	ButtonHP.SetFlags (IE_GUI_BUTTON_PICTURE | IE_GUI_BUTTON_NO_TEXT, op)
	GemRB.SetVar('Health Bar Settings', hbs ^ (1 << (i-1)))
	return

def SelectionChanged ():
	"""Ran by the Game class when a PC selection is changed."""

	global PortraitWindow

	# FIXME: hack. If defined, display single selection
	GemRB.SetVar ("ActionLevel", UAW_STANDARD)
	if (not SelectionChangeHandler):
		UpdateActionsWindow ()
		PortraitButtons = GetPortraitButtonPairs (PortraitWindow)
		for i, Button in PortraitButtons.iteritems():
			Button.EnableBorder (FRAME_PC_SELECTED, GemRB.GameIsPCSelected (i + 1))
		if SelectionChangeMultiHandler:
			SelectionChangeMultiHandler ()
	else:
		sel = GemRB.GameGetSelectedPCSingle ()

		#update mage school
		GemRB.SetVar ("MAGESCHOOL", 0)
		Kit = GUICommon.GetKitIndex (sel)
		if Kit and CommonTables.KitList.GetValue (Kit, 7) == 1:
			MageTable = GemRB.LoadTable ("magesch")
			GemRB.SetVar ("MAGESCHOOL", MageTable.FindValue (3, CommonTables.KitList.GetValue (Kit, 6) ) )

		PortraitButtons = GetPortraitButtonPairs (PortraitWindow)
		for i, Button in PortraitButtons.iteritems():
			Button.EnableBorder (FRAME_PC_SELECTED, i + 1 == sel)
	import CommonWindow
	CommonWindow.CloseContainerWindow()
	if SelectionChangeHandler:
		SelectionChangeHandler ()
	return

def ActionStopPressed ():
	for i in GemRB.GetSelectedActors():
		GemRB.ClearActions (i)
	return

def ActionTalkPressed ():
	GemRB.GameControlSetTargetMode (TARGET_MODE_TALK,GA_NO_DEAD|GA_NO_ENEMY|GA_NO_HIDDEN)

def ActionAttackPressed ():
	GemRB.GameControlSetTargetMode (TARGET_MODE_ATTACK,GA_NO_DEAD|GA_NO_SELF|GA_NO_HIDDEN)

def ActionDefendPressed ():
	GemRB.GameControlSetTargetMode (TARGET_MODE_DEFEND,GA_NO_SELF|GA_NO_ENEMY|GA_NO_HIDDEN)

def ActionThievingPressed ():
	GemRB.GameControlSetTargetMode (TARGET_MODE_PICK, GA_NO_DEAD|GA_NO_SELF|GA_NO_ENEMY|GA_NO_HIDDEN)

def MinimizePortraits(): #bg2
	GemRB.GameSetScreenFlags(GS_PORTRAITPANE, OP_OR)

def DisableAnimatedWindows (): #pst
	# FIXME: this is just pausing the game. what is the actual intent here?
	GemRB.GamePause (1,3)

def EnableAnimatedWindows (): #pst
	# FIXME: this is just unpausing the game. what is the actual intent here?
	GemRB.GamePause (0,3)

def SetItemButton (Window, Button, Slot, PressHandler, RightPressHandler): #relates to pst containers
	if Slot != None:
		Item = GemRB.GetItem (Slot['ItemResRef'])
		identified = Slot['Flags'] & IE_INV_ITEM_IDENTIFIED
		#Button.SetVarAssoc ("LeftIndex", LeftTopIndex+i)
		#Button.SetSprites ('IVSLOT', 0,  0, 0, 0, 0)
		Button.SetItemIcon (Slot['ItemResRef'],0)

		if Item['MaxStackAmount'] > 1:
			Button.SetText (str (Slot['Usages0']))
		else:
			Button.SetText ('')


		if not identified or Item['ItemNameIdentified'] == -1:
			Button.SetTooltip (Item['ItemName'])
		else:
			Button.SetTooltip (Item['ItemNameIdentified'])

		#Button.SetFlags (IE_GUI_BUTTON_PICTURE, OP_OR)
		#Button.SetFlags (IE_GUI_BUTTON_NO_IMAGE, OP_NAND)

		Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, PressHandler)
		Button.SetEvent (IE_GUI_BUTTON_ON_RIGHT_PRESS, RightPressHandler)
		#Button.SetEvent (IE_GUI_BUTTON_ON_SHIFT_PRESS, ShiftPressHandler)
		#Button.SetEvent (IE_GUI_BUTTON_ON_DRAG_DROP, DragDropHandler)

	else:
		#Button.SetVarAssoc ("LeftIndex", -1)
		Button.SetItemIcon ('')
		Button.SetTooltip (4273)  # Ground Item
		Button.SetText ('')
		Button.SetFlags (IE_GUI_BUTTON_PICTURE, OP_NAND)

		Button.SetEvent (IE_GUI_BUTTON_ON_PRESS, None)
		Button.SetEvent (IE_GUI_BUTTON_ON_RIGHT_PRESS, None)
		#Button.SetEvent (IE_GUI_BUTTON_ON_SHIFT_PRESS, None)
		#Button.SetEvent (IE_GUI_BUTTON_ON_DRAG_DROP, None)

def OpenWaitForDiscWindow ():
	global DiscWindow

	if DiscWindow:
		if DiscWindow:
			DiscWindow.Unload ()
		EnableAnimatedWindows ()
		DiscWindow = None
		return

	DiscWindow = Window = GemRB.LoadWindow (0, "GUIID")
	label = DiscWindow.GetControl (0)

	disc_num = GemRB.GetVar ("WaitForDisc")
	#disc_path = GemRB.GetVar ("WaitForDiscPath")
	disc_path = 'XX:'

	text = GemRB.GetString (31483) + " " + str (disc_num) + " " + GemRB.GetString (31569) + " " + disc_path + "\n" + GemRB.GetString (49152)
	label.SetText (text)
	DisableAnimatedWindows ()
	# 31483 - Please place PS:T disc number
	# 31568 - Please place the PS:T DVD
	# 31569 - in drive
	# 31570 - Wrong disc in drive
	# 31571 - There is no disc in drive
	# 31578 - No disc could be found in drive. Please place Disc 1 in drive.
	# 49152 - To quit the game, press Alt-F4

def SetPSTGamedaysAndHourToken ():
	currentTime = GemRB.GetGameTime()
	hours = (currentTime % 7200) / 300
	if hours < 12:
		ampm = "AM"
	else:
		ampm = "PM"
		hours -= 12
	minutes = (currentTime % 300) / 60

	GemRB.SetToken ('CLOCK_HOUR', str (hours))
	GemRB.SetToken ('CLOCK_MINUTE', '%02d' %minutes)
	GemRB.SetToken ('CLOCK_AMPM', ampm)

def UpdateClock ():
	global ActionsWindow, OptionsWindow

	if GameCheck.IsPST ():
		SetPSTGamedaysAndHourToken ()

	else:
		Clock = None
		if OptionsWindow:
			if GameCheck.IsIWD2():
				Clock = OptionsWindow.GetControl (10)
			elif OptionsWindow.GetControl(9):
				Clock = OptionsWindow.GetControl (9)
		elif ActionsWindow and ActionsWindow.GetControl (62):
			Clock = ActionsWindow.GetControl (62)
		if Clock and (Clock.HasAnimation("CGEAR") or GameCheck.IsIWD2()):
			Hours = (GemRB.GetGameTime () % 7200) / 300
			GUICommon.SetGamedaysAndHourToken ()
			Clock.SetBAM ("CDIAL", 0, (Hours + 12) % 24)
			Clock.SetTooltip (GemRB.GetString (16041)) # refetch the string, since the tokens changed

def CheckLevelUp(pc):
	GemRB.SetVar ("CheckLevelUp"+str(pc), LUCommon.CanLevelUp (pc))

def ToggleAlwaysRun():
	GemRB.GameControlToggleAlwaysRun()

def RestPress ():
	CloseTopWindow ()
	# only rest if the dream scripts haven't already
	# bg2 completely offloaded resting to them - if there's a talk, it has to call Rest(Party) itself
	if not GemRB.RunRestScripts ():
		# ensure the scripts run before the actual rest
		GemRB.SetTimedEvent (RealRestPress, 2)

def RealRestPress ():
	GemRB.RestParty(0, 0, 1)
	return
