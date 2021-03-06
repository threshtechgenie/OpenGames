/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// sv_phys.c

#include "quakedef.h"
#if !defined(CLIENTONLY) || defined(CSQC_DAT)

#include "pr_common.h"

/*


pushmove objects do not obey gravity, and do not interact with each other or trigger fields, but block normal movement and push normal objects when they move.

onground is set for toss objects when they come to a complete rest.  it is set for steping or walking objects

doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
corpses are SOLID_NOT and MOVETYPE_TOSS
crates are SOLID_BBOX and MOVETYPE_TOSS
walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY

solid_edge items only clip against bsp models.

*/

cvar_t	sv_maxvelocity = SCVAR("sv_maxvelocity","2000");

cvar_t	sv_gravity			 = SCVAR( "sv_gravity", "800");
cvar_t	sv_stopspeed		 = SCVAR( "sv_stopspeed", "100");
cvar_t	sv_maxspeed			 = SCVAR( "sv_maxspeed", "320");
cvar_t	sv_spectatormaxspeed = SCVAR( "sv_spectatormaxspeed", "500");
cvar_t	sv_accelerate		 = SCVAR( "sv_accelerate", "10");
cvar_t	sv_airaccelerate	 = SCVAR( "sv_airaccelerate", "0.7");
cvar_t	sv_wateraccelerate	 = SCVAR( "sv_wateraccelerate", "10");
cvar_t	sv_friction			 = SCVAR( "sv_friction", "4");
cvar_t	sv_waterfriction	 = SCVAR( "sv_waterfriction", "4");
cvar_t	sv_gameplayfix_noairborncorpse = SCVAR( "sv_gameplayfix_noairborncorpse", "0");
cvar_t	sv_gameplayfix_multiplethinks = CVARD( "sv_gameplayfix_multiplethinks", "1", "Enables multiple thinks per entity per frame so small nextthink times are accurate. QuakeWorld mods expect a value of 1.");
cvar_t	sv_sound_watersplash = CVAR( "sv_sound_watersplash", "misc/h2ohit1.wav");
cvar_t	sv_sound_land		 = CVAR( "sv_sound_land", "demon/dland2.wav");
cvar_t	sv_stepheight		 = CVARAFD("pm_stepheight", "",
					  "sv_stepheight", CVAR_SERVERINFO, "If empty, the value 18 will be used instead.");

cvar_t	pm_ktjump			 = SCVARF("pm_ktjump", "", CVAR_SERVERINFO);
cvar_t	pm_bunnyspeedcap	 = SCVARF("pm_bunnyspeedcap", "", CVAR_SERVERINFO);
cvar_t	pm_slidefix			 = SCVARF("pm_slidefix", "", CVAR_SERVERINFO);
cvar_t	pm_slidyslopes		 = SCVARF("pm_slidyslopes", "", CVAR_SERVERINFO);
cvar_t	pm_airstep			 = SCVARF("pm_airstep", "", CVAR_SERVERINFO);
cvar_t	pm_walljump			 = SCVARF("pm_walljump", "", CVAR_SERVERINFO);

#define cvargroup_serverphysics  "server physics variables"
void WPhys_Init(void)
{
    Cvar_Register (&sv_maxvelocity,                 cvargroup_serverphysics);
    Cvar_Register (&sv_gravity,                             cvargroup_serverphysics);
    Cvar_Register (&sv_stopspeed,                   cvargroup_serverphysics);
    Cvar_Register (&sv_maxspeed,                    cvargroup_serverphysics);
    Cvar_Register (&sv_spectatormaxspeed,   cvargroup_serverphysics);
    Cvar_Register (&sv_accelerate,                  cvargroup_serverphysics);
    Cvar_Register (&sv_airaccelerate,               cvargroup_serverphysics);
    Cvar_Register (&sv_wateraccelerate,             cvargroup_serverphysics);
    Cvar_Register (&sv_friction,                    cvargroup_serverphysics);
    Cvar_Register (&sv_waterfriction,               cvargroup_serverphysics);
    Cvar_Register (&sv_sound_watersplash,   cvargroup_serverphysics);
    Cvar_Register (&sv_sound_land,                  cvargroup_serverphysics);
    Cvar_Register (&sv_stepheight,                  cvargroup_serverphysics);

	Cvar_Register (&sv_gameplayfix_noairborncorpse, cvargroup_serverphysics);
	Cvar_Register (&sv_gameplayfix_multiplethinks,	cvargroup_serverphysics);
}

#define	MOVE_EPSILON	0.01

static void WPhys_Physics_Toss (world_t *w, wedict_t *ent);
const vec3_t standardgravity = {0, 0, -1};

// warning: �SV_CheckAllEnts� defined but not used
/*
================
SV_CheckAllEnts
================

static void SV_CheckAllEnts (void)
{
	int			e;
	edict_t		*check;

// see if any solid entities are inside the final position
	for (e=1 ; e<sv.world.num_edicts ; e++)
	{
		check = EDICT_NUM(svprogfuncs, e);
		if (check->isfree)
			continue;
		if (check->v->movetype == MOVETYPE_PUSH
		|| check->v->movetype == MOVETYPE_NONE
		|| check->v->movetype == MOVETYPE_FOLLOW
		|| check->v->movetype == MOVETYPE_NOCLIP
		|| check->v->movetype == MOVETYPE_ANGLENOCLIP)
			continue;

		if (World_TestEntityPosition (&sv.world, (wedict_t*)check))
			Con_Printf ("entity in invalid position\n");
	}
}
*/

/*
================
SV_CheckVelocity
================
*/
void WPhys_CheckVelocity (world_t *w, wedict_t *ent)
{
	int		i;

//
// bound velocity
//
	for (i=0 ; i<3 ; i++)
	{
		if (IS_NAN(ent->v->velocity[i]))
		{
			Con_Printf ("Got a NaN velocity on %s\n", PR_GetString(w->progs, ent->v->classname));
			ent->v->velocity[i] = 0;
		}
		if (IS_NAN(ent->v->origin[i]))
		{
			Con_Printf ("Got a NaN origin on %s\n", PR_GetString(w->progs, ent->v->classname));
			ent->v->origin[i] = 0;
		}
	}

	if (Length(ent->v->velocity) > sv_maxvelocity.value)
	{
//		Con_DPrintf("Slowing %s\n", PR_GetString(w->progs, ent->v->classname));
		VectorScale (ent->v->velocity, sv_maxvelocity.value/Length(ent->v->velocity), ent->v->velocity);
	}
}

/*
=============
SV_RunThink

Runs thinking code if time.  There is some play in the exact time the think
function will be called, because it is called before any movement is done
in a frame.  Not used for pushmove objects, because they must be exact.
Returns false if the entity removed itself.
=============
*/
qboolean WPhys_RunThink (world_t *w, wedict_t *ent)
{
	float	thinktime;

	if (!sv_gameplayfix_multiplethinks.ival)	//try and imitate nq as closeley as possible
	{
		thinktime = ent->v->nextthink;
		if (thinktime <= 0 || thinktime > w->physicstime + host_frametime)
			return true;

		if (thinktime < w->physicstime)
			thinktime = w->physicstime;	// don't let things stay in the past.
									// it is possible to start that way
									// by a trigger with a local time.
		ent->v->nextthink = 0;
		*w->g.time = thinktime;
		w->Event_Think(w, ent);
		return !ent->isfree;
	}

	do
	{
		thinktime = ent->v->nextthink;
		if (thinktime <= 0)
			return true;
		if (thinktime > w->physicstime + host_frametime)
			return true;

		if (thinktime < w->physicstime)
			thinktime = w->physicstime;	// don't let things stay in the past.
									// it is possible to start that way
									// by a trigger with a local time.
		ent->v->nextthink = 0;

		*w->g.time = thinktime;
		w->Event_Think(w, ent);

		if (ent->isfree)
			return false;

		if (ent->v->nextthink <= thinktime)	//hmm... infinate loop was possible here.. Quite a few non-QW mods do this.
			return true;
	} while (1);

	return true;
}

/*
==================
SV_Impact

Two entities have touched, so run their touch functions
==================
*/
static void WPhys_Impact (world_t *w, wedict_t *e1, wedict_t *e2)
{
	*w->g.time = w->physicstime;
	if (e1->v->touch && e1->v->solid != SOLID_NOT)
	{
		w->Event_Touch(w, e1, e2);
	}

	if (e2->v->touch && e2->v->solid != SOLID_NOT)
	{
		w->Event_Touch(w, e2, e1);
	}
}


/*
==================
ClipVelocity

Slide off of the impacting object
==================
*/
#define	STOP_EPSILON	0.1
//courtesy of darkplaces, it's just more efficient.
static void ClipVelocity (vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
	int i;
	float backoff;

	backoff = -DotProduct (in, normal) * overbounce;
	VectorMA(in, backoff, normal, out);

	for (i = 0;i < 3;i++)
		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
			out[i] = 0;
}



