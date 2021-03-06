/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#include "GUI/GameControl.h"

#include "strrefs.h"
#include "win32def.h"

#include "CharAnimations.h"
#include "DialogHandler.h"
#include "DisplayMessage.h"
#include "Game.h"
#include "GameData.h"
#include "GlobalTimer.h"
#include "GUIScriptInterface.h"
#include "ImageMgr.h"
#include "Interface.h"
#include "KeyMap.h"
#include "PathFinder.h"
#include "ScriptEngine.h"
#include "TileMap.h"
#include "Video.h"
#include "damages.h"
#include "ie_cursors.h"
#include "opcode_params.h"
#include "GameScript/GSUtils.h"
#include "GUI/EventMgr.h"
#include "GUI/TextArea.h"
#include "RNG/RNG_SFMT.h"
#include "Scriptable/Container.h"
#include "Scriptable/Door.h"
#include "Scriptable/InfoPoint.h"

#include <cmath>

namespace GemRB {

#define DEBUG_SHOW_INFOPOINTS   0x01
#define DEBUG_SHOW_CONTAINERS   0x02
#define DEBUG_SHOW_DOORS	DEBUG_SHOW_CONTAINERS
#define DEBUG_SHOW_LIGHTMAP     0x08

#define FORMATIONSIZE 10
typedef Point formation_type[FORMATIONSIZE];
ieDword formationcount;
static formation_type *formations=NULL;
static ieResRef TestSpell="SPWI207";

//If one of the actors has tracking on, the gamecontrol needs to display
//arrow markers on the edges to point at detected monsters
//tracterID is the tracker actor's global ID
//distance is the detection distance
void GameControl::SetTracker(Actor *actor, ieDword dist)
{
	trackerID = actor->GetGlobalID();
	distance = dist;
}

GameControl::GameControl(const Region& frame)
: View(frame)
{
	if (!formations) {
		ReadFormations();
	}
	//this is the default action, individual actors should have one too
	//at this moment we use only this
	//maybe we don't even need it
	spellCount = spellIndex = spellOrItem = spellSlot = 0;
	spellUser = NULL;
	spellName[0] = 0;
	user = NULL;
	lastActorID = 0;
	trackerID = 0;
	distance = 0;
	overDoor = NULL;
	overContainer = NULL;
	overInfoPoint = NULL;
	drawPath = NULL;
	pfs.null();
	lastCursor = IE_CURSOR_INVALID;
	moveX = moveY = 0;
	numScrollCursor = 0;
	DebugFlags = 0;
	AIUpdateCounter = 1;

	ieDword tmp=0;
	core->GetDictionary()->Lookup("Always Run", tmp);
	AlwaysRun = !!tmp;

	ClearMouseState();
	ResetTargetMode();

	core->GetDictionary()->Lookup("Center",tmp);
	if (tmp) {
		ScreenFlags=SF_ALWAYSCENTER|SF_CENTERONACTOR;
	} else {
		ScreenFlags = SF_CENTERONACTOR;
	}
	DialogueFlags = 0;
	dialoghandler = new DialogHandler();
	DisplayText = NULL;
	DisplayTextTime = 0;
	updateVPTimer = true;

	EventMgr::EventCallback* cb = new MethodCallback<GameControl, const Event&, bool>(this, &GameControl::OnGlobalMouseMove);
	EventMgr::RegisterEventMonitor(cb, Event::MouseMoveMask);
	EventMgr::EventCallback *cb2 = new MethodCallback<GameControl, const Event&, bool>(this, &GameControl::DispatchEvent);
	EventMgr::RegisterEventMonitor(cb2, Event::KeyDownMask);
}

//TODO:
//There could be a custom formation which is saved in the save game
//alternatively, all formations could be saved in some compatible way
//so it doesn't cause problems with the original engine
void GameControl::ReadFormations()
{
	unsigned int i,j;
	AutoTable tab("formatio");
	if (!tab) {
		// fallback
		formationcount = 1;
		formations = (formation_type *) calloc(1,sizeof(formation_type) );
		return;
	}
	formationcount = tab->GetRowCount();
	formations = (formation_type *) calloc(formationcount, sizeof(formation_type));
	for(i=0; i<formationcount; i++) {
		for(j=0;j<FORMATIONSIZE;j++) {
			short k=(short) atoi(tab->QueryField(i,j*2));
			formations[i][j].x=k;
			k=(short) atoi(tab->QueryField(i,j*2+1));
			formations[i][j].y=k;
		}
	}
}

//returns a single point offset for a formation
//formation: the formation type
//pos: the actor's slot ID
Point GameControl::GetFormationOffset(ieDword formation, ieDword pos)
{
	if (formation>=formationcount) formation = 0;
	if (pos>=FORMATIONSIZE) pos=FORMATIONSIZE-1;
	return formations[formation][pos];
}

//WARNING: don't pass p as a reference because it gets modified
Point GameControl::GetFormationPoint(Map *map, unsigned int pos, const Point& src, Point p)
{
	int formation=core->GetGame()->GetFormation();
	if (pos>=FORMATIONSIZE) pos=FORMATIONSIZE-1;

	// calculate angle
	double angle;
	double xdiff = src.x - p.x;
	double ydiff = src.y - p.y;
	if (ydiff == 0) {
		if (xdiff > 0) {
			angle = M_PI_2;
		} else {
			angle = -M_PI_2;
		}
	} else {
		angle = std::atan(xdiff/ydiff);
		if (ydiff < 0) angle += M_PI;
	}

	// calculate new coordinates by rotating formation around (0,0)
	double newx = -formations[formation][pos].x * std::cos(angle) + formations[formation][pos].y * std::sin(angle);
	double newy = formations[formation][pos].x * std::sin(angle) + formations[formation][pos].y * std::cos(angle);
	p.x += (int)newx;
	p.y += (int)newy;

	if (p.x < 0) p.x = 8;
	if (p.y < 0) p.y = 8;
	if (p.x > map->GetWidth()*16) p.x = map->GetWidth()*16 - 8;
	if (p.y > map->GetHeight()*12) p.y = map->GetHeight()*12 - 8;

	if(map->GetCursor(p) == IE_CURSOR_BLOCKED) {
		//we can't get there --> adjust position
		p.x/=16;
		p.y/=12;
		map->AdjustPosition(p);
		p.x*=16;
		p.y*=12;
	}
	return p;
}

void GameControl::ClearMouseState()
{
	isSelectionRect = false;
	isFormationRotation = false;
	isDoubleClick = false;
	
	SetCursor(NULL);
}

// generate an action to do the actual movement
// only PST supports RunToPoint
void GameControl::CreateMovement(Actor *actor, const Point &p)
{
	char Tmp[256];
	Action *action = NULL;
	static bool CanRun = true;

	//try running (in PST) only if not encumbered
	if (CanRun && ShouldRun(actor)) {
		sprintf( Tmp, "RunToPoint([%d.%d])", p.x, p.y );
		action = GenerateAction( Tmp );
		//if it didn't work don't insist
		if (!action)
			CanRun = false;
	}
	if (!action) {
		sprintf( Tmp, "MoveToPoint([%d.%d])", p.x, p.y );
		action = GenerateAction( Tmp );
	}

	actor->CommandActor(action);
}

// were we instructed to run and can handle it (no movement impairments)?
bool GameControl::ShouldRun(Actor *actor) const
{
	if (!actor) return false;
	ieDword speed = actor->CalculateSpeed(true);
	if (speed != actor->GetStat(IE_MOVEMENTRATE)) {
		return false;
	}
	return (isDoubleClick || AlwaysRun);
}

GameControl::~GameControl(void)
{
	if (formations)	{
		free( formations );
		formations = NULL;
	}
	delete dialoghandler;
	delete DisplayText;
}

// ArrowSprite cycles
//  321
//  4 0
//  567

#define D_LEFT   1
#define D_UP     2
#define D_RIGHT  4
#define D_BOTTOM 8
// Direction Bits
//  326
//  1 4
//  98c

static const int arrow_orientations[16]={
// 0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
	-1, 4, 2, 3, 0,-1, 1,-1, 6, 5,-1,-1, 7,-1,-1,-1
};

//Draws arrow markers along the edge of the game window
//WARNING:don't use reference for point, because it is altered
void GameControl::DrawArrowMarker(Point p, const Color& color)
{
	ieDword draw = 0;
	if (p.x < vpOrigin.x + 64) {
		p.x = vpOrigin.x + 64;
		draw|= D_LEFT;
	}
	if (p.y < vpOrigin.y) {
		p.y = vpOrigin.y;
		draw |= D_UP;
	}

	Sprite2D *spr = core->GetScrollCursorSprite(0,0);
	int tmp = 64;
	if (p.x > vpOrigin.x + frame.w - (tmp + spr->Width)) {
		p.x = vpOrigin.x + frame.w - tmp;
		draw |= D_RIGHT;
	}

	Region mwinframe = core->GetMessageTextArea()->GetWindow()->Frame();

	tmp = mwinframe.h + 48;
	if (p.y > vpOrigin.y + frame.h - (tmp + spr->Height)) {
		p.y = vpOrigin.y + frame.h - tmp;
		draw |= D_BOTTOM;
	}

	if (arrow_orientations[draw]>=0) {
		Video* video = core->GetVideoDriver();
		Sprite2D *arrow = core->GetScrollCursorSprite(arrow_orientations[draw], 0);
		video->BlitGameSprite(arrow, p.x - vpOrigin.x, p.y - vpOrigin.y, BLIT_TINTED, color, NULL);
		arrow->release();
	}
	spr->release();
}

void GameControl::DrawTargetReticle(Point p, int size, bool animate, bool flash, bool actorSelected)
{
	// reticles are never drawn in cutscenes
	if (GetScreenFlags()&SF_CUTSCENE)
		return;

	unsigned short step = 0;
	if (animate) {
		// generates "step" from sequence 3 2 1 0 1 2 3 4
		// updated each 1/15 sec
		++step = tp_steps [(GetTickCount() >> 6) & 7];
	} else {
		step = 3;
	}
	if (size < 3) size = 3;

	/* segments should not go outside selection radius */
	unsigned short xradius = (size * 4) - 5;
	unsigned short yradius = (size * 3) - 5;

	Color color = ColorGreen;
	if (flash) {
		if (step & 2) {
			color = ColorWhite;
		} else {
			if (!actorSelected) color = ColorGreenDark;
		}
	}

	p = p - vpOrigin;
	// TODO: 0.5 and 0.7 are pretty much random values
	// right segment
	core->GetVideoDriver()->DrawEllipseSegment( p + Point(step, 0),
											   xradius, yradius, color, -0.5, 0.5 );
	// top segment
	core->GetVideoDriver()->DrawEllipseSegment( p - Point(0, step),
											   xradius, yradius, color, -0.7 - M_PI_2, 0.7 - M_PI_2 );
	// left segment
	core->GetVideoDriver()->DrawEllipseSegment( p - Point(step, 0),
											   xradius, yradius, color, -0.5 - M_PI, 0.5 - M_PI );
	// bottom segment
	core->GetVideoDriver()->DrawEllipseSegment( p + Point(0, step),
											   xradius, yradius, color, -0.7 - M_PI - M_PI_2, 0.7 - M_PI - M_PI_2 );
}
	
void GameControl::WillDraw()
{
	UpdateCursor();

	bool update_scripts = !(DialogueFlags & DF_FREEZE_SCRIPTS);
	
	// handle keeping the actor in the spotlight, but only when unpaused
	if ((ScreenFlags & SF_ALWAYSCENTER) && update_scripts) {
		Actor *star = core->GetFirstSelectedActor();
		moveX = star->Pos.x - vpOrigin.x - frame.w/2;
		moveY = star->Pos.y - vpOrigin.y - frame.h/2;
	}

	if (moveX || moveY) {
		MoveViewportTo( vpOrigin + Point(moveX, moveY), false );

		if ((Flags() & IgnoreEvents) == 0 && core->GetMouseScrollSpeed()) {
			int cursorFrame = 0; // right
			if (moveY < 0) {
				cursorFrame = 2; // up
				if (moveX > 0) cursorFrame--; // +right
				else if (moveX < 0) cursorFrame++; // +left
			} else if (moveY > 0) {
				cursorFrame = 6; // down
				if (moveX > 0) cursorFrame++; // +right
				else if (moveX < 0) cursorFrame--; // +left
			} else if (moveX < 0) {
				cursorFrame = 4; // left
			}

			// set these cursors on game window so they are universal
			Sprite2D* cursor = core->GetScrollCursorSprite(cursorFrame, numScrollCursor);
			window->SetCursor(cursor);
			Sprite2D::FreeSprite(cursor);
			
			numScrollCursor = (numScrollCursor+1) % 15;
		}
	} else if (!window->IsDisabled()) {
		window->SetCursor(NULL);
	}
}

/** Draws the Control on the Output Display */
void GameControl::DrawSelf(Region screen, const Region& /*clip*/)
{
	Game* game = core->GetGame();
	Map *area = game->GetCurrentArea();

	// FIXME: some of this should happen during mouse events
	// setup outlines
	InfoPoint *i;
	unsigned int idx;
	for (idx = 0; (i = area->TMap->GetInfoPoint( idx )); idx++) {
		i->Highlight = false;
		if (overInfoPoint == i && target_mode) {
			if (i->VisibleTrap(0)) {
				i->outlineColor = ColorGreen;
				i->Highlight = true;
				continue;
			}
		}
		if (i->VisibleTrap(DebugFlags & DEBUG_SHOW_INFOPOINTS)) {
			i->outlineColor = ColorRed; // traps
		} else if (DebugFlags & DEBUG_SHOW_INFOPOINTS) {
			i->outlineColor = ColorBlue; // debug infopoints
			i->outlineColor.a = 0x80;
		} else {
			continue;
		}
		i->Highlight = true;
	}

	// FIXME: some of this should happen during mouse events
	Door *d;
	for (idx = 0; (d = area->TMap->GetDoor( idx )); idx++) {
		d->Highlight = false;
		if (d->Flags & DOOR_HIDDEN) {
			continue;
		}
		if (overDoor == d) {
			if (target_mode) {
				if (d->Visible() && (d->VisibleTrap(0) || (d->Flags & DOOR_LOCKED))) {
					// only highlight targettable doors
					d->outlineColor = ColorGreen;
					d->Highlight = true;
					continue;
				}
			} else if (!(d->Flags & DOOR_SECRET)) {
				// mouse over, not in target mode, no secret door
				d->outlineColor = ColorCyan;
				d->Highlight = true;
				continue;
			}
		}
		if (d->VisibleTrap(0)) {
			d->outlineColor = ColorRed; // traps
		} else if (d->Flags & DOOR_SECRET) {
			if (DebugFlags & DEBUG_SHOW_DOORS || d->Flags & DOOR_FOUND) {
				d->outlineColor = ColorMagenta; // found hidden door
			} else {
				// secret door is invisible
				continue;
			}
		} else if (DebugFlags & DEBUG_SHOW_DOORS) {
			d->outlineColor = ColorCyan; // debug doors
		} else {
			continue;
		}
		d->Highlight = true;
	}

	// FIXME: some of this should happen during mouse events
	Container *c;
	for (idx = 0; (c = area->TMap->GetContainer( idx )); idx++) {
		if (c->Flags & CONT_DISABLED) {
			continue;
		}

		c->Highlight = false;
		if (overContainer == c && target_mode) {
			if (c->VisibleTrap(0) || (c->Flags & CONT_LOCKED)) {
				// only highlight targettable containers
				c->outlineColor = ColorGreen;
				c->Highlight = true;
				continue;
			}
		} else if (overContainer == c) {
			// mouse over, not in target mode
			c->outlineColor = ColorCyan;
			c->Highlight = true;
			continue;
		}
		if (c->VisibleTrap(0)) {
			c->outlineColor = ColorRed; // traps
		} else if (DebugFlags & DEBUG_SHOW_CONTAINERS) {
			c->outlineColor = ColorCyan; // debug containers
		} else {
			continue;
		}
		c->Highlight = true;
	}

	//drawmap should be here so it updates fog of war
	bool update_scripts = !(DialogueFlags & DF_FREEZE_SCRIPTS);
	area->DrawMap( Viewport() );
	game->DrawWeather(screen, update_scripts);

	if (trackerID) {
		Actor *actor = area->GetActorByGlobalID(trackerID);

		if (actor) {
			Actor **monsters = area->GetAllActorsInRadius(actor->Pos, GA_NO_DEAD|GA_NO_LOS|GA_NO_UNSCHEDULED, distance);

			int i = 0;
			while(monsters[i]) {
				Actor *target = monsters[i++];
				if (target->InParty) continue;
				if (target->GetStat(IE_NOTRACKING)) continue;
				DrawArrowMarker(target->Pos, ColorBlack);
			}
			free(monsters);
		} else {
			trackerID = 0;
		}
	}

	if (lastActorID) {
		Actor* actor = GetLastActor();
		if (actor) {
			DrawArrowMarker(actor->Pos, ColorGreen);
		}
	}

	Video* video = core->GetVideoDriver();
	// Draw selection rect
	if (isSelectionRect) {
		Region r = SelectionRect();
		r.x -= vpOrigin.x;
		r.y -= vpOrigin.y;
		video->DrawRect(r, ColorGreen, false );
	}

	Point gameMousePos = GameMousePos();
	// draw reticles
	if (isFormationRotation) {
		Actor *actor;
		int max = game->GetPartySize(false);
		// we only care about PCs and not summons for this. the summons will be included in
		// the final mouse up event.
		int formationPos = 0;
		for(int idx = 1; idx<=max; idx++) {
			actor = game->FindPC(idx);
			if (actor && actor->IsSelected()) {
				// transform the formation point
				Point p = GetFormationPoint(actor->GetCurrentArea(), formationPos++, gameMousePos, gameClickPoint);
				DrawTargetReticle(p, 4, false);
			}
		}
	}

	// Show wallpolygons
	if (DebugFlags & DEBUG_SHOW_INFOPOINTS) {

		unsigned int count = area->GetWallCount();
		for (unsigned int i = 0; i < count; ++i) {
			Wall_Polygon* poly = area->GetWallGroup(i);
			if (!poly) continue;
			// yellow
			Color c;
			c.r = 0x7F;
			c.g = 0x7F;
			c.b = 0;
			c.a = 0;
			//if polygon is disabled, make it grey
			if (poly->wall_flag&WF_DISABLED) {
				c.b = 0x7F;
			}

			Color fillc(c.r, c.g, c.b, c.a/2);
			video->DrawPolygon( poly, vpOrigin, fillc, true );
			video->DrawPolygon( poly, vpOrigin, c, false );
		}
	}

	// Draw path
	if (drawPath) {
		PathNode* node = drawPath;
		while (true) {
			Point p( ( node-> x*16) + 8, ( node->y*12 ) + 6 );
			if (!node->Parent) {
				video->DrawCircle( p, 2, ColorRed );
			} else {
				short oldX = ( node->Parent-> x*16) + 8, oldY = ( node->Parent->y*12 ) + 6;
				video->DrawLine( Point(oldX, oldY), p, ColorGreen );
			}
			if (!node->Next) {
				video->DrawCircle( p, 2, ColorGreen );
				break;
			}
			node = node->Next;
		}
	}

	// Draw lightmap
	if (DebugFlags & DEBUG_SHOW_LIGHTMAP) {
		Sprite2D* spr = area->LightMap->GetSprite2D();
		video->BlitSprite( spr, 0, 0 );
		Sprite2D::FreeSprite( spr );
		Region point( gameMousePos.x / 16, gameMousePos.y / 12, 2, 2 );
		video->DrawRect( point, ColorRed );
	}

	if (core->HasFeature(GF_ONSCREEN_TEXT) && DisplayText) {
		core->GetTextFont()->Print(screen, *DisplayText, core->InfoTextPalette, IE_FONT_ALIGN_CENTER | IE_FONT_ALIGN_MIDDLE);
		if (update_scripts) {
			// just replicating original engine behaviour
			if (DisplayTextTime == 0) {
				SetDisplayText((String*)NULL, 0);
			} else {
				DisplayTextTime--;
			}
		}
	}
}

// this existly only so tab can be handled
// it's used both for tooltips everywhere and hp display on game control
bool GameControl::DispatchEvent(const Event& event)
{
	Game *game = core->GetGame();
	if (!game) return false;

	if (event.keyboard.keycode == GEM_TAB) {
		// show partymember hp/maxhp as overhead text
		for (int pm=0; pm < game->GetPartySize(false); pm++) {
			Actor *pc = game->GetPC(pm, true);
			if (!pc) continue;
			pc->DisplayHeadHPRatio();
		}
		return true;
	} else if (event.keyboard.keycode == GEM_ESCAPE) {
		core->SetEventFlag(EF_ACTION|EF_RESETTARGET);
	}
	return false;
}

/** Key Press Event */
bool GameControl::OnKeyPress(const KeyboardEvent& Key, unsigned short mod)
{
	unsigned int i, pc;
	Game* game = core->GetGame();

	KeyboardKey keycode = Key.keycode;
	switch (keycode) {
		case GEM_UP:
		case GEM_DOWN:
		case GEM_LEFT:
		case GEM_RIGHT:
			{
				ieDword keyScrollSpd = 64;
				core->GetDictionary()->Lookup("Keyboard Scroll Speed", keyScrollSpd);
				if (keycode >= GEM_UP) {
					int v = (keycode == GEM_UP) ? -1 : 1;
					Scroll( Point(0, keyScrollSpd * v) );
				} else {
					int v = (keycode == GEM_LEFT) ? -1 : 1;
					Scroll( Point(keyScrollSpd * v, 0) );
				}
			}
			break;
		case GEM_ALT:
#ifdef ANDROID
		case 'c': // show containers in ANDROID, GEM_ALT is not possible to use
#endif
			DebugFlags |= DEBUG_SHOW_CONTAINERS;
			break;
		case GEM_TAB: // show partymember hp/maxhp as overhead text
		// fallthrough
		case GEM_ESCAPE: // redraw actionbar
			// do nothing; these are handled in DispatchEvent due to tab having two functions
			break;
		case '0':
			game->SelectActor( NULL, false, SELECT_NORMAL );
			i = game->GetPartySize(false)/2+1;
			while(i--) {
				SelectActor(i, true);
			}
			break;
		case '-':
			game->SelectActor( NULL, true, SELECT_NORMAL );
			i = game->GetPartySize(false)/2+1;
			while(i--) {
				SelectActor(i, false);
			}
			break;
		case '=':
			SelectActor(-1);
			break;
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
			game->SelectPCSingle(keycode-'0');
			SelectActor(keycode-'0');
			break;
		case '7': // 1 & 2
		case '8': // 3 & 4
		case '9': // 5 & 6
			game->SelectActor( NULL, false, SELECT_NORMAL );
			i = game->GetPartySize(false);
			pc = 2*(keycode - '6')-1;
			if (pc >= i) {
				SelectActor(i, true);
				break;
			}
			SelectActor(pc, true);
			SelectActor(pc+1, true);
			break;
		default:
			// the random bitshift is to skip checking hotkeys with mods
			// eg. ctrl-j should be ignored for keymap.ini handling and
			// passed straight on
			if (!core->GetKeyMap()->ResolveKey(Key.keycode, mod<<20)) {
				core->GetGame()->SetHotKey(toupper(Key.character));
				return View::OnKeyPress(Key, mod);
			}
			break;
	}
	return true;
}

//Select (or deselect) a new actor (or actors)
void GameControl::SelectActor(int whom, int type)
{
	Game* game = core->GetGame();
	if (whom==-1) {
		game->SelectActor( NULL, true, SELECT_NORMAL );
		return;
	}

	/* doesn't fall through here */
	Actor* actor = game->FindPC( whom );
	if (!actor)
		return;

	if (type==0) {
		game->SelectActor( actor, false, SELECT_NORMAL );
		return;
	}
	if (type==1) {
		game->SelectActor( actor, true, SELECT_NORMAL );
		return;
	}

	bool was_selected = actor->IsSelected();
	if (game->SelectActor( actor, true, SELECT_REPLACE )) {
		if (was_selected || (ScreenFlags & SF_ALWAYSCENTER)) {
			ScreenFlags |= SF_CENTERONACTOR;
		}
	}
}

//Effect for the ctrl-r cheatkey (resurrect)
static EffectRef heal_ref = { "CurrentHPModifier", -1 };
static EffectRef damage_ref = { "Damage", -1 };

/** Key Release Event */
bool GameControl::OnKeyRelease(const KeyboardEvent& Key, unsigned short Mod)
{
	Point gameMousePos = GameMousePos();
	//cheatkeys with ctrl-
	if (Mod & GEM_MOD_CTRL) {
		if (!core->CheatEnabled()) {
			return false;
		}
		Game* game = core->GetGame();
		Map* area = game->GetCurrentArea( );
		if (!area)
			return false;
		Actor *lastActor = area->GetActorByGlobalID(lastActorID);
		switch (Key.keycode) {
			case 'a': //switches through the avatar animations
				if (lastActor) {
					lastActor->GetNextAnimation();
				}
				break;
			case 'b': //draw a path to the target (pathfinder debug)
				//You need to select an origin with ctrl-o first
				if (drawPath) {
					PathNode* nextNode = drawPath->Next;
					PathNode* thisNode = drawPath;
					while (true) {
						delete( thisNode );
						thisNode = nextNode;
						if (!thisNode)
							break;
						nextNode = thisNode->Next;
					}
				}
				drawPath = core->GetGame()->GetCurrentArea()->FindPath( pfs, gameMousePos, lastActor?lastActor->size:1 );
				break;
			case 'c': //force cast a hardcoded spell
				//caster is the last selected actor
				//target is the door/actor currently under the pointer
				if (game->selected.size() > 0) {
					Actor *src = game->selected[0];
					Scriptable *target = lastActor;
					if (overDoor) {
						target = overDoor;
					}
					if (target) {
						src->SetSpellResRef(TestSpell);
						src->CastSpell(target, false);
						if (src->LastSpellTarget) {
							src->CastSpellEnd(0, 0);
						} else {
							src->CastSpellPointEnd(0, 0);
						}
					}
				}
				break;
			case 'd': //detect a trap or door
				if (overInfoPoint) {
					overInfoPoint->DetectTrap(256, lastActorID);
				}
				if (overContainer) {
					overContainer->DetectTrap(256, lastActorID);
				}
				if (overDoor) {
					overDoor->TryDetectSecret(256, lastActorID);
					overDoor->DetectTrap(256, lastActorID);
				}
				break;
			case 'e':// reverses pc order (useful for parties bigger than 6)
				game->ReversePCs();
				break;
			// f
			case 'g'://shows loaded areas and other game information
				game->dump();
				break;
			// h
			case 'i'://interact trigger (from the original game)
				if (!lastActor) {
					lastActor = area->GetActor( gameMousePos, GA_DEFAULT);
				}
				if (lastActor && !(lastActor->GetStat(IE_MC_FLAGS)&MC_EXPORTABLE)) {
					int size = game->GetPartySize(true);
					if (size < 2 || game->NpcInParty < 2) break;
					for (int i = core->Roll(1, size, 0); i < 2*size; i++) {
						Actor *target = game->GetPC(i%size, true);
						if (target == lastActor) continue;
						if (target->GetStat(IE_MC_FLAGS) & MC_EXPORTABLE) continue; //not NPC
						lastActor->HandleInteractV1(target);
						break;
					}
				}
				break;
			case 'j': //teleports the selected actors
				for (size_t i = 0; i < game->selected.size(); i++) {
					Actor* actor = game->selected[i];
					actor->ClearActions();
					MoveBetweenAreasCore(actor, core->GetGame()->CurrentArea, gameMousePos, -1, true);
				}
				break;
			case 'k': //kicks out actor
				if (lastActor && lastActor->InParty) {
					lastActor->Stop();
					lastActor->AddAction( GenerateAction("LeaveParty()") );
				}
				break;
			case 'l': //play an animation (vvc/bam) over an actor
				//the original engine was able to swap through all animations
				if (lastActor) {
					lastActor->AddAnimation("S056ICBL", 0, 0, 0);
				}
				break;
			case 'M':
				if (!lastActor) {
					lastActor = area->GetActor( gameMousePos, GA_DEFAULT);
				}
				if (!lastActor) {
					// ValidTarget never returns immobile targets, making debugging a nightmare
					// so if we don't have an actor, we make really really sure by checking manually
					unsigned int count = area->GetActorCount(true);
					while (count--) {
						Actor *actor = area->GetActor(count, true);
						if (actor->IsOver(gameMousePos)) {
							actor->GetAnims()->DebugDump();
						}
					}
				}
				if (lastActor) {
					lastActor->GetAnims()->DebugDump();
					break;
				}
				break;
			case 'm': //prints a debug dump (ctrl-m in the original game too)
				if (!lastActor) {
					lastActor = area->GetActor( gameMousePos, GA_DEFAULT);
				}
				if (!lastActor) {
					// ValidTarget never returns immobile targets, making debugging a nightmare
					// so if we don't have an actor, we make really really sure by checking manually
					unsigned int count = area->GetActorCount(true);
					while (count--) {
						Actor *actor = area->GetActor(count, true);
						if (actor->IsOver(gameMousePos)) {
							actor->dump();
						}
					}
				}
				if (lastActor) {
					lastActor->dump();
					break;
				}
				if (overDoor) {
					overDoor->dump();
					break;
				}
				if (overContainer) {
					overContainer->dump();
					break;
				}
				if (overInfoPoint) {
					overInfoPoint->dump();
					break;
				}
				core->GetGame()->GetCurrentArea()->dump(false);
				break;
			case 'n': //prints a list of all the live actors in the area
				core->GetGame()->GetCurrentArea()->dump(true);
				break;
			case 'o': //set up the origin for the pathfinder
				// origin
				pfs = gameMousePos;
				break;
			case 'p': //center on actor
				ScreenFlags|=SF_CENTERONACTOR;
				ScreenFlags^=SF_ALWAYSCENTER;
				break;
			case 'q': //joins actor to the party
				if (lastActor && !lastActor->InParty) {
					lastActor->Stop();
					lastActor->AddAction( GenerateAction("JoinParty()") );
				}
				break;
			case 'r'://resurrects actor
				if (!lastActor) {
					lastActor = area->GetActor( gameMousePos, GA_DEFAULT);
				}
				if (lastActor) {
					Effect *fx = EffectQueue::CreateEffect(heal_ref, lastActor->GetStat(IE_MAXHITPOINTS), 0x30001, FX_DURATION_INSTANT_PERMANENT);
					if (fx) {
						core->ApplyEffect(fx, lastActor, lastActor);
					}
					delete fx;
				}
				break;
			case 's': //switches through the stance animations
				if (lastActor) {
					lastActor->GetNextStance();
				}
				break;
			case 't': // advances time by 1 hour
				game->AdvanceTime(core->Time.hour_size);
				//refresh gui here once we got it
				break;
			// u
			case 'V': //
				core->GetDictionary()->DebugDump();
				break;
			case 'v': //marks some of the map visited (random vision distance)
				area->ExploreMapChunk( gameMousePos, RAND(0,29), 1 );
				break;
			case 'w': // consolidates found ground piles under the pointed pc
				area->MoveVisibleGroundPiles(gameMousePos);
				break;
			case 'x': // shows coordinates on the map
				Log(MESSAGE, "GameControl", "Position: %s [%d.%d]", area->GetScriptName(), gameMousePos.x, gameMousePos.y );
				break;
			case 'Y': // damages all enemies by 300 (resistances apply)
				// mwahaha!
				{
				Effect *newfx = EffectQueue::CreateEffect(damage_ref, 300, DAMAGE_MAGIC<<16, FX_DURATION_INSTANT_PERMANENT);
				int i = area->GetActorCount(0);
				while(i--) {
					Actor *victim = area->GetActor(i, 0);
					if (victim->Modified[IE_EA] == EA_ENEMY) {
						core->ApplyEffect(newfx, victim, victim);
					}
				}
				delete newfx;
				}
				// fallthrough
			case 'y': //kills actor
				if (lastActor) {
					//using action so the actor is killed
					//correctly (synchronisation)
					lastActor->Stop();

					Effect *newfx;
					newfx = EffectQueue::CreateEffect(damage_ref, 300, DAMAGE_MAGIC<<16, FX_DURATION_INSTANT_PERMANENT);
					core->ApplyEffect(newfx, lastActor, lastActor);
					delete newfx;
					if (! (lastActor->GetInternalFlag() & IF_REALLYDIED)) {
						newfx = EffectQueue::CreateEffect(damage_ref, 300, DAMAGE_ACID<<16, FX_DURATION_INSTANT_PERMANENT);
						core->ApplyEffect(newfx, lastActor, lastActor);
						delete newfx;
						newfx = EffectQueue::CreateEffect(damage_ref, 300, DAMAGE_CRUSHING<<16, FX_DURATION_INSTANT_PERMANENT);
						core->ApplyEffect(newfx, lastActor, lastActor);
						delete newfx;
					}
				} else if (overContainer) {
					overContainer->SetContainerLocked(0);
				} else if (overDoor) {
					overDoor->SetDoorLocked(0,0);
				}
				break;
			case 'z': //shift through the avatar animations backward
				if (lastActor) {
					lastActor->GetPrevAnimation();
				}
				break;
			case '1': //change paperdoll armour level
				if (! lastActor)
					break;
				lastActor->NewStat(IE_ARMOR_TYPE,1,MOD_ADDITIVE);
				break;
			case '4': //show all traps and infopoints
				DebugFlags ^= DEBUG_SHOW_INFOPOINTS;
				Log(MESSAGE, "GameControl", "Show traps and infopoints %s", DebugFlags & DEBUG_SHOW_INFOPOINTS ? "ON" : "OFF");
				break;
			case '6': //show the lightmap
				DebugFlags ^= DEBUG_SHOW_LIGHTMAP;
				Log(MESSAGE, "GameControl", "Show lightmap %s", DebugFlags & DEBUG_SHOW_LIGHTMAP ? "ON" : "OFF");
				break;
			case '7': //toggles fog of war
				core->FogOfWar ^= FOG_DRAWFOG;
				Log(MESSAGE, "GameControl", "Show Fog-Of-War: %s", core->FogOfWar & FOG_DRAWFOG ? "ON" : "OFF");
				break;
			case '8': //show searchmap over area
				core->FogOfWar ^= FOG_DRAWSEARCHMAP;
				Log(MESSAGE, "GameControl", "Show searchmap %s", core->FogOfWar & FOG_DRAWSEARCHMAP ? "ON" : "OFF");
				break;
		}
		return true; //return from cheatkeys
	}
	switch (Key.keycode) {
//FIXME: move these to guiscript
		case ' ': //soft pause
			core->TogglePause();
			break;
		case GEM_ALT: //alt key (shows containers)
#ifdef ANDROID
		case 'c': // show containers in ANDROID, GEM_ALT is not possible to use
#endif
			DebugFlags &= ~DEBUG_SHOW_CONTAINERS;
			break;
		default:
			return false;
	}
	return true;
}

String GameControl::TooltipText() const {
	Map* area = CurrentArea();
	if (area == NULL) {
		return View::TooltipText();
	}
	Actor* actor = area->GetActor(GameMousePos(), GA_NO_DEAD|GA_NO_UNSCHEDULED);
	if (actor == NULL) {
		return View::TooltipText();
	}

	static String tip; // only one game control and we return a const& so cant be temporary.
	const char *name = actor->GetName(-1);
	// FIME: make the actor name a String instead
	String* wname = StringFromCString(name);
	if (wname) {
		tip = *wname;
	}

	int hp = actor->GetStat(IE_HITPOINTS);
	int maxhp = actor->GetStat(IE_MAXHITPOINTS);

	if (actor->InParty) {
		wchar_t hpstring[10];
		swprintf(hpstring, 10, L"%d/%d", hp, maxhp);
		if (core->HasFeature(GF_ONSCREEN_TEXT)) {
			tip += L": ";
		} else {
			tip += L"\n";
		}
		tip += hpstring;
	} else {
		// a guess at a neutral check
		bool enemy = actor->GetStat(IE_EA) != EA_NEUTRAL;
		// test for an injured string being present for this game
		int strindex = displaymsg->GetStringReference(STR_UNINJURED);
		if (enemy && strindex != -1) {
			// non-neutral, not in party: display injured string
			// these boundaries are just a guess
			if (hp == maxhp) {
				strindex = STR_UNINJURED;
			} else if (hp > (maxhp*3)/4) {
				strindex = STR_INJURED1;
			} else if (hp > maxhp/2) {
				strindex = STR_INJURED2;
			} else if (hp > maxhp/3) {
				strindex = STR_INJURED3;
			} else {
				strindex = STR_INJURED4;
			}
			strindex = displaymsg->GetStringReference(strindex);
			String* injuredstring = core->GetString(strindex, 0);
			assert(injuredstring); // we just "checked" for these (by checking for STR_UNINJURED)
			tip += L"\n" + *injuredstring;
			delete injuredstring;
		}
	}

	return tip;
}

//returns the appropriate cursor over an active region (trap, infopoint, travel region)
int GameControl::GetCursorOverInfoPoint(InfoPoint *overInfoPoint) const
{
	if (target_mode == TARGET_MODE_PICK) {
		if (overInfoPoint->VisibleTrap(0)) {
			return IE_CURSOR_TRAP;
		}

		return IE_CURSOR_STEALTH|IE_CURSOR_GRAY;
	}
	// traps always display a walk cursor?
	if (overInfoPoint->Type == ST_PROXIMITY) {
		return IE_CURSOR_WALK;
	}
	return overInfoPoint->Cursor;
}

//returns the appropriate cursor over a door
int GameControl::GetCursorOverDoor(Door *overDoor) const
{
	if (!overDoor->Visible()) {
		if (target_mode == TARGET_MODE_NONE) {
			// most secret doors are in walls, so default to the blocked cursor to not give them away
			// iwd ar6010 table/door/puzzle is walkable, secret and undetectable
			Game *game = core->GetGame();
			Map *area = game->GetCurrentArea();
			assert(area);
			return area->GetCursor(overDoor->Pos);
		} else {
			return lastCursor|IE_CURSOR_GRAY;
		}
	}
	if (target_mode == TARGET_MODE_PICK) {
		if (overDoor->VisibleTrap(0)) {
			return IE_CURSOR_TRAP;
		}
		if (overDoor->Flags & DOOR_LOCKED) {
			return IE_CURSOR_LOCK;
		}

		return IE_CURSOR_STEALTH|IE_CURSOR_GRAY;
	}
	return overDoor->Cursor;
}

//returns the appropriate cursor over a container (or pile)
int GameControl::GetCursorOverContainer(Container *overContainer) const
{
	if (overContainer->Flags & CONT_DISABLED) {
		return lastCursor;
	}

	if (target_mode == TARGET_MODE_PICK) {
		if (overContainer->VisibleTrap(0)) {
			return IE_CURSOR_TRAP;
		}
		if (overContainer->Flags & CONT_LOCKED) {
			return IE_CURSOR_LOCK2;
		}

		return IE_CURSOR_STEALTH|IE_CURSOR_GRAY;
	}
	return IE_CURSOR_TAKE;
}

Sprite2D* GameControl::GetTargetActionCursor() const
{
	int curIdx = IE_CURSOR_NORMAL;
	switch(target_mode) {
		case TARGET_MODE_TALK:
			curIdx = IE_CURSOR_TALK;
			break;
		case TARGET_MODE_ATTACK:
			curIdx = IE_CURSOR_ATTACK;
			break;
		case TARGET_MODE_CAST:
			curIdx = IE_CURSOR_CAST;
			break;
		case TARGET_MODE_DEFEND:
			curIdx = IE_CURSOR_DEFEND;
			break;
		case TARGET_MODE_PICK:
			curIdx = IE_CURSOR_PICK;
			break;
	}
	return core->Cursors[curIdx];
}

Sprite2D* GameControl::Cursor() const
{
	Sprite2D* cursor = View::Cursor();
	if (cursor == NULL && lastCursor != IE_CURSOR_INVALID) {
		int idx = lastCursor & ~IE_CURSOR_GRAY;
		if (EventMgr::MouseDown()) {
			++idx;
		}
		cursor = core->Cursors[idx];
	}
	return cursor;
}

/** Mouse Over Event */
bool GameControl::OnMouseOver(const MouseEvent& /*me*/)
{
	Map* area = CurrentArea();
	Actor *lastActor = area->GetActorByGlobalID(lastActorID);
	if (lastActor) {
		lastActor->SetOver( false );
	}

	Point gameMousePos = GameMousePos();
	// let us target party members even if they are invisible
	lastActor = area->GetActor(gameMousePos, GA_NO_DEAD|GA_NO_UNSCHEDULED);
	if (lastActor && lastActor->Modified[IE_EA]>=EA_CONTROLLED) {
		if (!lastActor->ValidTarget(target_types) || !area->IsVisible(gameMousePos, false)) {
			lastActor = NULL;
		}
	}

	if ((target_types & GA_NO_SELF) && lastActor ) {
		if (lastActor == core->GetFirstSelectedActor()) {
			lastActor=NULL;
		}
	}

	SetLastActor(lastActor);

	return true;
}

void GameControl::UpdateCursor()
{
	Map *area = CurrentArea();
	if (area == NULL) {
		lastCursor = IE_CURSOR_BLOCKED;
		return;
	}

	Point gameMousePos = GameMousePos();
	int nextCursor = area->GetCursor( gameMousePos );
	//make the invisible area really invisible
	if (nextCursor == IE_CURSOR_INVALID) {
		lastCursor = IE_CURSOR_BLOCKED;
		return;
	}

	overInfoPoint = area->TMap->GetInfoPoint( gameMousePos, true );
	if (overInfoPoint) {
		nextCursor = GetCursorOverInfoPoint(overInfoPoint);
	}
	// recheck in case the position was different, resulting in a new isVisible check
	if (nextCursor == IE_CURSOR_INVALID) {
		lastCursor = IE_CURSOR_BLOCKED;
		return;
	}

	if (overDoor) {
		overDoor->Highlight = false;
	}
	if (overContainer) {
		overContainer->Highlight = false;
	}

	overDoor = area->TMap->GetDoor( gameMousePos );
	overContainer = area->TMap->GetContainer( gameMousePos );

	if (overDoor) {
		nextCursor = GetCursorOverDoor(overDoor);
	}

	if (overContainer) {
		nextCursor = GetCursorOverContainer(overContainer);
	}
	// recheck in case the positioon was different, resulting in a new isVisible check
	// fixes bg2 long block door in ar0801 above vamp beds, crashing on mouseover (too big)
	if (nextCursor == IE_CURSOR_INVALID) {
		lastCursor = IE_CURSOR_BLOCKED;
		return;
	}

	Actor *lastActor = area->GetActorByGlobalID(lastActorID);
	if (lastActor) {
		ieDword type = lastActor->GetStat(IE_EA);
		if (type >= EA_EVILCUTOFF || type == EA_GOODBUTRED) {
			nextCursor = IE_CURSOR_ATTACK;
		} else if ( type > EA_CHARMED ) {
			nextCursor = IE_CURSOR_TALK;
			//don't let the pc to talk to frozen/stoned creatures
			ieDword state = lastActor->GetStat(IE_STATE_ID);
			if (state & (STATE_CANTMOVE^STATE_SLEEP)) {
				nextCursor |= IE_CURSOR_GRAY;
			}
		} else {
			nextCursor = IE_CURSOR_NORMAL;
		}
	}

	if (target_mode == TARGET_MODE_TALK) {
		nextCursor = IE_CURSOR_TALK;
		if (!lastActor) {
			nextCursor |= IE_CURSOR_GRAY;
		} else {
			//don't let the pc to talk to frozen/stoned creatures
			ieDword state = lastActor->GetStat(IE_STATE_ID);
			if (state & (STATE_CANTMOVE^STATE_SLEEP)) {
				nextCursor |= IE_CURSOR_GRAY;
			}
		}
	} else if (target_mode == TARGET_MODE_ATTACK) {
		nextCursor = IE_CURSOR_ATTACK;
		if (overDoor) {
			if (!overDoor->Visible()) {
				nextCursor |= IE_CURSOR_GRAY;
			}
		} else if (!lastActor && !overContainer) {
			nextCursor |= IE_CURSOR_GRAY;
		}
	} else if (target_mode == TARGET_MODE_CAST) {
		nextCursor = IE_CURSOR_CAST;
		//point is always valid
		if (!(target_types & GA_POINT)) {
			if(!lastActor) {
				nextCursor |= IE_CURSOR_GRAY;
			}
		}
	} else if (target_mode == TARGET_MODE_DEFEND) {
		nextCursor = IE_CURSOR_DEFEND;
		if(!lastActor) {
			nextCursor |= IE_CURSOR_GRAY;
		}
	} else if (target_mode == TARGET_MODE_PICK) {
		if (lastActor) {
			nextCursor = IE_CURSOR_PICK;
		} else {
			if (!overContainer && !overDoor && !overInfoPoint) {
				nextCursor = IE_CURSOR_STEALTH|IE_CURSOR_GRAY;
			}
		}
		goto end_function;
	}

end_function:
	if (nextCursor >= 0) {
		lastCursor = nextCursor ;
	}
}

bool GameControl::IsDisabledCursor() const
{
	bool isDisabled = View::IsDisabledCursor();
	if (lastCursor != IE_CURSOR_INVALID)
		isDisabled |= bool(lastCursor&IE_CURSOR_GRAY);

	return isDisabled;
}

bool GameControl::OnMouseDrag(const MouseEvent& me)
{
	if (target_mode != TARGET_MODE_NONE) {
		// we are in a target mode; nothing here applies
		return true;
	}

	if (overDoor || overContainer || overInfoPoint) {
		return true;
	}

	if (me.ButtonState(GEM_MB_ACTION)) {
		// PST uses alt + left click for formation rotation
		// is there any harm in this being true in all games?
		if (EventMgr::ModState(GEM_MOD_ALT)) {
			isFormationRotation = true;
		} else {
			isSelectionRect = true;
		}
	}

	if (me.ButtonState(GEM_MB_MENU)) {
		isFormationRotation = true;
	}

	if (core->GetGame()->selected.size() <= 1) {
		isFormationRotation = false;
	}

	if (isFormationRotation) {
		SetCursor(core->Cursors[IE_CURSOR_USE]);
	} else if (isSelectionRect) {
		SetCursor(core->Cursors[IE_CURSOR_PRESSED]);
	}
	return true;
}

bool GameControl::OnTouchDown(const TouchEvent& te, unsigned short mod)
{
	if (te.numFingers == 2) {
		// container highlights
		DebugFlags |= DEBUG_SHOW_CONTAINERS;
	}

	// TODO: check pressure to distinguish between tooltip and HP modes
	if (View::OnTouchDown(te, mod)) {
		if (te.numFingers == 1) {
			screenMousePos = te.Pos();

			// if an actor is being touched show HP
			Actor* actor = GetLastActor();
			if (actor) {
				actor->DisplayHeadHPRatio();
			}
		}
		return true;
	}
	return false;
}

bool GameControl::OnTouchUp(const TouchEvent& te, unsigned short mod)
{
	if (EventMgr::ModState(GEM_ALT) == false) {
		DebugFlags &= ~DEBUG_SHOW_CONTAINERS;
	}

	return View::OnTouchUp(te, mod);
}

bool GameControl::OnTouchGesture(const GestureEvent& gesture)
{
	if (gesture.numFingers == 1) {
		if (target_mode != TARGET_MODE_NONE) {
			// we are in a target mode; nothing here applies
			return true;
		}

		if (overDoor || overContainer || overInfoPoint) {
			return true;
		}

		screenMousePos = gesture.Pos();
		isSelectionRect = true;
	} else if (gesture.numFingers == 2) {
		if (gesture.dTheta < -0.2 || gesture.dTheta > 0.2) { // TODO: actually figure out a good number
			if (EventMgr::ModState(GEM_ALT) == false) {
				DebugFlags &= ~DEBUG_SHOW_CONTAINERS;
			}

			isSelectionRect = false;

			if (core->GetGame()->selected.size() <= 1) {
				isFormationRotation = false;
			} else {
				isFormationRotation = true;
				screenMousePos = gesture.fingers[1].Pos();
			}
		}
	} else if (gesture.numFingers == 3) {
		// keyboard/console

		Video* video = core->GetVideoDriver();

		enum SWIPE {DOWN = -1, NONE = 0, UP = 1};
		SWIPE swipe = NONE; // TODO: calculate this

		Window* consoleWin = GemRB::GetWindow(0, "WIN_CON");
		assert(consoleWin);

		switch (swipe) {
			case DOWN:
				consoleWin->Close();
				video->StopTextInput();
				break;
			case UP:
				if (video->InTextInput()) {
					consoleWin->Focus();
				} else {
					video->StartTextInput();
				}
				break;
			case NONE:
				break;
		}


		video->InTextInput();
	}
	return true;
}

Point GameControl::GameMousePos() const
{
	return vpOrigin + ConvertPointFromScreen(screenMousePos);
}

bool GameControl::OnGlobalMouseMove(const Event& e)
{
	// we are using the window->IsDisabled on purpose
	// to avoid bugs, we are disabling the window when we open one of the "top window"s
	// GC->IsDisabled is for other uses
	if (window->IsDisabled() || Flags()&IgnoreEvents) {
		return false;
	}
	
#define SCROLL_AREA_WIDTH 5
	Region mask = frame;
	mask.x += SCROLL_AREA_WIDTH;
	mask.y += SCROLL_AREA_WIDTH;
	mask.w -= SCROLL_AREA_WIDTH*2;
	mask.h -= SCROLL_AREA_WIDTH*2;
#undef SCROLL_AREA_WIDTH

	screenMousePos = e.mouse.Pos();
	Point mp = ConvertPointFromScreen(screenMousePos);
	int mousescrollspd = core->GetMouseScrollSpeed();

	if (mp.x < mask.x) {
		moveX = -mousescrollspd;
	} else if (mp.x > mask.x + mask.w) {
		moveX = mousescrollspd;
	} else {
		moveX = 0;
	}

	if (mp.y < mask.y) {
		moveY = -mousescrollspd;
	} else if (mp.y > mask.y + mask.h) {
		moveY = mousescrollspd;
	} else {
		moveY = 0;
	}
	return true;
}

void GameControl::MoveViewportTo(Point p, bool center, int speed)
{
	if (updateVPTimer && speed) {
		updateVPTimer = false;
		core->timer->SetMoveViewPort(p, speed, center);
	} else if (p != vpOrigin) {
		updateVPTimer = true;

		Map* area = CurrentArea();
		if (area == NULL) {
			return;
		}

		Size mapsize = area->GetSize();

		if (center) {
			p.x -= frame.w/2;
			p.y -= frame.h/2;
		}

		// TODO: make the overflow more dynamic
		if (p.x + frame.w >= mapsize.w + 64) {
			p.x = mapsize.w - frame.w + 64;
		}
		if (p.x < -64) {
			p.x = -64;
		}
		if (p.y + frame.h >= mapsize.h + 288) {
			p.y = mapsize.h - frame.h + 288;
		}
		if (p.y < 0) {
			p.y = 0;
		}

		core->GetAudioDrv()->UpdateListenerPos( p.x + frame.w / 2, p.y + frame.h / 2 );
		vpOrigin = p;
	} else {
		updateVPTimer = true;
	}
}

Region GameControl::Viewport()
{
	return Region(vpOrigin, frame.Dimensions());
}

//generate action code for source actor to try to attack a target
void GameControl::TryToAttack(Actor *source, Actor *tgt)
{
	if (source->GetStat(IE_SEX) == SEX_ILLUSION) return;
	source->CommandActor(GenerateActionDirect( "NIDSpecial3()", tgt));
}

//generate action code for source actor to try to defend a target
void GameControl::TryToDefend(Actor *source, Actor *tgt)
{
	source->SetModal(MS_NONE);
	source->CommandActor(GenerateActionDirect( "NIDSpecial4()", tgt));
}

// generate action code for source actor to try to pick pockets of a target (if an actor)
// else if door/container try to pick a lock/disable trap
// The -1 flag is a placeholder for dynamic target IDs
void GameControl::TryToPick(Actor *source, Scriptable *tgt)
{
	source->SetModal(MS_NONE);
	const char* cmdString = NULL;
	switch (tgt->Type) {
		case ST_ACTOR:
			cmdString = "PickPockets([-1])";
			break;
		case ST_DOOR:
		case ST_CONTAINER:
			if (((Highlightable*)tgt)->Trapped && ((Highlightable*)tgt)->TrapDetected) {
				cmdString = "RemoveTraps([-1])";
			} else {
				cmdString = "PickLock([-1])";
			}
			break;
		default:
			Log(ERROR, "GameControl", "Invalid pick target of type %d", tgt->Type);
			return;
	}
	source->CommandActor(GenerateActionDirect(cmdString, tgt));
}

//generate action code for source actor to try to disable trap (only trap type active regions)
void GameControl::TryToDisarm(Actor *source, InfoPoint *tgt)
{
	if (tgt->Type!=ST_PROXIMITY) return;

	source->SetModal(MS_NONE);
	source->CommandActor(GenerateActionDirect( "RemoveTraps([-1])", tgt ));
}

//generate action code for source actor to use item/cast spell on a point
void GameControl::TryToCast(Actor *source, const Point &tgt)
{
	char Tmp[40];

	if (!spellCount) {
		ResetTargetMode();
		return; //not casting or using an own item
	}
	source->Stop();

	spellCount--;
	if (spellOrItem>=0) {
		if (spellIndex<0) {
			strlcpy(Tmp, "SpellPointNoDec(\"\",[0.0])", sizeof(Tmp));
		} else {
			strlcpy(Tmp, "SpellPoint(\"\",[0.0])", sizeof(Tmp));
		}
	} else {
		//using item on target
		strlcpy(Tmp, "UseItemPoint(\"\",[0,0],0)", sizeof(Tmp));
	}
	Action* action = GenerateAction( Tmp );
	action->pointParameter=tgt;
	if (spellOrItem>=0) {
		if (spellIndex<0) {
			sprintf(action->string0Parameter,"%.8s",spellName);
		} else {
			CREMemorizedSpell *si;
			//spell casting at target
			si = source->spellbook.GetMemorizedSpell(spellOrItem, spellSlot, spellIndex);
			if (!si) {
				ResetTargetMode();
				delete action;
				return;
			}
			sprintf(action->string0Parameter,"%.8s",si->SpellResRef);
		}
	} else {
		action->int0Parameter = spellSlot;
		action->int1Parameter = spellIndex;
		action->int2Parameter = UI_SILENT;
                //for multi-shot items like BG wand of lightning
                if (spellCount)
                    action->int2Parameter |= UI_NOAURA|UI_NOCHARGE;
	}
	source->AddAction( action );
	if (!spellCount) {
		ResetTargetMode();
	}
}

//generate action code for source actor to use item/cast spell on another actor
void GameControl::TryToCast(Actor *source, Actor *tgt)
{
	char Tmp[40];

	if (!spellCount) {
		ResetTargetMode();
		return; //not casting or using an own item
	}
	source->Stop();

	// cannot target spells on invisible or sanctuaried creatures
	// invisible actors are invisible, so this is usually impossible by itself, but improved invisibility changes that
	if (source != tgt && tgt->Untargetable(spellName)) {
		displaymsg->DisplayConstantStringName(STR_NOSEE_NOCAST, DMC_RED, source);
		ResetTargetMode();
		return;
	}

	spellCount--;
	if (spellOrItem>=0) {
		if (spellIndex<0) {
			sprintf(Tmp, "NIDSpecial7()");
		} else {
			sprintf(Tmp, "NIDSpecial6()");
		}
	} else {
		//using item on target
		sprintf(Tmp, "NIDSpecial5()");
	}
	Action* action = GenerateActionDirect( Tmp, tgt);
	if (spellOrItem>=0) {
		if (spellIndex<0) {
			sprintf(action->string0Parameter,"%.8s",spellName);
		} else {
			CREMemorizedSpell *si;
			//spell casting at target
			si = source->spellbook.GetMemorizedSpell(spellOrItem, spellSlot, spellIndex);
			if (!si) {
				ResetTargetMode();
				delete action;
				return;
			}
			sprintf(action->string0Parameter,"%.8s",si->SpellResRef);
		}
	} else {
		action->int0Parameter = spellSlot;
		action->int1Parameter = spellIndex;
		action->int2Parameter = UI_SILENT;
                //for multi-shot items like BG wand of lightning
                if (spellCount)
                    action->int2Parameter |= UI_NOAURA|UI_NOCHARGE;
	}
	source->AddAction( action );
	if (!spellCount) {
		ResetTargetMode();
	}
}

//generate action code for source actor to use talk to target actor
void GameControl::TryToTalk(Actor *source, Actor *tgt)
{
	if (source->GetStat(IE_SEX) == SEX_ILLUSION) return;
	//Nidspecial1 is just an unused action existing in all games
	//(non interactive demo)
	//i found no fitting action which would emulate this kind of
	//dialog initation
	source->SetModal(MS_NONE);
	dialoghandler->SetTarget(tgt); //this is a hack, but not so deadly
	source->CommandActor(GenerateActionDirect( "NIDSpecial1()", tgt));
}

//generate action code for actor appropriate for the target mode when the target is a container
void GameControl::HandleContainer(Container *container, Actor *actor)
{
	if (actor->GetStat(IE_SEX) == SEX_ILLUSION) return;
	//container is disabled, it should not react
	if (container->Flags & CONT_DISABLED) {
		return;
	}

	if ((target_mode == TARGET_MODE_CAST) && spellCount) {
		//we'll get the container back from the coordinates
		TryToCast(actor, container->Pos);
		//Do not reset target_mode, TryToCast does it for us!!
		return;
	}

	core->SetEventFlag(EF_RESETTARGET);

	if (target_mode == TARGET_MODE_ATTACK) {
		char Tmp[256];
		snprintf(Tmp, sizeof(Tmp), "BashDoor(\"%s\")", container->GetScriptName());
		actor->CommandActor(GenerateAction(Tmp));
		return;
	}

	if (target_mode == TARGET_MODE_PICK) {
		TryToPick(actor, container);
		return;
	}

	container->AddTrigger(TriggerEntry(trigger_clicked, actor->GetGlobalID()));
	core->SetCurrentContainer( actor, container);
	actor->CommandActor(GenerateAction("UseContainer()"));
}

//generate action code for actor appropriate for the target mode when the target is a door
void GameControl::HandleDoor(Door *door, Actor *actor)
{
	if (actor->GetStat(IE_SEX) == SEX_ILLUSION) return;
	if ((target_mode == TARGET_MODE_CAST) && spellCount) {
		//we'll get the door back from the coordinates
		Point *p = door->toOpen;
		Point *otherp = door->toOpen+1;
		if (Distance(*p,actor)>Distance(*otherp,actor)) {
			p=otherp;
		}
		TryToCast(actor, *p);
		return;
	}

	core->SetEventFlag(EF_RESETTARGET);

	if (target_mode == TARGET_MODE_ATTACK) {
		char Tmp[256];
		snprintf(Tmp, sizeof(Tmp), "BashDoor(\"%s\")", door->GetScriptName());
		actor->CommandActor(GenerateAction(Tmp));
		return;
	}

	if (target_mode == TARGET_MODE_PICK) {
		TryToPick(actor, door);
		return;
	}

	door->AddTrigger(TriggerEntry(trigger_clicked, actor->GetGlobalID()));
	actor->TargetDoor = door->GetGlobalID();
	// internal gemrb toggle door action hack - should we use UseDoor instead?
	actor->CommandActor(GenerateAction("NIDSpecial9()"));
}

//generate action code for actor appropriate for the target mode when the target is an active region (infopoint, trap or travel)
bool GameControl::HandleActiveRegion(InfoPoint *trap, Actor * actor, const Point& p)
{
	if (actor->GetStat(IE_SEX) == SEX_ILLUSION) return false;
	if ((target_mode == TARGET_MODE_CAST) && spellCount) {
		//we'll get the active region from the coordinates (if needed)
		TryToCast(actor, p);
		//don't bother with this region further
		return true;
	}
	if (target_mode == TARGET_MODE_PICK) {
		TryToDisarm(actor, trap);
		return true;
	}

	switch(trap->Type) {
		case ST_TRAVEL:
			trap->AddTrigger(TriggerEntry(trigger_clicked, actor->GetGlobalID()));
			actor->LastMarked = trap->GetGlobalID();
			//clear the go closer flag
			trap->GetCurrentArea()->LastGoCloser = 0;
			return false;
		case ST_TRIGGER:
			// pst, eg. ar1500
			if (trap->GetDialog()[0]) {
				trap->AddAction(GenerateAction("Dialogue([PC])"));
				return true;
			}

			// always display overhead text; totsc's ar0511 library relies on it
			if (!trap->GetOverheadText().empty()) {
				if (!trap->OverheadTextIsDisplaying()) {
					trap->DisplayOverheadText(true);
					DisplayString( trap );
				}
			}
			//the importer shouldn't load the script
			//if it is unallowed anyway (though
			//deactivated scripts could be reactivated)
			//only the 'trapped' flag should be honoured
			//there. Here we have to check on the
			//reset trap and deactivated flags
			if (trap->Scripts[0]) {
				if (!(trap->Flags&TRAP_DEACTIVATED) ) {
					trap->AddTrigger(TriggerEntry(trigger_clicked, actor->GetGlobalID()));
					actor->LastMarked = trap->GetGlobalID();
					//directly feeding the event, even if there are actions in the queue
					//trap->Scripts[0]->Update();
					// FIXME
					trap->ExecuteScript(1);
					trap->ProcessActions();
				}
			}
			if (trap->GetUsePoint() ) {
				char Tmp[256];
				sprintf(Tmp, "TriggerWalkTo(\"%s\")", trap->GetScriptName());
				actor->CommandActor(GenerateAction(Tmp));
				return true;
			}
			return true;
		default:;
	}
	return false;
}

/** Mouse Button Down */
bool GameControl::OnMouseDown(const MouseEvent& me, unsigned short Mod)
{
	Point p = ConvertPointFromScreen(me.Pos());
	gameClickPoint = p + vpOrigin;

	switch(me.button) {
	case GEM_MB_MENU: //right click.
		if (core->HasFeature(GF_HAS_FLOAT_MENU) && !Mod) {
			core->GetGUIScriptEngine()->RunFunction( "GUICommon", "OpenFloatMenuWindow", false, p);
		}
		break;
	case GEM_MB_ACTION:
		isDoubleClick = me.repeats == 2;
		break;
	}
	return true;
}

/** Mouse Button Up */
bool GameControl::OnMouseUp(const MouseEvent& me, unsigned short Mod)
{
	//heh, i found no better place
	core->CloseCurrentContainer();

	Point p = ConvertPointFromScreen(me.Pos()) + vpOrigin;
	Game* game = core->GetGame();
	Map* area = game->GetCurrentArea();

	// right click
	if (me.button == GEM_MB_MENU) {
		if (!isFormationRotation) {
			if (!core->HasFeature(GF_HAS_FLOAT_MENU)) {
				SetTargetMode(TARGET_MODE_NONE);
			}
			// update the action bar
			core->SetEventFlag(EF_ACTION);
			ClearMouseState();
			return true;
		} else {
			p = gameClickPoint;
		}
	} else {
		// any other button behaves as left click (scrollwhell buttons are mosue wheel events now)
		if (isDoubleClick)
			MoveViewportTo(p, true);

		// handle actions
		// FIXME: is this the right place to do this? seems ok.
		if (target_mode == TARGET_MODE_NONE && lastActorID) {
			switch (lastCursor & ~IE_CURSOR_GRAY) {
				case IE_CURSOR_TALK:
					SetTargetMode(TARGET_MODE_TALK);
					break;
				case IE_CURSOR_ATTACK:
					SetTargetMode(TARGET_MODE_ATTACK);
					break;
				case IE_CURSOR_CAST:
					SetTargetMode(TARGET_MODE_CAST);
					break;
				case IE_CURSOR_DEFEND:
					SetTargetMode(TARGET_MODE_DEFEND);
					break;
				case IE_CURSOR_PICK:
					SetTargetMode(TARGET_MODE_PICK);
					break;
				default: break;
			}
		}

		if (target_mode != TARGET_MODE_NONE || overInfoPoint || overContainer || overDoor) {
			PerformSelectedAction(p);
			ClearMouseState();
			return true;
		}
		
		// handle selections
		Actor* targetActor = area->GetActor(p, target_types);
		if (isSelectionRect) {
			MakeSelection(SelectionRect(), Mod&GEM_MOD_SHIFT);
			ClearMouseState();
			return true;
		} else if (targetActor) {
			if (Mod & GEM_MOD_SHIFT) {
				game->SelectActor(targetActor, true, SELECT_NORMAL);
			} else {
				game->SelectActor(targetActor, true, SELECT_REPLACE);
			}
			ClearMouseState();
			return true;
		}
	}

	// handle movement/travel
	CommandSelectedMovement(p);
	ClearMouseState();
	return true;
}

void GameControl::PerformSelectedAction(const Point& p)
{
	// TODO: consolidate the 'over' members into a single Scriptable*
	// then we simply switch on its type

	Game* game = core->GetGame();
	Map* area = game->GetCurrentArea();
	Actor* targetActor = area->GetActor(p, target_types & ~GA_NO_HIDDEN);

	Actor* selectedActor = core->GetFirstSelectedPC(false);
	if (!selectedActor) {
		//this could be a non-PC
		selectedActor = game->selected[0];
	}

	//add a check if you don't want some random monster handle doors and such
	if (overDoor) {
		CommandSelectedMovement(p);
		HandleDoor(overDoor, selectedActor);
	} else if (overContainer) {
		CommandSelectedMovement(p);
		HandleContainer(overContainer, selectedActor);
	} else if (overInfoPoint) {
		if (overInfoPoint->Type==ST_TRAVEL) {
			CommandSelectedMovement(p);

			ieDword exitID = overInfoPoint->GetGlobalID();
			if (core->HasFeature(GF_TEAM_MOVEMENT)) {
				// pst forces everyone to travel (eg. ar0201 outside_portal)
				int i = game->GetPartySize(false);
				while(i--) {
					game->GetPC(i, false)->UseExit(exitID);
				}
			} else {
				size_t i = game->selected.size();
				while(i--) {
					game->selected[i]->UseExit(exitID);
				}
			}
		}
		if (HandleActiveRegion(overInfoPoint, selectedActor, p)) {
			core->SetEventFlag(EF_RESETTARGET);
		}
	} else if (targetActor) {
		PerformActionOn(targetActor);
	} else if (target_mode == TARGET_MODE_CAST) {
		//the player is using an item or spell on the ground
		TryToCast(selectedActor, p);
	}
}

void GameControl::CommandSelectedMovement(const Point& p)
{
	Game* game = core->GetGame();

	// construct a sorted party
	// TODO: this is still ugly, help?
	std::vector<Actor *> party;
	// first, from the actual party
	int max = game->GetPartySize(false);
	for(int idx = 1; idx<=max; idx++) {
		Actor *act = game->FindPC(idx);
		if(act->IsSelected()) {
			party.push_back(act);
		}
	}

	//summons etc
	for (size_t i = 0; i < game->selected.size(); i++) {
		Actor *act = game->selected[i];
		if (!act->InParty) {
			party.push_back(act);
		}
	}
	
	if (party.empty())
		return;

	//party formation movement
	Point src;
	Point move = p;
	if (isFormationRotation) {
		src = GameMousePos();
	} else {
		src = party[0]->Pos;
	}

	for(unsigned int i = 0; i < party.size(); i++) {
		Actor* actor = party[i];
		actor->Stop();

		if (i || party.size() > 1) {
			Map* map = actor->GetCurrentArea();
			move = GetFormationPoint(map, i, src, p);
		}
		CreateMovement(actor, move);
	}

	// handle travel
	//p is a searchmap travel region
	if (game->GetCurrentArea()->GetCursor(p) == IE_CURSOR_TRAVEL) {
		char Tmp[256];
		sprintf( Tmp, "NIDSpecial2()" );
		party[0]->AddAction( GenerateAction( Tmp) );
	}
}

bool GameControl::OnMouseWheelScroll(const Point& delta)
{
	// gc uses the opposite direction
	Point d = delta;
	d.x *= -1;
	d.y *= -1;
	Scroll(d);
	return true;
}

void GameControl::Scroll(const Point& amt)
{
	MoveViewportTo(vpOrigin + amt, false);
}

void GameControl::PerformActionOn(Actor *actor)
{
	Game* game = core->GetGame();
	unsigned int i;

	//determining the type of the clicked actor
	ieDword type;

	type = actor->GetStat(IE_EA);
	if ( type >= EA_EVILCUTOFF || type == EA_GOODBUTRED ) {
		type = ACT_ATTACK; //hostile
	} else if ( type > EA_CHARMED ) {
		type = ACT_TALK; //neutral
	} else {
		type = ACT_NONE; //party
	}

	if (target_mode == TARGET_MODE_ATTACK) {
		type = ACT_ATTACK;
	} else if (target_mode == TARGET_MODE_TALK) {
		type = ACT_TALK;
	} else if (target_mode == TARGET_MODE_CAST) {
		type = ACT_CAST;
	} else if (target_mode == TARGET_MODE_DEFEND) {
		type = ACT_DEFEND;
	} else if (target_mode == TARGET_MODE_PICK) {
		type = ACT_THIEVING;
	}

	if (type != ACT_NONE) {
		if(!actor->ValidTarget(target_types)) {
			return;
		}
	}

	//we shouldn't zero this for two reasons in case of spell or item
	//1. there could be multiple targets
	//2. the target mode is important
	if (!(target_mode == TARGET_MODE_CAST) || !spellCount) {
		ResetTargetMode();
	}

	switch (type) {
		case ACT_NONE: //none
			if (!actor->ValidTarget(GA_SELECT)) {
				return;
			}

			if (actor->InParty)
				SelectActor( actor->InParty );
			else if (actor->GetStat(IE_EA) <= EA_CHARMED) {
				/*let's select charmed/summoned creatures
				EA_CHARMED is the maximum value known atm*/
				core->GetGame()->SelectActor(actor, true, SELECT_REPLACE);
			}
			break;
		case ACT_TALK:
			if (!actor->ValidTarget(GA_TALK)) {
				return;
			}

			//talk (first selected talks)
			if (game->selected.size()) {
				//if we are in PST modify this to NO!
				Actor *source;
				if (core->HasFeature(GF_PROTAGONIST_TALKS) ) {
					source = game->GetPC(0, false); //protagonist
				} else {
					source = core->GetFirstSelectedPC(false);
				}
				// only party members can start conversations
				if (source) {
					TryToTalk(source, actor);
				}
			}
			break;
		case ACT_ATTACK:
			//all of them attacks the red circled actor
			for(i=0;i<game->selected.size();i++) {
				TryToAttack(game->selected[i], actor);
			}
			break;
		case ACT_CAST: //cast on target or use item on target
			if (game->selected.size()==1) {
				Actor *source;
				source = core->GetFirstSelectedActor();
				if(source) {
					TryToCast(source, actor);
				}
			}
			break;
		case ACT_DEFEND:
			for(i=0;i<game->selected.size();i++) {
				TryToDefend(game->selected[i], actor);
			}
			break;
		case ACT_THIEVING:
			if (game->selected.size()==1) {
				Actor *source;
				source = core->GetFirstSelectedActor();
				if(source) {
					TryToPick(source, actor);
				}
			}
			break;
	}
}

//sets target mode, and resets the cursor
void GameControl::SetTargetMode(int mode) {
	target_mode = mode;
}

void GameControl::ResetTargetMode() {
	target_types = GA_NO_DEAD|GA_NO_HIDDEN|GA_NO_UNSCHEDULED;
	SetTargetMode(TARGET_MODE_NONE);
}

void GameControl::UpdateTargetMode() {
	SetTargetMode(target_mode);
}

Region GameControl::SelectionRect() const
{
	if (isSelectionRect) {
		return Region::RegionFromPoints(GameMousePos(), gameClickPoint);
	}
	return Region();
}

void GameControl::MakeSelection(const Region &r, bool extend)
{
	Game* game = core->GetGame();
	Map* area = game->GetCurrentArea();

	Actor **ab;
	int count = area->GetActorsInRect(ab, r, GA_NO_DEAD|GA_NO_UNSCHEDULED|GA_SELECT|GA_NO_ENEMY|GA_NO_NEUTRAL);
	if (!extend && count > 0) {
		std::set<Actor*>::iterator it = highlighted.begin();
		for (; it != highlighted.end(); ++it) {
			Actor* act = *it;
			act->SetOver(false);
		}

		highlighted.clear();
		game->SelectActor( NULL, false, SELECT_NORMAL );
	}

	for (int i = 0; i < count; i++) {
		Actor* actor = ab[i];
		if (highlighted.insert( actor ).second == true) {
			actor->SetOver( true );
			game->SelectActor( actor, true, SELECT_NORMAL );
		}
	}
	free( ab );
}

void GameControl::SetCutSceneMode(bool active)
{
	if (active) {
		ScreenFlags |= SF_CUTSCENE;
		moveX = 0;
		moveY = 0;
	} else {
		ScreenFlags &= ~SF_CUTSCENE;
	}
	SetFlags(IgnoreEvents, (active || DialogueFlags&DF_IN_DIALOG) ? OP_OR : OP_NAND);
}

//Create an overhead text over a scriptable target
//Multiple texts are possible, as this code copies the text to a new object
void GameControl::DisplayString(Scriptable* target)
{
	Scriptable* scr = new Scriptable( ST_TRIGGER );
	scr->SetOverheadText(target->GetOverheadText());
	scr->Pos = target->Pos;

	// add as a "subtitle" to the main message window
	ieDword tmp = 0;
	core->GetDictionary()->Lookup("Duplicate Floating Text", tmp);
	if (tmp && !target->GetOverheadText().empty()) {
		// pass NULL target so pst does not display multiple
		displaymsg->DisplayString(target->GetOverheadText());
	}
}

/** changes displayed map to the currently selected PC */
void GameControl::ChangeMap(Actor *pc, bool forced)
{
	//swap in the area of the actor
	Game* game = core->GetGame();
	if (forced || (pc && stricmp( pc->Area, game->CurrentArea) != 0) ) {
		// disable so that drawing and events dispatched doesn't happen while there is not an area
		// we are single threaded, but game loading runs its own event loop which will cause events/drawing to come in
		SetDisabled(true);
		ClearMouseState();

		dialoghandler->EndDialog();
		overInfoPoint = NULL;
		overContainer = NULL;
		overDoor = NULL;
		/*this is loadmap, because we need the index, not the pointer*/
		char *areaname = game->CurrentArea;
		if (pc) {
			areaname = pc->Area;
		}
		game->GetMap( areaname, true );
		ScreenFlags|=SF_CENTERONACTOR;
		
		SetDisabled(false);
	}
	//center on first selected actor
	if (pc && (ScreenFlags&SF_CENTERONACTOR)) {
		MoveViewportTo( pc->Pos, true );
		ScreenFlags&=~SF_CENTERONACTOR;
	}
}

void GameControl::FlagsChanged(unsigned int /*oldflags*/)
{
	if (Flags()&IgnoreEvents) {
		ClearMouseState();
	}
}

bool GameControl::SetScreenFlags(unsigned int value, int mode)
{
	return SetBits(ScreenFlags, value, mode);
}

void GameControl::SetDialogueFlags(unsigned int value, int mode)
{
	SetBits(DialogueFlags, value, mode);
	SetFlags(IgnoreEvents, (DialogueFlags&DF_IN_DIALOG || ScreenFlags&SF_CUTSCENE) ? OP_OR : OP_NAND);
}

Map* GameControl::CurrentArea() const
{
	Game *game = core->GetGame();
	if (game) {
		return game->GetCurrentArea();
	}
	return NULL;
}

Actor *GameControl::GetLastActor()
{
	Actor* actor = NULL;
	Map* area = CurrentArea();
	if (area) {
		actor = area->GetActorByGlobalID(lastActorID);
	}
	return actor;
}

void GameControl::SetLastActor(Actor* lastActor)
{
	if (lastActorID) {
		Map* area = CurrentArea();
		if (area == NULL) {
			return;
		}

		Actor* current = area->GetActorByGlobalID(lastActorID);
		if (current)
			current->SetOver(false);
		lastActorID = 0;
	}

	if (lastActor) {
		lastActorID = lastActor->GetGlobalID();
		lastActor->SetOver(true);
	}
}

//Set up an item use which needs targeting
//Slot is an inventory slot
//header is the used item extended header
//u is the user
//target type is a bunch of GetActor flags that alter valid targets
//cnt is the number of different targets (usually 1)
void GameControl::SetupItemUse(int slot, int header, Actor *u, int targettype, int cnt)
{
	memset(spellName, 0, sizeof(ieResRef));
	spellOrItem = -1;
	spellUser = u;
	spellSlot = slot;
	spellIndex = header;
	//item use also uses the casting icon, this might be changed in some custom game?
	SetTargetMode(TARGET_MODE_CAST);
	target_types = targettype;
	spellCount = cnt;
}

//Set up spell casting which needs targeting
//type is the spell's type
//level is the caster level
//idx is the spell's number
//u is the caster
//target type is a bunch of GetActor flags that alter valid targets
//cnt is the number of different targets (usually 1)
void GameControl::SetupCasting(ieResRef spellname, int type, int level, int idx, Actor *u, int targettype, int cnt)
{
	memcpy(spellName, spellname, sizeof(ieResRef));
	spellOrItem = type;
	spellUser = u;
	spellSlot = level;
	spellIndex = idx;
	SetTargetMode(TARGET_MODE_CAST);
	target_types = targettype;
	spellCount = cnt;
}

void GameControl::SetDisplayText(String* text, unsigned int time)
{
	delete DisplayText;
	DisplayTextTime = time;
	DisplayText = text;
}

void GameControl::SetDisplayText(ieStrRef text, unsigned int time)
{
	SetDisplayText(core->GetString(displaymsg->GetStringReference(text), 0), time);
}

void GameControl::ToggleAlwaysRun()
{
	AlwaysRun = !AlwaysRun;
	core->GetDictionary()->SetAt("Always Run", AlwaysRun);
}

}
