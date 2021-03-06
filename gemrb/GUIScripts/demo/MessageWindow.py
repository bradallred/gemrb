import GemRB
import Tests
import GUICommon
import GUICommonWindows
import CommonWindow
import GUIClasses
from GameCheck import MAX_PARTY_SIZE
from GUIDefines import *

MessageWindow = 0
#PortraitWindow = 0
#OptionsWindow = 0
TMessageTA = 0 # for dialog code

def OnLoad():
#	global PortraitWindow, OptionsWindow

	GemRB.GameSetPartySize(MAX_PARTY_SIZE)
	GemRB.GameSetProtagonistMode(1)

#	GUICommonWindows.PortraitWindow = None
#	GUICommonWindows.ActionsWindow = None
#	GUICommonWindows.OptionsWindow = None
 
#	ActionsWindow = GemRB.LoadWindow(3)
#	OptionsWindow = GemRB.LoadWindow(0)
#	PortraitWindow = GUICommonWindows.OpenPortraitWindow(1)
	
#	GUICommonWindows.OpenActionsWindowControls (ActionsWindow)
#	GUICommonWindows.SetupMenuWindowControls (OptionsWindow, 1, None)

	UpdateControlStatus()

	# set up some *initial* text (UpdateControlStatus will get called several times)
	TMessageTA.SetFlags (IE_GUI_TEXTAREA_AUTOSCROLL|IE_GUI_TEXTAREA_HISTORY)
	results = Tests.RunTests ()
	TMessageTA.SetText ("[cap]D[/cap]emo " + "DEMO "*40 + "\n" + results)
	print results

def UpdateControlStatus():
	global MessageWindow, TMessageTA

	TMessageWindow = 0
	TMessageTA = 0
	GSFlags = GemRB.GetGUIFlags()
	GSFlags = GSFlags - GS_LARGEDIALOG
	Override = GSFlags&GS_DIALOG

	TMessageWindow = GemRB.LoadWindow(0, GUICommon.GetWindowPack())
	TMessageTA = TMessageWindow.GetControl(0)

	hideflag = IsGameGUIHidden()
	MessageWindow = GemRB.GetVar("MessageWindow")
	MessageTA = GUIClasses.GTextArea(MessageWindow,GemRB.GetVar("MessageTextArea"))
	if MessageWindow > 0 and MessageWindow != TMessageWindow.ID:
		TMessageTA = MessageTA.SubstituteForControl(TMessageTA)
		GUIClasses.GWindow(MessageWindow).Unload()

	GemRB.SetVar("MessageWindow", TMessageWindow.ID)
	GemRB.SetVar("MessageTextArea", TMessageTA.ID)
	if Override:
		TMessageTA.Focus()

	SetGameGUIHidden(hideflag)