/*
============
SV_FlyMove

The basic solid body movement clip that slides along multiple planes
Returns the clipflags if the velocity was modified (hit something solid)
1 = floor
2 = wall / step
4 = dead stop
If steptrace is not NULL, the trace of any vertical wall hit will be stored
============
*/
#define	MAX_CLIP_PLANES	5
static int WPhys_FlyMove (world_t *w, wedict_t *ent, const vec3_t gravitydir, float time, trace_t *steptrace)
{
	int			bumpcount, numbumps;
	vec3_t		dir;
	float		d;
	int			numplanes;
	vec3_t		planes[MAX_CLIP_PLANES];
	vec3_t		primal_velocity, original_velocity, new_velocity;
	int			i, j;
	trace_t		trace;
	vec3_t		end;
	float		time_left;
	int			blocked;
	vec3_t diff;

	vec3_t startorg;

	numbumps = 4;

	blocked = 0;
	VectorCopy (ent->v->velocity, original_velocity);
	VectorCopy (ent->v->velocity, primal_velocity);
	numplanes = 0;

	time_left = time;

	VectorCopy (ent->v->origin, startorg);

	for (bumpcount=0 ; bumpcount<numbumps ; bumpcount++)
	{
		for (i=0 ; i<3 ; i++)
			end[i] = ent->v->origin[i] + time_left * ent->v->velocity[i];

		trace = World_Move (w, ent->v->origin, ent->v->mins, ent->v->maxs, end, false, (wedict_t*)ent);

		if (trace.startsolid)
		{	// entity is trapped in another solid
			VectorClear (ent->v->velocity);
			return 3;
		}

		if (trace.fraction > 0)
		{	// actually covered some distance
			VectorCopy (trace.endpos, ent->v->origin);
			VectorCopy (ent->v->velocity, original_velocity);
			numplanes = 0;
		}

		if (trace.fraction == 1)
			 break;		// moved the entire distance

		if (!trace.ent)
			Host_Error ("SV_FlyMove: !trace.ent");

		if (-DotProduct(gravitydir, trace.plane.normal) > 0.7)
		{
			blocked |= 1;		// floor
			if (((wedict_t *)trace.ent)->v->solid == SOLID_BSP)
			{
				ent->v->flags =	(int)ent->v->flags | FL_ONGROUND;
				ent->v->groundentity = EDICT_TO_PROG(w->progs, trace.ent);
			}
		}
		if (!DotProduct(gravitydir, trace.plane.normal))
		{
			blocked |= 2;		// step
			if (steptrace)
				*steptrace = trace;	// save for player extrafriction
		}

//
// run the impact function
//
		WPhys_Impact (w, ent, trace.ent);
		if (ent->isfree)
			break;		// removed by the impact function


		time_left -= time_left * trace.fraction;

	// cliped to another plane
		if (numplanes >= MAX_CLIP_PLANES)
		{	// this shouldn't really happen
			VectorClear (ent->v->velocity);
			if (steptrace)
				*steptrace = trace;	// save for player extrafriction
			return 3;
		}

		if (0)
		{
			ClipVelocity(ent->v->velocity, trace.plane.normal, ent->v->velocity, 1);
			break;
		}
		else
		{
			if (numplanes)
			{
				VectorSubtract(planes[0], trace.plane.normal, diff);
				if (Length(diff) < 0.01)
					continue;	//hit this plane already
			}

			VectorCopy (trace.plane.normal, planes[numplanes]);
			numplanes++;

	//
	// modify original_velocity so it parallels all of the clip planes
	//
			for (i=0 ; i<numplanes ; i++)
			{
				ClipVelocity (original_velocity, planes[i], new_velocity, 1);
				for (j=0 ; j<numplanes ; j++)
					if (j != i)
					{
						if (DotProduct (new_velocity, planes[j]) < 0)
							break;	// not ok
					}
				if (j == numplanes)
					break;
			}

			if (i != numplanes)
			{	// go along this plane
//				Con_Printf ("%5.1f %5.1f %5.1f   ",ent->v->velocity[0], ent->v->velocity[1], ent->v->velocity[2]);
				VectorCopy (new_velocity, ent->v->velocity);
//				Con_Printf ("%5.1f %5.1f %5.1f\n",ent->v->velocity[0], ent->v->velocity[1], ent->v->velocity[2]);
			}
			else
			{	// go along the crease
				if (numplanes != 2)
				{
//					Con_Printf ("clip velocity, numplanes == %i\n",numplanes);
//					Con_Printf ("%5.1f %5.1f %5.1f   ",ent->v->velocity[0], ent->v->velocity[1], ent->v->velocity[2]);
					VectorClear (ent->v->velocity);
//					Con_Printf ("%5.1f %5.1f %5.1f\n",ent->v->velocity[0], ent->v->velocity[1], ent->v->velocity[2]);
					return 7;
				}
//				Con_Printf ("%5.1f %5.1f %5.1f   ",ent->v->velocity[0], ent->v->velocity[1], ent->v->velocity[2]);
				CrossProduct (planes[0], planes[1], dir);
				VectorNormalize(dir);	//fixes slow falling in corners
				d = DotProduct (dir, ent->v->velocity);
				VectorScale (dir, d, ent->v->velocity);
//				Con_Printf ("%5.1f %5.1f %5.1f\n",ent->v->velocity[0], ent->v->velocity[1], ent->v->velocity[2]);
			}
		}

//
// if original velocity is against the original velocity, stop dead
// to avoid tiny occilations in sloping corners
//
		if (DotProduct (ent->v->velocity, primal_velocity) <= 0)
		{
			VectorClear (ent->v->velocity);
			return blocked;
		}
	}

	return blocked;
}


/*
============
SV_AddGravity

============
*/
static void WPhys_AddGravity (world_t *w, wedict_t *ent, const float *gravitydir, float scale)
{
	if (!scale)
		scale = w->defaultgravityscale;

	VectorMA(ent->v->velocity, scale * movevars.gravity * host_frametime, gravitydir, ent->v->velocity);
}

/*
===============================================================================

PUSHMOVE

===============================================================================
*/

/*
============
SV_PushEntity

Does not change the entities velocity at all
============
*/
static trace_t WPhys_PushEntity (world_t *w, wedict_t *ent, vec3_t push, unsigned int traceflags)
{
	trace_t	trace;
	vec3_t	end;

	VectorAdd (ent->v->origin, push, end);

	if ((int)ent->v->flags&FLQW_LAGGEDMOVE)
		traceflags |= MOVE_LAGGED;

	if (ent->v->movetype == MOVETYPE_FLYMISSILE)
		trace = World_Move (w, ent->v->origin, ent->v->mins, ent->v->maxs, end, MOVE_MISSILE|traceflags, (wedict_t*)ent);
	else if (ent->v->solid == SOLID_TRIGGER || ent->v->solid == SOLID_NOT)
	// only clip against bmodels
		trace = World_Move (w, ent->v->origin, ent->v->mins, ent->v->maxs, end, MOVE_NOMONSTERS|traceflags, (wedict_t*)ent);
	else
		trace = World_Move (w, ent->v->origin, ent->v->mins, ent->v->maxs, end, MOVE_NORMAL|traceflags, (wedict_t*)ent);

	/*hexen2's movetype_swim does not allow swimming entities to move out of water. this implementation is quite hacky, but matches hexen2 well enough*/
	if (ent->v->movetype == MOVETYPE_H2SWIM)
	{
		if (!(w->worldmodel->funcs.PointContents(w->worldmodel, NULL, trace.endpos) & (FTECONTENTS_WATER|FTECONTENTS_SLIME|FTECONTENTS_LAVA)))
		{
			VectorCopy(ent->v->origin, trace.endpos);
			trace.fraction = 0;
			trace.ent = w->edicts;
		}
	}

//	if (trace.ent)
//		VectorMA(trace.endpos, sv_impactpush.value, trace.plane.normal, ent->v->origin);
//	else
		VectorCopy (trace.endpos, ent->v->origin);
	World_LinkEdict (w, ent, true);

	if (trace.ent)
		WPhys_Impact (w, ent, trace.ent);

	return trace;
}




typedef struct
{
	wedict_t	*ent;
	vec3_t	origin;
	vec3_t	angles;
//	float	deltayaw;
} pushed_t;
static pushed_t	pushed[MAX_EDICTS], *pushed_p;

/*
============
SV_Push

Objects need to be moved back on a failed push,
otherwise riders would continue to slide.
============
*/
static qboolean WPhys_PushAngles (world_t *w, wedict_t *pusher, vec3_t move, vec3_t amove)
{
	int			i, e;
	wedict_t	*check, *block;
	vec3_t		mins, maxs;
	//float oldsolid;
	pushed_t	*p;
	vec3_t		org, org2, move2, forward, right, up;

	pushed_p = pushed;

	// find the bounding box
	for (i=0 ; i<3 ; i++)
	{
		mins[i] = pusher->v->absmin[i] + move[i];
		maxs[i] = pusher->v->absmax[i] + move[i];
	}

// we need this for pushing things later
	VectorNegate (amove, org);
	AngleVectors (org, forward, right, up);

// save the pusher's original position
	pushed_p->ent = pusher;
	VectorCopy (pusher->v->origin, pushed_p->origin);
	VectorCopy (pusher->v->angles, pushed_p->angles);
	pushed_p++;

// move the pusher to it's final position
	VectorAdd (pusher->v->origin, move, pusher->v->origin);
	VectorAdd (pusher->v->angles, amove, pusher->v->angles);
	World_LinkEdict (w, pusher, false);

// see if any solid entities are inside the final position
	for (e = 1; e < w->num_edicts; e++)
	{
		check = WEDICT_NUM(w->progs, e);
		if (check->isfree)
			continue;

		if (check->v->movetype == MOVETYPE_PUSH
		|| check->v->movetype == MOVETYPE_NONE
		|| check->v->movetype == MOVETYPE_NOCLIP
		|| check->v->movetype == MOVETYPE_ANGLENOCLIP)
			continue;
/*
		oldsolid = pusher->v->solid;
		pusher->v->solid = SOLID_NOT;
		block = World_TestEntityPosition (w, check);
		pusher->v->solid = oldsolid;
		if (block)
			continue;
*/
	// if the entity is standing on the pusher, it will definitely be moved
		if ( ! ( ((int)check->v->flags & FL_ONGROUND)
			&& PROG_TO_WEDICT(w->progs, check->v->groundentity) == pusher) )
		{
			// see if the ent needs to be tested
			if ( check->v->absmin[0] >= maxs[0]
			|| check->v->absmin[1] >= maxs[1]
			|| check->v->absmin[2] >= maxs[2]
			|| check->v->absmax[0] <= mins[0]
			|| check->v->absmax[1] <= mins[1]
			|| check->v->absmax[2] <= mins[2] )
				continue;


			// see if the ent's bbox is inside the pusher's final position
			if (!World_TestEntityPosition (w, (wedict_t*)check))
				continue;
		}

		if ((pusher->v->movetype == MOVETYPE_PUSH) || (PROG_TO_WEDICT(w->progs, check->v->groundentity) == pusher))
		{
			// move this entity
			pushed_p->ent = check;
			VectorCopy (check->v->origin, pushed_p->origin);
			VectorCopy (check->v->angles, pushed_p->angles);
			pushed_p++;

			// try moving the contacted entity
			VectorAdd (check->v->origin, move, check->v->origin);
			VectorAdd (check->v->angles, amove, check->v->angles);

			// figure movement due to the pusher's amove
			VectorSubtract (check->v->origin, pusher->v->origin, org);
			org2[0] = DotProduct (org, forward);
			org2[1] = -DotProduct (org, right);
			org2[2] = DotProduct (org, up);
			VectorSubtract (org2, org, move2);
			VectorAdd (check->v->origin, move2, check->v->origin);

			check->v->flags = (int)check->v->flags & ~FL_ONGROUND;

			// may have pushed them off an edge
			if (PROG_TO_WEDICT(w->progs, check->v->groundentity) != pusher)
				check->v->groundentity = 0;

			block = World_TestEntityPosition (w, check);
			if (!block)
			{	// pushed ok
				World_LinkEdict (w, check, false);
				// impact?
				continue;
			}



			// if it is ok to leave in the old position, do it
			// this is only relevent for riding entities, not pushed
			// FIXME: this doesn't acount for rotation
			VectorSubtract (check->v->origin, move, check->v->origin);
			block = World_TestEntityPosition (w, check);
			if (!block)
			{
				pushed_p--;
				continue;
			}
		}

		// if it is sitting on top. Do not block.
		if (check->v->mins[0] == check->v->maxs[0])
		{
			World_LinkEdict (w, check, false);
			continue;
		}

//		Con_Printf("Pusher hit %s\n", PR_GetString(w->progs, check->v->classname));
		if (pusher->v->blocked)
		{
			*w->g.self = EDICT_TO_PROG(w->progs, pusher);
			*w->g.other = EDICT_TO_PROG(w->progs, check);
#ifdef VM_Q1
			if (w==&sv.world && svs.gametype == GT_Q1QVM)
				Q1QVM_Blocked();
			else
#endif
				PR_ExecuteProgram (w->progs, pusher->v->blocked);
		}

		// move back any entities we already moved
		// go backwards, so if the same entity was pushed
		// twice, it goes back to the original position
		for (p=pushed_p-1 ; p>=pushed ; p--)
		{
			VectorCopy (p->origin, p->ent->v->origin);
			VectorCopy (p->angles, p->ent->v->angles);
			World_LinkEdict (w, p->ent, false);
		}
		return false;
	}

//FIXME: is there a better way to handle this?
	// see if anything we moved has touched a trigger
	for (p=pushed_p-1 ; p>=pushed ; p--)
		World_TouchLinks (w, p->ent, w->areanodes);

	return true;
}

/*
============
SV_Push

============
*/
static qboolean WPhys_Push (world_t *w, wedict_t *pusher, vec3_t move, vec3_t amove)
{
#define PUSHABLE_LIMIT 32768
	int			i, e;
	wedict_t	*check, *block;
	vec3_t		mins, maxs;
	vec3_t		pushorig;
	int			num_moved;
	wedict_t	*moved_edict[PUSHABLE_LIMIT];
	vec3_t		moved_from[PUSHABLE_LIMIT];
	float oldsolid;

	if (amove[0] || amove[1] || amove[2])
	{
		return WPhys_PushAngles(w, pusher, move, amove);
	}

	for (i=0 ; i<3 ; i++)
	{
		mins[i] = pusher->v->absmin[i] + move[i];
		maxs[i] = pusher->v->absmax[i] + move[i];
	}

	VectorCopy (pusher->v->origin, pushorig);

// move the pusher to it's final position

	VectorAdd (pusher->v->origin, move, pusher->v->origin);
	World_LinkEdict (w, pusher, false);

// see if any solid entities are inside the final position
	num_moved = 0;
	for (e=1 ; e<w->num_edicts ; e++)
	{
		check = WEDICT_NUM(w->progs, e);
		if (check->isfree)
			continue;
		if (check->v->movetype == MOVETYPE_PUSH
		|| check->v->movetype == MOVETYPE_NONE
		|| check->v->movetype == MOVETYPE_FOLLOW
		|| check->v->movetype == MOVETYPE_NOCLIP
		|| check->v->movetype == MOVETYPE_ANGLENOCLIP)
			continue;

	// if the entity is standing on the pusher, it will definately be moved
		if ( ! ( ((int)check->v->flags & FL_ONGROUND)
		&&
			PROG_TO_WEDICT(w->progs, check->v->groundentity) == pusher) )
		{
			if ( check->v->absmin[0] >= maxs[0]
			|| check->v->absmin[1] >= maxs[1]
			|| check->v->absmin[2] >= maxs[2]
			|| check->v->absmax[0] <= mins[0]
			|| check->v->absmax[1] <= mins[1]
			|| check->v->absmax[2] <= mins[2] )
				continue;

		// see if the ent's bbox is inside the pusher's final position
			if (!World_TestEntityPosition (w, check))
				continue;
		}

		oldsolid = pusher->v->solid;
		pusher->v->solid = SOLID_NOT;
		block = World_TestEntityPosition (w, check);
		pusher->v->solid = oldsolid;
		if (block)
			continue;

		if (num_moved == PUSHABLE_LIMIT)
			break;

		VectorCopy (check->v->origin, moved_from[num_moved]);
		moved_edict[num_moved] = check;
		num_moved++;

//		check->v->flags = (int)check->v->flags & ~FL_ONGROUND;

		// try moving the contacted entity
		VectorAdd (check->v->origin, move, check->v->origin);
		block = World_TestEntityPosition (w, check);
		if (!block)
		{	// pushed ok
			World_LinkEdict (w, check, false);
			continue;
		}

		// if it is ok to leave in the old position, do it
		VectorSubtract (check->v->origin, move, check->v->origin);
		block = World_TestEntityPosition (w, check);
		if (!block)
		{
			//if leaving it where it was, allow it to drop to the floor again (useful for plats that move downward)
			check->v->flags = (int)check->v->flags & ~FL_ONGROUND;

			num_moved--;
			continue;
		}

	// if it is still inside the pusher, block
		if (check->v->mins[0] == check->v->maxs[0])
		{
			World_LinkEdict (w, check, false);
			continue;
		}
		if (check->v->solid == SOLID_NOT || check->v->solid == SOLID_TRIGGER)
		{	// corpse
			check->v->mins[0] = check->v->mins[1] = 0;
			VectorCopy (check->v->mins, check->v->maxs);
			World_LinkEdict (w, check, false);
			continue;
		}

		VectorCopy (pushorig, pusher->v->origin);
		World_LinkEdict (w, pusher, false);

		// if the pusher has a "blocked" function, call it
		// otherwise, just stay in place until the obstacle is gone
		if (pusher->v->blocked)
		{
			*w->g.self = EDICT_TO_PROG(w->progs, pusher);
			*w->g.other = EDICT_TO_PROG(w->progs, check);
#ifdef VM_Q1
			if (w==&sv.world && svs.gametype == GT_Q1QVM)
				Q1QVM_Blocked();
			else
#endif
				PR_ExecuteProgram (w->progs, pusher->v->blocked);
		}

	// move back any entities we already moved
		for (i=0 ; i<num_moved ; i++)
		{
			VectorCopy (moved_from[i], moved_edict[i]->v->origin);
			World_LinkEdict (w, moved_edict[i], false);
		}
		return false;
	}

	return true;
}


/*
============
SV_PushMove

============
*/
static void WPhys_PushMove (world_t *w, wedict_t *pusher, float movetime)
{
	int			i;
	vec3_t		move;
	vec3_t		amove;

	if (!pusher->v->velocity[0] && !pusher->v->velocity[1] && !pusher->v->velocity[2]
		&& !pusher->v->avelocity[0] && !pusher->v->avelocity[1] && !pusher->v->avelocity[2])
	{
		pusher->v->ltime += movetime;
		return;
	}

	for (i=0 ; i<3 ; i++)
	{
		move[i] = pusher->v->velocity[i] * movetime;
		amove[i] = pusher->v->avelocity[i] * movetime;
	}

	if (WPhys_Push (w, pusher, move, amove))
		pusher->v->ltime += movetime;
}


/*
================
SV_Physics_Pusher

================
*/
static void WPhys_Physics_Pusher (world_t *w, wedict_t *ent)
{
	float	thinktime;
	float	oldltime;
	float	movetime;
vec3_t oldorg, move;
vec3_t oldang, amove;
float	l;

	oldltime = ent->v->ltime;

	thinktime = ent->v->nextthink;
	if (thinktime < ent->v->ltime + host_frametime)
	{
		movetime = thinktime - ent->v->ltime;
		if (movetime < 0)
			movetime = 0;
	}
	else
		movetime = host_frametime;

	if (movetime)
	{
		WPhys_PushMove (w, ent, movetime);	// advances ent->v->ltime if not blocked
	}

	if (thinktime > oldltime && thinktime <= ent->v->ltime)
	{
VectorCopy (ent->v->origin, oldorg);
VectorCopy (ent->v->angles, oldang);
		ent->v->nextthink = 0;
#if 1
		*w->g.time = w->physicstime;
		w->Event_Think(w, ent);
#else
		pr_global_struct->time = sv.world.physicstime;
		pr_global_struct->self = EDICT_TO_PROG(w->progs, ent);
		pr_global_struct->other = EDICT_TO_PROG(w->progs, w->edicts);
#ifdef VM_Q1
		if (svs.gametype == GT_Q1QVM)
			Q1QVM_Think();
		else
#endif
			PR_ExecuteProgram (svprogfuncs, ent->v->think);
#endif
		if (ent->isfree)
			return;
VectorSubtract (ent->v->origin, oldorg, move);
VectorSubtract (ent->v->angles, oldang, amove);

l = Length(move)+Length(amove);
if (l > 1.0/64)
{
//	Con_Printf ("**** snap: %f\n", Length (l));
	VectorCopy (oldorg, ent->v->origin);
	VectorCopy (oldang, ent->v->angles);
	WPhys_Push (w, ent, move, amove);
}

	}

}


/*
=============
SV_Physics_Follow

Entities that are "stuck" to another entity
=============
*/
static void WPhys_Physics_Follow (world_t *w, wedict_t *ent)
{
	vec3_t vf, vr, vu, angles, v;
	wedict_t *e;

	// regular thinking
	if (!WPhys_RunThink (w, ent))
		return;

	// LordHavoc: implemented rotation on MOVETYPE_FOLLOW objects
	e = PROG_TO_WEDICT(w->progs, ent->v->aiment);
	if (e->v->angles[0] == ent->xv->punchangle[0] && e->v->angles[1] == ent->xv->punchangle[1] && e->v->angles[2] == ent->xv->punchangle[2])
	{
		// quick case for no rotation
		VectorAdd(e->v->origin, ent->v->view_ofs, ent->v->origin);
	}
	else
	{
		angles[0] = -ent->xv->punchangle[0];
		angles[1] =  ent->xv->punchangle[1];
		angles[2] =  ent->xv->punchangle[2];
		AngleVectors (angles, vf, vr, vu);
		v[0] = ent->v->view_ofs[0] * vf[0] + ent->v->view_ofs[1] * vr[0] + ent->v->view_ofs[2] * vu[0];
		v[1] = ent->v->view_ofs[0] * vf[1] + ent->v->view_ofs[1] * vr[1] + ent->v->view_ofs[2] * vu[1];
		v[2] = ent->v->view_ofs[0] * vf[2] + ent->v->view_ofs[1] * vr[2] + ent->v->view_ofs[2] * vu[2];
		angles[0] = -e->v->angles[0];
		angles[1] =  e->v->angles[1];
		angles[2] =  e->v->angles[2];
		AngleVectors (angles, vf, vr, vu);
		ent->v->origin[0] = v[0] * vf[0] + v[1] * vf[1] + v[2] * vf[2] + e->v->origin[0];
		ent->v->origin[1] = v[0] * vr[0] + v[1] * vr[1] + v[2] * vr[2] + e->v->origin[1];
		ent->v->origin[2] = v[0] * vu[0] + v[1] * vu[1] + v[2] * vu[2] + e->v->origin[2];
	}
	VectorAdd (e->v->angles, ent->v->v_angle, ent->v->angles);
	World_LinkEdict (w, ent, true);
}

/*
=============
SV_Physics_Noclip

A moving object that doesn't obey physics
=============
*/
static void WPhys_Physics_Noclip (world_t *w, wedict_t *ent)
{
// regular thinking
	if (!WPhys_RunThink (w, ent))
		return;

	VectorMA (ent->v->angles, host_frametime, ent->v->avelocity, ent->v->angles);
	VectorMA (ent->v->origin, host_frametime, ent->v->velocity, ent->v->origin);

	World_LinkEdict (w, (wedict_t*)ent, false);
}

/*
==============================================================================

TOSS / BOUNCE

==============================================================================
*/

/*
=============
SV_CheckWaterTransition

=============
*/
static void WPhys_CheckWaterTransition (world_t *w, wedict_t *ent)
{
	int		cont;

	cont = World_PointContents (w, ent->v->origin);

	//needs to be q1 progs compatible
	if (cont & FTECONTENTS_LAVA)
		cont = Q1CONTENTS_LAVA;
	else if (cont & FTECONTENTS_SLIME)
		cont = Q1CONTENTS_SLIME;
	else if (cont & FTECONTENTS_WATER)
		cont = Q1CONTENTS_WATER;
	else
		cont = Q1CONTENTS_EMPTY;

	if (!ent->v->watertype)
	{	// just spawned here
		ent->v->watertype = cont;
		ent->v->waterlevel = 1;
		return;
	}

	if (ent->v->watertype != cont && w->Event_ContentsTransition(w, ent, ent->v->watertype, cont))
	{
		ent->v->watertype = cont;
		ent->v->waterlevel = 1;
	}

	else if (cont <= Q1CONTENTS_WATER)
	{
		if (ent->v->watertype == Q1CONTENTS_EMPTY && *sv_sound_watersplash.string)
		{	// just crossed into water
			w->Event_Sound(NULL, ent, 0, sv_sound_watersplash.string, 255, 1, 0);
		}
		ent->v->watertype = cont;
		ent->v->waterlevel = 1;
	}
	else
	{
		if (ent->v->watertype != Q1CONTENTS_EMPTY && *sv_sound_watersplash.string)
		{	// just crossed into open
			w->Event_Sound(NULL, ent, 0, sv_sound_watersplash.string, 255, 1, 0);
		}
		ent->v->watertype = Q1CONTENTS_EMPTY;
		ent->v->waterlevel = cont;
	}
}

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
static void WPhys_Physics_Toss (world_t *w, wedict_t *ent)
{
	trace_t	trace;
	vec3_t	move;
	float	backoff;

	vec3_t temporg;
	int fl;
	const float *gravitydir;

	WPhys_CheckVelocity (w, ent);

// regular thinking
	if (!WPhys_RunThink (w, ent))
		return;

	if (ent->xv->gravitydir[2] || ent->xv->gravitydir[1] || ent->xv->gravitydir[0])
		gravitydir = ent->xv->gravitydir;
	else
		gravitydir = standardgravity;

// if onground, return without moving
	if ( ((int)ent->v->flags & FL_ONGROUND) )
	{
		if (-DotProduct(gravitydir, ent->v->velocity) >= (1.0f/32.0f))
			ent->v->flags = (int)ent->v->flags & ~FL_ONGROUND;
		else
		{
			if (sv_gameplayfix_noairborncorpse.value)
			{
				wedict_t *onent;
				onent = PROG_TO_WEDICT(w->progs, ent->v->groundentity);
				if (!onent->isfree)
					return;	//don't drop if our fround is still valid
			}
			else
				return;	//don't drop, even if the item we were on was removed (certain dm maps do this for q3 style stuff).
		}
	}

// add gravity
	if (ent->v->movetype != MOVETYPE_FLY
		&& ent->v->movetype != MOVETYPE_FLYMISSILE
		&& ent->v->movetype != MOVETYPE_BOUNCEMISSILE
		&& ent->v->movetype != MOVETYPE_H2SWIM)
		WPhys_AddGravity (w, ent, gravitydir, 1.0);

// move angles
	VectorMA (ent->v->angles, host_frametime, ent->v->avelocity, ent->v->angles);

// move origin
	VectorScale (ent->v->velocity, host_frametime, move);
	if (!DotProduct(move, move))
		return;
	VectorCopy(ent->v->origin, temporg);

	fl = 0;
#ifndef CLIENTONLY
	/*doesn't affect csqc, as it has no lagged ents registered anywhere*/
	if (sv_antilag.ival==2)
		fl |= MOVE_LAGGED;
#endif

	trace = WPhys_PushEntity (w, ent, move, fl);

	if (trace.allsolid)
	{
		trace.fraction = 0;

#pragma warningmsg("The following line might help boost framerates a lot in rmq, not sure if they violate expected behaviour in other mods though - check that they're safe.")
		VectorNegate(gravitydir, trace.plane.normal);
	}
	if (trace.fraction == 1)
		return;
	if (ent->isfree)
		return;

	VectorCopy(trace.endpos, move);

	if (ent->v->movetype == MOVETYPE_BOUNCE)
		backoff = 1.5;
	else if (ent->v->movetype == MOVETYPE_BOUNCEMISSILE)
	{
//		if (progstype == PROG_H2 && ent->v->solid == SOLID_PHASEH2 && ((int)((wedict_t*)trace.ent)->v->flags & (FL_MONSTER|FL_CLIENT)))
//			backoff = 0;
//		else
			backoff = 2;
	}
	else
		backoff = 1;

	if (backoff)
		ClipVelocity (ent->v->velocity, trace.plane.normal, ent->v->velocity, backoff);


// stop if on ground
	if ((-DotProduct(gravitydir, trace.plane.normal) > 0.7) && (ent->v->movetype != MOVETYPE_BOUNCEMISSILE))
	{
		if (-DotProduct(gravitydir, ent->v->velocity) < 60 || ent->v->movetype != MOVETYPE_BOUNCE )
		{
			ent->v->flags = (int)ent->v->flags | FL_ONGROUND;
			ent->v->groundentity = EDICT_TO_PROG(w->progs, trace.ent);
			VectorClear (ent->v->velocity);
			VectorClear (ent->v->avelocity);
		}
	}

// check for in water
	WPhys_CheckWaterTransition (w, ent);
}

/*
===============================================================================

STEPPING MOVEMENT

===============================================================================
*/

/*
=============
SV_Physics_Step

Monsters freefall when they don't have a ground entity, otherwise
all movement is done with discrete steps.

This is also used for objects that have become still on the ground, but
will fall if the floor is pulled out from under them.
FIXME: is this true?
=============
*/
static void WPhys_Physics_Step (world_t *w, wedict_t *ent)
{
	qboolean	hitsound;
	qboolean	freefall;
	int fl = ent->v->flags;
	const float *gravitydir;

	if (ent->xv->gravitydir[2] || ent->xv->gravitydir[1] || ent->xv->gravitydir[0])
		gravitydir = ent->xv->gravitydir;
	else
		gravitydir = standardgravity;

	if (-DotProduct(gravitydir, ent->v->velocity) >= (1.0 / 32.0) && (fl & FL_ONGROUND))
	{
		fl &= ~FL_ONGROUND;
		ent->v->flags = fl;
	}

// frefall if not onground
	if (fl & (FL_ONGROUND | FL_FLY))
		freefall = false;
	else
		freefall = true;
	if (fl & FL_SWIM)
		freefall = ent->v->waterlevel <= 0;
	if (freefall)
	{
		hitsound = -DotProduct(gravitydir, ent->v->velocity) < movevars.gravity*-0.1;

		WPhys_AddGravity (w, ent, gravitydir, 1.0);
		WPhys_CheckVelocity (w, ent);
		WPhys_FlyMove (w, ent, gravitydir, host_frametime, NULL);
		World_LinkEdict (w, ent, true);

		if ( (int)ent->v->flags & FL_ONGROUND )	// just hit ground
		{
			if (hitsound && *sv_sound_land.string)
			{
				w->Event_Sound(NULL, ent, 0, sv_sound_land.string, 255, 1, 0);
			}
		}
	}

// regular thinking
	WPhys_RunThink (w, ent);

	WPhys_CheckWaterTransition (w, ent);
}

//============================================================================

#ifndef CLIENTONLY
void SV_ProgStartFrame (void)
{

// let the progs know that a new frame has started
	pr_global_struct->self = EDICT_TO_PROG(svprogfuncs, sv.world.edicts);
	pr_global_struct->other = EDICT_TO_PROG(svprogfuncs, sv.world.edicts);
	pr_global_struct->time = sv.world.physicstime;
#ifdef VM_Q1
	if (svs.gametype == GT_Q1QVM)
		Q1QVM_StartFrame();
	else
#endif
	{
		if (pr_global_ptrs->StartFrame)
			PR_ExecuteProgram (svprogfuncs, *pr_global_ptrs->StartFrame);
	}
}
#endif












/*
=============
SV_CheckStuck

This is a big hack to try and fix the rare case of getting stuck in the world
clipping hull.
=============
*/
static void WPhys_CheckStuck (world_t *w, wedict_t *ent)
{
	int		i, j;
	int		z;
	vec3_t	org;
//return;
	if (!World_TestEntityPosition (w, ent))
	{
		VectorCopy (ent->v->origin, ent->v->oldorigin);
		return;
	}

	VectorCopy (ent->v->origin, org);
	VectorCopy (ent->v->oldorigin, ent->v->origin);
	if (!World_TestEntityPosition (w, ent))
	{
		Con_DPrintf ("Unstuck.\n");
		World_LinkEdict (w, ent, true);
		return;
	}

	for (z=0 ; z < movevars.stepheight ; z++)
		for (i=-1 ; i <= 1 ; i++)
			for (j=-1 ; j <= 1 ; j++)
			{
				ent->v->origin[0] = org[0] + i;
				ent->v->origin[1] = org[1] + j;
				ent->v->origin[2] = org[2] + z;
				if (!World_TestEntityPosition (w, ent))
				{
					Con_DPrintf ("Unstuck.\n");
					World_LinkEdict (w, ent, true);
					return;
				}
			}

	VectorCopy (org, ent->v->origin);
	Con_DPrintf ("player is stuck.\n");
}

/*
=============
SV_CheckWater
=============
*/
static qboolean WPhys_CheckWater (world_t *w, wedict_t *ent)
{
	vec3_t	point;
	int		cont;

	point[0] = ent->v->origin[0];
	point[1] = ent->v->origin[1];
	point[2] = ent->v->origin[2] + ent->v->mins[2] + 1;

	ent->v->waterlevel = 0;
	ent->v->watertype = Q1CONTENTS_EMPTY;
	cont = World_PointContents (w, point);
	if (cont & FTECONTENTS_FLUID)
	{
		if (cont & FTECONTENTS_LAVA)
			ent->v->watertype = Q1CONTENTS_LAVA;
		else if (cont & FTECONTENTS_SLIME)
			ent->v->watertype = Q1CONTENTS_SLIME;
		else if (cont & FTECONTENTS_WATER)
			ent->v->watertype = Q1CONTENTS_WATER;
		else
			ent->v->watertype = Q1CONTENTS_SKY;
		ent->v->waterlevel = 1;
		point[2] = ent->v->origin[2] + (ent->v->mins[2] + ent->v->maxs[2])*0.5;
		cont = World_PointContents (w, point);
		if (cont & FTECONTENTS_FLUID)
		{
			ent->v->waterlevel = 2;
			point[2] = ent->v->origin[2] + ent->v->view_ofs[2];
			cont = World_PointContents (w, point);
			if (cont & FTECONTENTS_FLUID)
				ent->v->waterlevel = 3;
		}
	}

	return ent->v->waterlevel > 1;
}


/*
============
SV_WallFriction

============
*/
static void WPhys_WallFriction (wedict_t *ent, trace_t *trace)
{
	vec3_t		forward, right, up;
	float		d, i;
	vec3_t		into, side;

	AngleVectors (ent->v->v_angle, forward, right, up);
	d = DotProduct (trace->plane.normal, forward);

	d += 0.5;
	if (d >= 0 || IS_NAN(d))
		return;

// cut the tangential velocity
	i = DotProduct (trace->plane.normal, ent->v->velocity);
	VectorScale (trace->plane.normal, i, into);
	VectorSubtract (ent->v->velocity, into, side);

	ent->v->velocity[0] = side[0] * (1 + d);
	ent->v->velocity[1] = side[1] * (1 + d);
}

// warning: �SV_TryUnstick� defined but not used
/*
=====================
SV_TryUnstick

Player has come to a dead stop, possibly due to the problem with limited
float precision at some angle joins in the BSP hull.

Try fixing by pushing one pixel in each direction.

This is a hack, but in the interest of good gameplay...
======================

static int SV_TryUnstick (edict_t *ent, vec3_t oldvel)
{
	int		i;
	vec3_t	oldorg;
	vec3_t	dir;
	int		clip;
	trace_t	steptrace;

	VectorCopy (ent->v->origin, oldorg);
	VectorClear (dir);

	for (i=0 ; i<8 ; i++)
	{
// try pushing a little in an axial direction
		switch (i)
		{
			case 0:	dir[0] = 2; dir[1] = 0; break;
			case 1:	dir[0] = 0; dir[1] = 2; break;
			case 2:	dir[0] = -2; dir[1] = 0; break;
			case 3:	dir[0] = 0; dir[1] = -2; break;
			case 4:	dir[0] = 2; dir[1] = 2; break;
			case 5:	dir[0] = -2; dir[1] = 2; break;
			case 6:	dir[0] = 2; dir[1] = -2; break;
			case 7:	dir[0] = -2; dir[1] = -2; break;
		}

		SV_PushEntity (ent, dir, MOVE_NORMAL);

// retry the original move
		ent->v->velocity[0] = oldvel[0];
		ent->v-> velocity[1] = oldvel[1];
		ent->v-> velocity[2] = 0;
		clip = SV_FlyMove (ent, 0.1, &steptrace);

		if ( fabs(oldorg[1] - ent->v->origin[1]) > 4
		|| fabs(oldorg[0] - ent->v->origin[0]) > 4 )
		{
//Con_DPrintf ("unstuck!\n");
			return clip;
		}

// go back to the original pos and try again
		VectorCopy (oldorg, ent->v->origin);
	}

	VectorClear (ent->v->velocity);
	return 7;		// still not moving
}
*/

/*
=====================
SV_WalkMove

Only used by players
======================
*/
#if 0
#define	SMSTEPSIZE	4
static void SV_WalkMove (edict_t *ent)
{
	vec3_t		upmove, downmove;
	vec3_t		oldorg, oldvel;
	vec3_t		nosteporg, nostepvel;
	int			clip;
	int			oldonground;
	trace_t		steptrace, downtrace;

//
// do a regular slide move unless it looks like you ran into a step
//
	oldonground = (int)ent->v->flags & FL_ONGROUND;
	ent->v->flags = (int)ent->v->flags & ~FL_ONGROUND;

	VectorCopy (ent->v->origin, oldorg);
	VectorCopy (ent->v->velocity, oldvel);

	clip = SV_FlyMove (ent, host_frametime, &steptrace);

	if ( !(clip & 2) )
		return;		// move didn't block on a step

	if (!oldonground && ent->v->waterlevel == 0)
		return;		// don't stair up while jumping

	if (ent->v->movetype != MOVETYPE_WALK)
		return;		// gibbed by a trigger

//	if (sv_nostep.value)
//		return;

	if ( (int)ent->v->flags & FL_WATERJUMP )
		return;

	VectorCopy (ent->v->origin, nosteporg);
	VectorCopy (ent->v->velocity, nostepvel);

//
// try moving up and forward to go up a step
//
	VectorCopy (oldorg, ent->v->origin);	// back to start pos

	VectorCopy (vec3_origin, upmove);
	VectorCopy (vec3_origin, downmove);
	upmove[2] = movevars.stepheight;
	downmove[2] = -movevars.stepheight + oldvel[2]*host_frametime;

// move up
	SV_PushEntity (ent, upmove);	// FIXME: don't link?

// move forward
	ent->v->velocity[0] = oldvel[0];
	ent->v->velocity[1] = oldvel[1];
	ent->v->velocity[2] = 0;
	clip = SV_FlyMove (ent, host_frametime, &steptrace);

// check for stuckness, possibly due to the limited precision of floats
// in the clipping hulls
	if (clip)
	{
		if ( fabs(oldorg[1] - ent->v->origin[1]) < 0.03125
		&& fabs(oldorg[0] - ent->v->origin[0]) < 0.03125 )
		{	// stepping up didn't make any progress
			clip = SV_TryUnstick (ent, oldvel);

//			Con_Printf("Try unstick fwd\n");
		}
	}

// extra friction based on view angle
	if ( clip & 2 )
	{
		vec3_t lastpos, lastvel, lastdown;

//		Con_Printf("couldn't do it\n");

		//retry with a smaller step (allows entering smaller areas with a step of 4)
		VectorCopy (downmove, lastdown);
		VectorCopy (ent->v->origin, lastpos);
		VectorCopy (ent->v->velocity, lastvel);

	//
	// try moving up and forward to go up a step
	//
		VectorCopy (oldorg, ent->v->origin);	// back to start pos

		VectorCopy (vec3_origin, upmove);
		VectorCopy (vec3_origin, downmove);
		upmove[2] = SMSTEPSIZE;
		downmove[2] = -SMSTEPSIZE + oldvel[2]*host_frametime;

	// move up
		SV_PushEntity (ent, upmove);	// FIXME: don't link?

	// move forward
		ent->v->velocity[0] = oldvel[0];
		ent->v->velocity[1] = oldvel[1];
		ent->v->velocity[2] = 0;
		clip = SV_FlyMove (ent, host_frametime, &steptrace);

	// check for stuckness, possibly due to the limited precision of floats
	// in the clipping hulls
		if (clip)
		{
			if ( fabs(oldorg[1] - ent->v->origin[1]) < 0.03125
			&& fabs(oldorg[0] - ent->v->origin[0]) < 0.03125 )
			{	// stepping up didn't make any progress
				clip = SV_TryUnstick (ent, oldvel);

//				Con_Printf("Try unstick up\n");
			}
		}

		if ( fabs(oldorg[1] - ent->v->origin[1])+fabs(oldorg[0] - ent->v->origin[0]) < fabs(oldorg[1] - lastpos[1])+fabs(oldorg[1] - lastpos[1]))
		{	// stepping up didn't make any progress
				//go back
				VectorCopy (lastdown, downmove);
				VectorCopy (lastpos, ent->v->origin);
				VectorCopy (lastvel, ent->v->velocity);

				SV_WallFriction (ent, &steptrace);

//				Con_Printf("wall friction\n");
			}

		else if (clip & 2)
		{
			SV_WallFriction (ent, &steptrace);
//			Con_Printf("wall friction 2\n");
		}
	}

// move down
	downtrace = SV_PushEntity (ent, downmove);	// FIXME: don't link?

	if (downtrace.plane.normal[2] > 0.7)
	{
		if (ent->v->solid == SOLID_BSP)
		{
			ent->v->flags =	(int)ent->v->flags | FL_ONGROUND;
			ent->v->groundentity = EDICT_TO_PROG(svprogfuncs, downtrace.ent);
		}
	}
	else
	{
// if the push down didn't end up on good ground, use the move without
// the step up.  This happens near wall / slope combinations, and can
// cause the player to hop up higher on a slope too steep to climb
		VectorCopy (nosteporg, ent->v->origin);
		VectorCopy (nostepvel, ent->v->velocity);

//		Con_Printf("down not good\n");
	}
}
#else

// 1/32 epsilon to keep floating point happy
#define	DIST_EPSILON	(0.03125)
static int WPhys_SetOnGround (world_t *w, wedict_t *ent, const float *gravitydir)
{
	vec3_t end;
	trace_t trace;
	if ((int)ent->v->flags & FL_ONGROUND)
		return 1;
	VectorMA(ent->v->origin, 1, gravitydir, end);
	trace = World_Move(w, ent->v->origin, ent->v->mins, ent->v->maxs, end, MOVE_NORMAL, (wedict_t*)ent);
	if (trace.fraction <= DIST_EPSILON && -DotProduct(gravitydir, trace.plane.normal) >= 0.7)
	{
		ent->v->flags = (int)ent->v->flags | FL_ONGROUND;
		ent->v->groundentity = EDICT_TO_PROG(w->progs, trace.ent);
		return 1;
	}
	return 0;
}
static void WPhys_WalkMove (world_t *w, wedict_t *ent, const float *gravitydir)
{
	int clip, oldonground, originalmove_clip, originalmove_flags, originalmove_groundentity;
	vec3_t upmove, downmove, start_origin, start_velocity, originalmove_origin, originalmove_velocity;
	trace_t downtrace, steptrace;

	WPhys_CheckVelocity(w, ent);

	// do a regular slide move unless it looks like you ran into a step
	oldonground = (int)ent->v->flags & FL_ONGROUND;
	ent->v->flags = (int)ent->v->flags & ~FL_ONGROUND;

	VectorCopy (ent->v->origin, start_origin);
	VectorCopy (ent->v->velocity, start_velocity);

	clip = WPhys_FlyMove (w, ent, gravitydir, host_frametime, NULL);

	WPhys_SetOnGround (w, ent, gravitydir);
	WPhys_CheckVelocity(w, ent);

	VectorCopy(ent->v->origin, originalmove_origin);
	VectorCopy(ent->v->velocity, originalmove_velocity);
	originalmove_clip = clip;
	originalmove_flags = (int)ent->v->flags;
	originalmove_groundentity = ent->v->groundentity;

	if ((int)ent->v->flags & FL_WATERJUMP)
		return;

//	if (sv_nostep.value)
//		return;

	// if move didn't block on a step, return
	if (clip & 2)
	{
		// if move was not trying to move into the step, return
		if (fabs(start_velocity[0]) < 0.03125 && fabs(start_velocity[1]) < 0.03125)
			return;

		if (ent->v->movetype != MOVETYPE_FLY)
		{
			// return if gibbed by a trigger
			if (ent->v->movetype != MOVETYPE_WALK)
				return;

			// only step up while jumping if that is enabled
//			if (!(sv_jumpstep.value && sv_gameplayfix_stepwhilejumping.value))
				if (!oldonground && ent->v->waterlevel == 0)
					return;
		}

		// try moving up and forward to go up a step
		// back to start pos
		VectorCopy (start_origin, ent->v->origin);
		VectorCopy (start_velocity, ent->v->velocity);

		// move up
		VectorScale(gravitydir, -movevars.stepheight, upmove);
		// FIXME: don't link?
		WPhys_PushEntity(w, ent, upmove, MOVE_NORMAL);

		// move forward
		ent->v->velocity[2] = 0;
		clip = WPhys_FlyMove (w, ent, gravitydir, host_frametime, &steptrace);
		ent->v->velocity[2] += start_velocity[2];

		WPhys_CheckVelocity(w, ent);

		// check for stuckness, possibly due to the limited precision of floats
		// in the clipping hulls
		if (clip
		 && fabs(originalmove_origin[1] - ent->v->origin[1]) < 0.03125
		 && fabs(originalmove_origin[0] - ent->v->origin[0]) < 0.03125)
		{
//			Con_Printf("wall\n");
			// stepping up didn't make any progress, revert to original move
			VectorCopy(originalmove_origin, ent->v->origin);
			VectorCopy(originalmove_velocity, ent->v->velocity);
			//clip = originalmove_clip;
			ent->v->flags = originalmove_flags;
			ent->v->groundentity = originalmove_groundentity;
			// now try to unstick if needed
			//clip = SV_TryUnstick (ent, oldvel);
			return;
		}

		//Con_Printf("step - ");

		// extra friction based on view angle
		if (clip & 2)// && sv_wallfriction.value)
		{
//			Con_Printf("wall\n");
			WPhys_WallFriction (ent, &steptrace);
		}
	}
	else if (/*!sv_gameplayfix_stepdown.integer || */!oldonground || start_velocity[2] > 0 || ((int)ent->v->flags & FL_ONGROUND) || ent->v->waterlevel >= 2)
		return;

	// move down
	VectorScale(gravitydir, -(-movevars.stepheight + start_velocity[2]*host_frametime), downmove);
	// FIXME: don't link?
	downtrace = WPhys_PushEntity (w, ent, downmove, MOVE_NORMAL);

	if (downtrace.fraction < 1 && -DotProduct(gravitydir, downtrace.plane.normal) > 0.7)
	{
		// LordHavoc: disabled this check so you can walk on monsters/players
		//if (ent->v->solid == SOLID_BSP)
		{
			//Con_Printf("onground\n");
			ent->v->flags =	(int)ent->v->flags | FL_ONGROUND;
			ent->v->groundentity = EDICT_TO_PROG(w->progs, downtrace.ent);
		}
	}
	else
	{
		//Con_Printf("slope\n");
		// if the push down didn't end up on good ground, use the move without
		// the step up.  This happens near wall / slope combinations, and can
		// cause the player to hop up higher on a slope too steep to climb
		VectorCopy(originalmove_origin, ent->v->origin);
		VectorCopy(originalmove_velocity, ent->v->velocity);
		//clip = originalmove_clip;
		ent->v->flags = originalmove_flags;
		ent->v->groundentity = originalmove_groundentity;
	}

	WPhys_SetOnGround (w, ent, gravitydir);
	WPhys_CheckVelocity(w, ent);
}
#endif

void WPhys_MoveChain(world_t *w, wedict_t *ent, wedict_t *movechain, float *initial_origin, float *initial_angle)
{
	qboolean callfunc;
	if ((callfunc=DotProduct(ent->v->origin, initial_origin)) || DotProduct(ent->v->angles, initial_angle))
	{
		vec3_t moveang, moveorg;
		int i;
		VectorSubtract(ent->v->angles, initial_angle, moveang);
		VectorSubtract(ent->v->origin, initial_origin, moveorg);

		for(i=16;i && movechain != w->edicts && !movechain->isfree;i--, movechain = PROG_TO_WEDICT(w->progs, movechain->xv->movechain))
		{
			if ((int)movechain->v->flags & FL_MOVECHAIN_ANGLE)
				VectorAdd(movechain->v->angles, moveang, movechain->v->angles);
			VectorAdd(movechain->v->origin, moveorg, movechain->v->origin);

			if (movechain->xv->chainmoved && callfunc)
			{
				*w->g.self = EDICT_TO_PROG(w->progs, movechain);
				*w->g.other = EDICT_TO_PROG(w->progs, ent);
#ifdef VM_Q1
				if (svs.gametype == GT_Q1QVM && w == &sv.world)
					Q1QVM_ChainMoved();
				else
#endif
					PR_ExecuteProgram(w->progs, movechain->xv->chainmoved);
			}
		}
	}
}

/*
================
SV_RunEntity

================
*/
void WPhys_RunEntity (world_t *w, wedict_t *ent)
{
	wedict_t	*movechain;
	vec3_t	initial_origin = {0},initial_angle = {0}; // warning: �initial_?[?]� may be used uninitialized in this function
	const float *gravitydir;

#ifndef CLIENTONLY
	edict_t *svent = (edict_t*)ent;
	if (ent->entnum > 0 && ent->entnum <= sv.allocated_client_slots && w == &sv.world)
	{	//a client woo.
		qboolean readyforjump = false;

		if ( svs.clients[ent->entnum-1].state < cs_spawned )
			return;		// unconnected slot


		if (svs.clients[ent->entnum-1].protocol == SCP_BAD)
			svent->v->fixangle = 0;	//bots never get fixangle cleared otherwise

		host_client = &svs.clients[ent->entnum-1];
		SV_ClientThink();

		if (progstype == PROG_QW)	//detect if the mod should do a jump
			if (svent->v->button2)
				if ((int)svent->v->flags & FL_JUMPRELEASED)
					readyforjump = true;

	//
	// call standard client pre-think
	//
		pr_global_struct->time = sv.world.physicstime;
		pr_global_struct->self = EDICT_TO_PROG(svprogfuncs, ent);
#ifdef VM_Q1
		if (svs.gametype == GT_Q1QVM)
			Q1QVM_PlayerPreThink();
		else
#endif
			if (pr_global_ptrs->PlayerPreThink)
				PR_ExecuteProgram (svprogfuncs, *pr_global_ptrs->PlayerPreThink);

		if (readyforjump)	//qw progs can't jump for themselves...
		{
			if (!svent->v->button2 && !((int)ent->v->flags & FL_JUMPRELEASED) && ent->v->velocity[2] <= 0)
				svent->v->velocity[2] += 270;
		}
	}
	else
#endif
	{
		if ((unsigned int)ent->v->lastruntime == w->framenum)
			return;
		ent->v->lastruntime = w->framenum;
#ifndef CLIENTONLY
		svent = NULL;
#endif
	}



	movechain = PROG_TO_WEDICT(w->progs, ent->xv->movechain);
	if (movechain != w->edicts)
	{
		VectorCopy(ent->v->origin,initial_origin);
		VectorCopy(ent->v->angles,initial_angle);
	}

	switch ( (int)ent->v->movetype)
	{
	case MOVETYPE_PUSH:
		WPhys_Physics_Pusher (w, ent);
		break;
	case MOVETYPE_NONE:
		if (!WPhys_RunThink (w, ent))
			return;
		break;
	case MOVETYPE_NOCLIP:
	case MOVETYPE_ANGLENOCLIP:
		WPhys_Physics_Noclip (w, ent);
		break;
	case MOVETYPE_STEP:
	case MOVETYPE_H2PUSHPULL:
		WPhys_Physics_Step (w, ent);
		break;
	case MOVETYPE_FOLLOW:
		WPhys_Physics_Follow (w, ent);
		break;
	case MOVETYPE_FLY:
	case MOVETYPE_H2SWIM:
	case MOVETYPE_TOSS:
	case MOVETYPE_BOUNCE:
	case MOVETYPE_BOUNCEMISSILE:
	case MOVETYPE_FLYMISSILE:
		WPhys_Physics_Toss (w, ent);
		break;
	case MOVETYPE_WALK:
		if (!WPhys_RunThink (w, ent))
			return;

		if (ent->xv->gravitydir[2] || ent->xv->gravitydir[1] || ent->xv->gravitydir[0])
			gravitydir = ent->xv->gravitydir;
		else
			gravitydir = standardgravity;

		if (!WPhys_CheckWater (w, ent) && ! ((int)ent->v->flags & FL_WATERJUMP) )
			WPhys_AddGravity (w, ent, gravitydir, ent->xv->gravity);
		WPhys_CheckStuck (w, ent);

		WPhys_WalkMove (w, ent, gravitydir);

#ifndef CLIENTONLY
		if (!(ent->entnum > 0 && ent->entnum <= sv.allocated_client_slots) && w == &sv.world)
			World_LinkEdict (w, ent, true);
#endif

		break;
	case MOVETYPE_PHYSICS:
		if (WPhys_RunThink(w, ent))
			World_LinkEdict (w, ent, true);
		w->ode.hasodeents = true;
		break;
	default:
//		SV_Error ("SV_Physics: bad movetype %i on %s", (int)ent->v->movetype, PR_GetString(w->progs, ent->v->classname));
		break;
	}

	if (movechain != w->edicts)
	{
		WPhys_MoveChain(w, ent, movechain, initial_origin, initial_angle);
	}

#ifndef CLIENTONLY
	if (svent)
	{
		World_LinkEdict (w, (wedict_t*)svent, true);

		pr_global_struct->time = w->physicstime;
		pr_global_struct->self = EDICT_TO_PROG(w->progs, ent);
#ifdef VM_Q1
		if (svs.gametype == GT_Q1QVM)
			Q1QVM_PostThink();
		else
#endif
		{
			if (pr_global_ptrs->PlayerPostThink)
				PR_ExecuteProgram (w->progs, *pr_global_ptrs->PlayerPostThink);
		}
	}
#endif
}

/*
================
SV_RunNewmis

================
*/
void WPhys_RunNewmis (world_t *w)
{
	wedict_t	*ent;

	if (!w->g.newmis)	//newmis variable is not exported.
		return;

	if (!sv_gameplayfix_multiplethinks.ival)
		return;

	if (!*w->g.newmis)
		return;
	ent = PROG_TO_WEDICT(w->progs, *w->g.newmis);
	host_frametime = 0.05;
	*w->g.newmis = 0;

	WPhys_RunEntity (w, ent);

	host_frametime = *w->g.frametime;
}

trace_t WPhys_Trace_Toss (world_t *w, wedict_t *tossent, wedict_t *ignore)
{
	int i;
	float gravity;
	vec3_t move, end;
	trace_t trace;

	vec3_t origin, velocity;

	// this has to fetch the field from the original edict, since our copy is truncated
	gravity = tossent->xv->gravity;
	if (!gravity)
		gravity = 1.0;
	gravity *= sv_gravity.value * 0.05;

	VectorCopy (tossent->v->origin, origin);
	VectorCopy (tossent->v->velocity, velocity);

	WPhys_CheckVelocity (w, tossent);

	for (i = 0;i < 200;i++) // LordHavoc: sanity check; never trace more than 10 seconds
	{
		velocity[2] -= gravity;
		VectorScale (velocity, 0.05, move);
		VectorAdd (origin, move, end);
		trace = World_Move (w, origin, tossent->v->mins, tossent->v->maxs, end, MOVE_NORMAL, tossent);
		VectorCopy (trace.endpos, origin);

		if (trace.fraction < 1 && trace.ent && trace.ent != ignore)
			break;

		if (Length(velocity) > sv_maxvelocity.value)
		{
//			Con_DPrintf("Slowing %s\n", PR_GetString(w->progs, tossent->v->classname));
			VectorScale (velocity, sv_maxvelocity.value/Length(velocity), velocity);
		}
	}

	trace.fraction = 0; // not relevant
	return trace;
}

/*
Run an individual physics frame. This might be run multiple times in one frame if we're running slow, or not at all.
*/
void World_Physics_Frame(world_t *w)
{
	int i;
	qboolean retouch;
	wedict_t *ent;
	extern cvar_t sv_nqplayerphysics;

	w->framenum++;

	i = *w->g.physics_mode;
	if (i == 0)
	{
		/*physics mode 0 = none*/
		return;
	}
	if (i == 1)
	{
		/*physics mode 1 = thinks only*/
		for (i=0 ; i<w->num_edicts ; i++)
		{
			ent = (wedict_t*)EDICT_NUM(w->progs, i);
			if (ent->isfree)
				continue;

			WPhys_RunThink (w, ent);
		}
		return;
	}
	/*physics mode 2 = normal movetypes*/

	retouch = (w->g.force_retouch && (*w->g.force_retouch >= 1));

	//
	// treat each object in turn
	// even the world gets a chance to think
	//
	for (i=0 ; i<w->num_edicts ; i++)
	{
		ent = (wedict_t*)EDICT_NUM(w->progs, i);
		if (ent->isfree)
			continue;

		if (retouch)
			World_LinkEdict (w, ent, true);	// force retouch even for stationary

#ifndef CLIENTONLY
		if (i > 0 && i <= sv.allocated_client_slots && w == &sv.world)
		{
			if (!svs.clients[i-1].isindependant)
			{
				if (sv_nqplayerphysics.ival || svs.clients[i-1].state < cs_spawned)
				{
					WPhys_RunEntity (w, ent);
					WPhys_RunNewmis (w);
				}
				else
				{
					int newt;
					int delt;
					newt = sv.time*1000;
					delt = newt - svs.clients[i-1].msecs;
					if (delt > 1000/77 || delt < -10)
					{
						float ft = host_frametime;
						host_client = &svs.clients[i-1];
						sv_player = svs.clients[i-1].edict;
						svs.clients[i-1].msecs = newt;
						SV_PreRunCmd();
						svs.clients[i-1].last_check = 0;
						svs.clients[i-1].lastcmd.msec = bound(0, delt, 255);
						SV_RunCmd (&svs.clients[i-1].lastcmd, true);
						svs.clients[i-1].lastcmd.impulse = 0;
						SV_PostRunCmd();
						*w->g.frametime = host_frametime = ft;
					}
				}
			}
//			else
//				World_LinkEdict(w, (wedict_t*)ent, true);
			continue;		// clients are run directly from packets
		}
#endif

		WPhys_RunEntity (w, ent);
		WPhys_RunNewmis (w);
	}

	if (retouch)
		*w->g.force_retouch-=1;
}

#ifndef CLIENTONLY
/*
================
SV_Physics

================
*/
qboolean SV_Physics (void)
{
	int		i;
	qboolean moved = false;
	int maxtics;

	//keep gravity tracking the cvar properly
	movevars.gravity = sv_gravity.value;

	if (svs.gametype != GT_PROGS && svs.gametype != GT_Q1QVM && svs.gametype != GT_HALFLIFE)	//make tics multiples of sv_maxtic (defaults to 0.1)
	{
		host_frametime = sv.time - sv.world.physicstime;
		if (host_frametime<0)
		{
			if (host_frametime < -1)
				sv.world.physicstime = sv.time;
			host_frametime = 0;
		}
		if (svs.gametype != GT_QUAKE3)
		if (host_frametime < sv_maxtic.value && realtime)
		{
//			sv.time+=host_frametime;
			return false;	//don't bother with the whole server thing for a bit longer
		}
		if (host_frametime > sv_maxtic.value)
			host_frametime = sv_maxtic.value;
		sv.world.physicstime = sv.time;

		switch(svs.gametype)
		{
#ifdef Q2SERVER
		case GT_QUAKE2:
			ge->RunFrame();
			break;
#endif
#ifdef Q3SERVER
		case GT_QUAKE3:
			SVQ3_RunFrame();
			break;
#endif
		default:
			break;
		}
		return true;
	}

	if (svs.gametype != GT_HALFLIFE && /*sv.botsonthemap &&*/ progstype == PROG_QW)
	{
		//DP_SV_BOTCLIENT - make the bots move with qw physics.
		//They only move when there arn't any players on the server, but they should move at the right kind of speed if there are... hopefully
		//they might just be a bit lagged. they will at least be as smooth as other players are.

		usercmd_t ucmd;
		static int old_bot_time;	//I hate using floats for timers.
		int newbottime, ms;
		client_t *oldhost;
		edict_t *oldplayer;
		host_frametime = (Sys_Milliseconds() - old_bot_time) / 1000.0f;
		if (1 || host_frametime >= 1 / 72.0f)
		{
			memset(&ucmd, 0, sizeof(ucmd));
			newbottime = Sys_Milliseconds();
			ms = newbottime - old_bot_time;
			old_bot_time = newbottime;
			for (i = 1; i <= sv.allocated_client_slots; i++)
			{
				if (svs.clients[i-1].state && svs.clients[i-1].protocol == SCP_BAD)
				{	//then this is a bot
					oldhost = host_client;
					oldplayer = sv_player;
					host_client = &svs.clients[i-1];
					host_client->isindependant = true;
					sv_player = host_client->edict;

					SV_PreRunCmd();

#ifdef SERVERONLY
					ucmd.msec = host_frametime*1000;
#else
					// FIXME: Something very weird is going on here!
					ucmd.msec = ms;
#endif
					ucmd.angles[0] = (int)(sv_player->v->v_angle[0] * (65535/360.0f));
					ucmd.angles[1] = (int)(sv_player->v->v_angle[1] * (65535/360.0f));
					ucmd.angles[2] = (int)(sv_player->v->v_angle[2] * (65535/360.0f));
					ucmd.forwardmove = sv_player->xv->movement[0];
					ucmd.sidemove = sv_player->xv->movement[1];
					ucmd.upmove = sv_player->xv->movement[2];
					ucmd.buttons = (sv_player->v->button0?1:0) | (sv_player->v->button2?2:0);

					svs.clients[i-1].lastcmd = ucmd;	//allow the other clients to predict this bot.

					SV_RunCmd(&ucmd, false);
					SV_PostRunCmd();

					host_client = oldhost;
					sv_player = oldplayer;
				}
			}
			old_bot_time = Sys_Milliseconds();
		}
	}

	maxtics = sv_limittics.ival;

// don't bother running a frame if sys_ticrate seconds haven't passed
	while (1)
	{
		host_frametime = sv.time - sv.world.physicstime;
		if (host_frametime < 0)
		{
			sv.world.physicstime = sv.time;
			break;
		}
		if (host_frametime <= 0 || host_frametime < sv_mintic.value)
			break;
		if (host_frametime > sv_maxtic.value)
		{
			if (maxtics-- <= 0)
			{
				//timewarp, as we're running too slowly
				sv.world.physicstime = sv.time;
				break;
			}
			host_frametime = sv_maxtic.value;
		}
		if (!host_frametime)
			continue;
		sv.world.physicstime += host_frametime;

		moved = true;

#ifdef HLSERVER
		if (svs.gametype == GT_HALFLIFE)
		{
			SVHL_RunFrame();
			continue;
		}
#endif

		pr_global_struct->frametime = host_frametime;

		SV_ProgStartFrame ();

		PRSV_RunThreads();

#ifdef USEODE
		World_ODE_Frame(&sv.world, host_frametime, sv_gravity.value);
#endif


		World_Physics_Frame(&sv.world);

#ifdef VM_Q1
		if (svs.gametype == GT_Q1QVM)
		{
			pr_global_struct->self = EDICT_TO_PROG(svprogfuncs, sv.world.edicts);
			pr_global_struct->other = EDICT_TO_PROG(svprogfuncs, sv.world.edicts);
			pr_global_struct->time = sv.world.physicstime;
			Q1QVM_EndFrame();
		}
		else
#endif
			if (EndFrameQC)
		{
			pr_global_struct->self = EDICT_TO_PROG(svprogfuncs, sv.world.edicts);
			pr_global_struct->other = EDICT_TO_PROG(svprogfuncs, sv.world.edicts);
			pr_global_struct->time = sv.world.physicstime;
			PR_ExecuteProgram (svprogfuncs, EndFrameQC);
		}

		NPP_Flush();	//flush it just in case there was an error and we stopped preparsing. This is only really needed while debugging.

	}
	return moved;
}
#endif

void SV_SetMoveVars(void)
{
	movevars.stopspeed		    = sv_stopspeed.value;
	movevars.maxspeed			= sv_maxspeed.value;
	movevars.spectatormaxspeed  = sv_spectatormaxspeed.value;
	movevars.accelerate		    = sv_accelerate.value;
	movevars.airaccelerate	    = sv_airaccelerate.value;
	movevars.wateraccelerate	= sv_wateraccelerate.value;
	movevars.friction			= sv_friction.value;
	movevars.waterfriction	    = sv_waterfriction.value;
	movevars.entgravity			= 1.0;
	if (*sv_stepheight.string)
		movevars.stepheight			= sv_stepheight.value;
	else
		movevars.stepheight			= PM_DEFAULTSTEPHEIGHT;
}
#endif
