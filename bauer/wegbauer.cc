/*
 * Copyright (c) 1997 - 2001 Hansjoerg Malthaner
 *
 * This file is part of the Simutrans project under the artistic licence.
 * (see licence.txt)
 *
 * Ways (Roads, Railways, etc.)
 *
 * Hj. Malthaner
 */

#include <algorithm>

#include "../simdebug.h"
#include "../simworld.h"
#include "../simtool.h"
#include "../simmesg.h"
#include "../simintr.h"
#include "../player/simplay.h"
#include "../simplan.h"
#include "../utils/simrandom.h"
#include "../simdepot.h"

#include "wegbauer.h"
#include "brueckenbauer.h"
#include "tunnelbauer.h"

#include "../besch/weg_besch.h"
#include "../besch/tunnel_besch.h"
#include "../besch/haus_besch.h"
#include "../besch/kreuzung_besch.h"

#include "../boden/wege/strasse.h"
#include "../boden/wege/schiene.h"
#include "../boden/wege/monorail.h"
#include "../boden/wege/maglev.h"
#include "../boden/wege/narrowgauge.h"
#include "../boden/wege/kanal.h"
#include "../boden/wege/runway.h"
#include "../boden/brueckenboden.h"
#include "../boden/monorailboden.h"
#include "../boden/tunnelboden.h"
#include "../boden/grund.h"

#include "../dataobj/environment.h"
#include "../dataobj/route.h"
#include "../dataobj/marker.h"
#include "../dataobj/translator.h"
#include "../dataobj/scenario.h"

// binary heap, since we only need insert and pop
#include "../tpl/binary_heap_tpl.h" // fastest

#include "../obj/field.h"
#include "../obj/gebaeude.h"
#include "../obj/bruecke.h"
#include "../obj/tunnel.h"
#include "../obj/crossing.h"
#include "../obj/leitung2.h"
#include "../obj/groundobj.h"
#include "../obj/wayobj.h"

#include "../ifc/simtestdriver.h"

#include "../gui/messagebox.h"
#include "../tpl/stringhashtable_tpl.h"

#include "../gui/karte.h"	// for debugging
#include "../gui/tool_selector.h"
#include "../gui/messagebox.h"

#ifdef DEBUG_ROUTES
#include "../simsys.h"
#endif

// built bridges automatically
//#define AUTOMATIC_BRIDGES

// built tunnels automatically
//#define AUTOMATIC_TUNNELS

// lookup also return route and take the better of the two
#define REVERSE_CALC_ROUTE_TOO

karte_ptr_t wegbauer_t::welt;

const weg_besch_t *wegbauer_t::leitung_besch = NULL;

static stringhashtable_tpl <weg_besch_t *> alle_wegtypen;

stringhashtable_tpl <weg_besch_t *> * wegbauer_t::get_all_ways()
{
	return &alle_wegtypen;
}

static void set_default(weg_besch_t const*& def, waytype_t const wtyp, sint32 const speed_limit = 1)
{
	def = wegbauer_t::weg_search(wtyp, speed_limit, 0, type_flat);
}


static void set_default(weg_besch_t const*& def, waytype_t const wtyp, systemtype_t const system_type, sint32 const speed_limit = 1)
{
	set_default(def, wtyp, speed_limit);
	if (def) {
		return;
	}
	def = wegbauer_t::weg_search(wtyp, 1, 0, system_type);
}


bool wegbauer_t::alle_wege_geladen()
{
	// some defaults to avoid hardcoded values
	set_default(strasse_t::default_strasse,         road_wt,        type_flat, 50);
	if(  strasse_t::default_strasse == NULL ) {
		dbg->fatal( "wegbauer_t::alle_wege_geladen()", "No road found at all!" );
		return false;
	}

	set_default(schiene_t::default_schiene,         track_wt,       type_flat, 80);
	set_default(monorail_t::default_monorail,       monorail_wt,    type_elevated); // Only elevated?
	set_default(maglev_t::default_maglev,           maglev_wt,      type_elevated); // Only elevated?
	set_default(narrowgauge_t::default_narrowgauge, narrowgauge_wt);
	set_default(kanal_t::default_kanal,             water_wt,       type_all); // Also find hidden rivers.
	set_default(runway_t::default_runway,           air_wt);
	set_default(wegbauer_t::leitung_besch,          powerline_wt);

	return true;
}


bool wegbauer_t::register_besch(weg_besch_t *besch)
{
	DBG_DEBUG("wegbauer_t::register_besch()", besch->get_name());
	const weg_besch_t *old_besch = alle_wegtypen.get(besch->get_name());
	if(  old_besch  ) {
		alle_wegtypen.remove(besch->get_name());
		dbg->warning( "wegbauer_t::register_besch()", "Object %s was overlaid by addon!", besch->get_name() );
		tool_t::general_tool.remove( old_besch->get_builder() );
		delete old_besch->get_builder();
		delete old_besch;
	}

	if(  besch->get_cursor()->get_image_id(1)!=IMG_EMPTY  ) {
		// add the tool
		tool_build_way_t *tool = new tool_build_way_t();
		tool->set_icon( besch->get_cursor()->get_image_id(1) );
		tool->cursor = besch->get_cursor()->get_image_id(0);
		tool->set_default_param(besch->get_name());
		tool_t::general_tool.append( tool );
		besch->set_builder( tool );
	}
	else {
		besch->set_builder( NULL );
	}
	alle_wegtypen.put(besch->get_name(), besch);
	return true;
}

const vector_tpl<const weg_besch_t *>&  wegbauer_t::get_way_list(const waytype_t wtyp, systemtype_t styp)
{
	static vector_tpl<const weg_besch_t *> dummy;
	dummy.clear();
	const uint16 time = welt->get_timeline_year_month();
	FOR(stringhashtable_tpl<weg_besch_t*>, & i, alle_wegtypen) {
		weg_besch_t const* const test = i.value;
		if (test->get_wtyp() == wtyp  &&  test->get_styp() == styp  &&  test->is_available(time) && test->get_builder()) {
			dummy.append(test);
		}
	}
	return dummy;
}

/**
 * Finds a way with a given speed limit for a given waytype
 * It finds:
 *  - the slowest way, as fast as speed limit
 *  - if no way faster than speed limit, the fastest way.
 * The timeline is also respected.
 * @author prissi, gerw
 */
const weg_besch_t* wegbauer_t::weg_search(const waytype_t wtyp, const sint32 speed_limit, const uint16 time, const systemtype_t system_type)
{
	const weg_besch_t* best = NULL;
	bool best_allowed = false; // Does the best way fulfill the timeline?
	FOR(stringhashtable_tpl<weg_besch_t *>, const& i, alle_wegtypen) {
		weg_besch_t const* const test = i.value;
		if(  ((test->get_wtyp()==wtyp  &&
			(test->get_styp()==system_type  ||  system_type==type_all))  ||  (test->get_wtyp()==track_wt  &&  test->get_styp()==type_tram  &&  wtyp==tram_wt))
			&&  test->get_cursor()->get_image_id(1)!=IMG_EMPTY  ) 
		{
			bool test_allowed = (time == 0 || (test->get_intro_year_month() <= time && time < test->get_retire_year_month())) && !test->is_mothballed();
				if(!best_allowed || test_allowed) 
				{
					if(  best==NULL  ||
						( best->get_topspeed() <  test->get_topspeed()  &&  test->get_topspeed() <=     speed_limit  )    || // closer to desired speed (from the low end)
						(     speed_limit      <  best->get_topspeed()  &&  test->get_topspeed() <   best->get_topspeed()) || // respects speed_limit better
						( time!=0  &&  !best_allowed  &&  test_allowed)                                                       // current choice is actually not really allowed, timewise
						) 
					{
							best = test;
							best_allowed = test_allowed;
					}
				}
		}
	}
	return best;
}

// Finds a way with a given speed *and* weight limit
// for a given way type.
// @author: jamespetts (slightly adapted from the standard version with just speed limit)
const weg_besch_t* wegbauer_t::weg_search(const waytype_t wtyp, const sint32 speed_limit, const uint32 weight_limit, const uint16 time, const systemtype_t system_type, const uint32 wear_capacity_limit, way_constraints_of_vehicle_t way_constraints)
{
	const weg_besch_t* best = NULL;
	bool best_allowed = false; // Does the best way fulfill the timeline?
	FOR(stringhashtable_tpl<weg_besch_t*>, const& iter, alle_wegtypen)
	{
		const weg_besch_t* const test = iter.value;
		if(((test->get_wtyp() == wtyp
			&& (test->get_styp() == system_type ||
				 system_type == type_all)) || 
				 (test->get_wtyp() == track_wt &&  
				 test->get_styp() == type_tram &&  
				 wtyp == tram_wt))
			     && test->get_cursor()->get_image_id(1) != IMG_EMPTY) 
		{
			const missing_way_constraints_t missing_constraints(way_constraints, test->get_way_constraints());
			bool test_allowed = (time == 0 || (test->get_intro_year_month() <= time && time < test->get_retire_year_month())) && missing_constraints.get_permissive() == 0;

			const sint32 test_topspeed = test->get_topspeed();
			const uint32 test_max_axle_load = test->get_max_axle_load();
			const uint32 test_wear_capacity = test->get_wear_capacity();

			if(best == NULL || test_allowed) 
			{
				if(best == NULL ||
						((best->get_topspeed() < speed_limit && test_topspeed >= speed_limit) ||
						(best->get_max_axle_load() < weight_limit && test_max_axle_load >= weight_limit) ||
						(best->get_wear_capacity() < wear_capacity_limit && test_wear_capacity >= wear_capacity_limit)) ||

						((test_topspeed <= speed_limit && best->get_topspeed() < test_topspeed) ||	
						(test_max_axle_load <= weight_limit && best->get_max_axle_load() < test_max_axle_load) ||
						(test->get_wear_capacity() <= wear_capacity_limit && best->get_wear_capacity() < test->get_wear_capacity())) ||

						((best->get_preis() > test->get_preis() && test_topspeed >= speed_limit && test_max_axle_load >= weight_limit && test->get_wear_capacity() >= wear_capacity_limit) ||		
						((best->get_maintenance() > test->get_maintenance() && test_topspeed >= speed_limit && test_max_axle_load >= weight_limit && test_wear_capacity >= wear_capacity_limit))) ||	

						(time !=0 && !best_allowed && test_allowed)
					) 
				{
					best = test;
					best_allowed = test_allowed;
				}
			}
		}
	}
	return best;
}

const weg_besch_t *wegbauer_t::way_search_mothballed(const waytype_t wtyp, const systemtype_t system_type)
{
	FOR(stringhashtable_tpl<weg_besch_t*>, const& iter, alle_wegtypen)
	{
		const weg_besch_t* const test = iter.value;
		if(test->get_wtyp() == wtyp && (test->get_styp()==system_type || system_type==type_all) && test->is_mothballed())
		{
			return test;
		}
	}

	return NULL;
}

const weg_besch_t *wegbauer_t::get_earliest_way(const waytype_t wtyp)
{
	const weg_besch_t *besch = NULL;
	FOR(stringhashtable_tpl<weg_besch_t*>, const& i, alle_wegtypen) {
		weg_besch_t const* const test = i.value;
		if(  test->get_wtyp()==wtyp  &&  (besch==NULL  ||  test->get_intro_year_month()<besch->get_intro_year_month())  ) {
			besch = test;
		}
	}
	return besch;
}



const weg_besch_t *wegbauer_t::get_latest_way(const waytype_t wtyp)
{
	const weg_besch_t *besch = NULL;
	FOR(stringhashtable_tpl<weg_besch_t*>, const& i, alle_wegtypen) {
		weg_besch_t const* const test = i.value;
		if(  test->get_wtyp()==wtyp  &&  (besch==NULL  ||  test->get_retire_year_month()>besch->get_retire_year_month())  ) {
			besch = test;
		}
	}
	return besch;
}


// true if the way is available with timely
bool wegbauer_t::waytype_available( const waytype_t wtyp, uint16 time )
{
	if(  time==0  ) {
		return true;
	}

	FOR(stringhashtable_tpl<weg_besch_t*>, const& i, alle_wegtypen) {
		weg_besch_t const* const test = i.value;
		if(  test->get_wtyp()==wtyp  &&  test->get_intro_year_month()<=time  &&  test->get_retire_year_month()>time  ) {
			return true;
		}
	}
	return false;
}



const weg_besch_t * wegbauer_t::get_besch(const char * way_name, const uint16 time)
{
//DBG_MESSAGE("wegbauer_t::get_besch","return besch for %s in (%i)",way_name, time/12);
	if(!way_name)
	{
		return NULL;
	}
	const weg_besch_t *besch = alle_wegtypen.get(way_name);
	if(  besch  &&  besch->is_available(time)  ) {
		return besch;
	}
	return NULL;
}



// generates timeline message
void wegbauer_t::new_month()
{
	const uint16 current_month = welt->get_timeline_year_month();
	if(current_month!=0) {
		// check, what changed
		slist_tpl <const weg_besch_t *> matching;
		FOR(stringhashtable_tpl<weg_besch_t *>, const& i, alle_wegtypen) {
			weg_besch_t const* const besch = i.value;
			cbuffer_t buf;

			const uint16 intro_month = besch->get_intro_year_month();
			if(intro_month == current_month) {
				buf.printf( translator::translate("way %s now available:\n"), translator::translate(besch->get_name()) );
				welt->get_message()->add_message(buf,koord::invalid,message_t::new_vehicle,NEW_VEHICLE,besch->get_image_id(5,0));
			}

			const uint16 retire_month = besch->get_retire_year_month();
			if(retire_month == current_month) {
				buf.printf( translator::translate("way %s cannot longer used:\n"), translator::translate(besch->get_name()) );
				welt->get_message()->add_message(buf,koord::invalid,message_t::new_vehicle,NEW_VEHICLE,besch->get_image_id(5,0));
			}
		}

	}
}


static bool compare_ways(const weg_besch_t* a, const weg_besch_t* b)
{
	int cmp = a->get_topspeed() - b->get_topspeed();
	if(cmp==0) {
		cmp = (int)a->get_intro_year_month() - (int)b->get_intro_year_month();
	}
	if(cmp==0) {
		cmp = strcmp(a->get_name(), b->get_name());
	}
	return cmp<0;
}


/**
 * Fill menu with icons of given waytype, return number of added entries
 * @author Hj. Malthaner/prissi/dariok
 */
void wegbauer_t::fill_menu(tool_selector_t *tool_selector, const waytype_t wtyp, const systemtype_t styp, sint16 /*ok_sound*/)
{
	// check if scenario forbids this
	const waytype_t rwtyp = wtyp!=track_wt  || styp!=type_tram  ? wtyp : tram_wt;
	if (!welt->get_scenario()->is_tool_allowed(welt->get_active_player(), TOOL_BUILD_WAY | GENERAL_TOOL, rwtyp)) {
		return;
	}

	const uint16 time = welt->get_timeline_year_month();

	// list of matching types (sorted by speed)
	vector_tpl<weg_besch_t*> matching;

	FOR(stringhashtable_tpl<weg_besch_t *>, const& i, alle_wegtypen) {
		weg_besch_t* const besch = i.value;
		if (  besch->get_styp()==styp &&  besch->get_wtyp()==wtyp  &&  besch->get_builder()  &&  besch->is_available(time)  ) {
				matching.append(besch);
		}
	}
	std::sort(matching.begin(), matching.end(), compare_ways);

	// now add sorted ways ...
	FOR(vector_tpl<weg_besch_t*>, const i, matching) {
		tool_selector->add_tool_selector(i->get_builder());
	}
}


/* allow for railroad crossing
 * @author prissi
 */
bool wegbauer_t::check_crossing(const koord zv, const grund_t *bd, waytype_t wtyp0, const player_t *player) const
{
	const waytype_t wtyp = wtyp0==tram_wt ? track_wt : wtyp0;
	// nothing to cross here
	if (!bd->hat_wege()) {
		return true;
	}
	// no triple crossings please
	if (bd->has_two_ways() && !bd->hat_weg(wtyp)) {
		return false;
	}
	const weg_t *w = bd->get_weg_nr(0);
	// index of our wtype at the tile (must exists due to triple-crossing-check above)
	const uint8 iwtyp = w->get_waytype() != wtyp;
	// get the other way
	if(iwtyp==0) {
		w = bd->get_weg_nr(1);
		// no other way here
		if (w==NULL) {
			return true;
		}
	}
	// special case: tram track on road
	if ( (wtyp==road_wt  &&  w->get_waytype()==track_wt  &&  w->get_besch()->get_styp()==type_tram)  ||
		     (wtyp0==tram_wt  &&  w->get_waytype()==road_wt) ) {
		return true;
	}
	// right owner of the other way
	if(!check_owner(w->get_owner(),player) && !check_access(w, player)) 
	{
		return false;
	}
	// check for existing crossing
	crossing_t *cr = bd->find<crossing_t>();
	if (cr) {
		// index of the waytype in ns-direction at the crossing
		const uint8 ns_way = cr->get_dir();
		// only cross with the right direction
		return (ns_way==iwtyp ? ribi_t::is_straight_ns(ribi_type(zv)) : ribi_t::is_straight_ew(ribi_type(zv)));
	}
	// no crossings in tunnels
	if((bautyp & tunnel_flag)!=0  || bd->ist_tunnel()) {
		return false;
	}
	// no crossings on elevated ways
	if((bautyp & elevated_flag)!=0  ||  bd->get_typ()==grund_t::monorailboden) {
		return false;
	}
	// crossing available and ribis ok
	if(crossing_logic_t::get_crossing(wtyp, w->get_waytype(), 0, 0, welt->get_timeline_year_month())!=NULL) {
		ribi_t::ribi w_ribi = w->get_ribi_unmasked();
		// it is our way we want to cross: can we built a crossing here?
		// both ways must be straight and no ends
		return  ribi_t::is_straight(w_ribi)
					&&  !ribi_t::is_single(w_ribi)
					&&  ribi_t::is_straight(ribi_type(zv))
				&&  (w_ribi&ribi_type(zv))==0;
	}
	// cannot build crossing here
	return false;
}


/* crossing of powerlines, or no powerline
 * @author prissi
 */
bool wegbauer_t::check_for_leitung(const koord zv, const grund_t *bd) const
{
	if(zv==koord(0,0)) {
		return true;
	}
	leitung_t* lt = bd->find<leitung_t>();
	if(lt!=NULL) {
		ribi_t::ribi lt_ribi = lt->get_ribi();
		// it is our way we want to cross: can we built a crossing here?
		// both ways must be straight and no ends
		return
		  ribi_t::is_straight(lt_ribi)
		  &&  !ribi_t::is_single(lt_ribi)
		  &&  ribi_t::is_straight(ribi_type(zv))
		  &&  (lt_ribi&ribi_type(zv))==0
		  &&  !bd->ist_tunnel();
	}
	// check for transformer
	if (bd->find<pumpe_t>() != NULL || bd->find<senke_t>()  != NULL) {
		return false;
	}
	// ok, there is not high power transmission stuff going on here
	return true;
}


// allowed slope?
bool wegbauer_t::check_slope( const grund_t *from, const grund_t *to )
{
	const koord from_pos=from->get_pos().get_2d();
	const koord to_pos=to->get_pos().get_2d();
	const koord zv=to_pos-from_pos;

	if(  !besch->has_double_slopes()
		&&  (    (from->get_weg_hang()  &&  !(from->get_weg_hang() & 7))
		      ||   (to->get_weg_hang()  &&    !(to->get_weg_hang() & 7))  )  ) {
		return false;
	}

	if(from==to) {
		if(!slope_t::is_way(from->get_weg_hang())) {
			return false;
		}
	}
	else {
		if(from->get_weg_hang()  &&  ribi_t::doubles(ribi_type(from->get_weg_hang()))!=ribi_t::doubles(ribi_type(zv))) {
			return false;
		}
		if(to->get_weg_hang()  &&  ribi_t::doubles(ribi_type(to->get_weg_hang()))!=ribi_t::doubles(ribi_type(zv))) {
			return false;
		}
	}

	return true;
}


// allowed owner?
bool wegbauer_t::check_owner( const player_t *player1, const player_t *player2 ) const
{
	// unowned, mine or public property or superuser ... ?
	return player1==NULL  ||  player1==player2  ||  player1==welt->get_public_player()  ||  player2==welt->get_public_player();
}


/* do not go through depots, station buildings etc. ...
 * direction results from layout
 */
bool wegbauer_t::check_building( const grund_t *to, const koord dir ) const
{
	if(dir==koord(0,0)) {
		return true;
	}
	if(to->obj_count()<=0) {
		return true;
	}

	if(to->get_signalbox())
	{
		return false;
	}

	// first find all kind of buildings
	gebaeude_t *gb = to->find<gebaeude_t>();
	if(gb==NULL) {
		// but depots might be overlooked ...
		depot_t* depot = to->get_depot();
		// no road to tram depot and vice-versa
		if (depot) {
			if ( (waytype_t)(bautyp&bautyp_mask) != depot->get_waytype() ) {
				return false;
			}
		}
		gb = depot;
	}
	// check, if we may enter
	if(gb) {
		// now check for directions
		uint8 layouts = gb->get_tile()->get_besch()->get_all_layouts();
		uint8 layout = gb->get_tile()->get_layout();
		ribi_t::ribi r = ribi_type(dir);
		if(  layouts&1  ) {
			return false;
		}
		if(  layouts==4  ) {
			return  r == ribi_t::layout_to_ribi[layout];
		}
		return ribi_t::is_straight( r | ribi_t::doubles(ribi_t::layout_to_ribi[layout&1]) );
	}
	return true;
}


/* This is the core routine for the way search
 * it will check
 * A) allowed step
 * B) if allowed, calculate the cost for the step from from to to
 * @author prissi
 */
bool wegbauer_t::is_allowed_step( const grund_t *from, const grund_t *to, sint32 *costs )
{
	const koord from_pos=from->get_pos().get_2d();
	const koord to_pos=to->get_pos().get_2d();
	const koord zv=to_pos-from_pos;
	// fake empty elevated tiles
	static monorailboden_t to_dummy(koord3d::invalid, slope_t::flat);
	static monorailboden_t from_dummy(koord3d::invalid, slope_t::flat);

	const sint8 altitude = max(from->get_pos().z, to->get_pos().z) - welt->get_grundwasser();
	const sint8 max_altitude = besch->get_max_altitude();
	if(max_altitude > 0 && altitude > max_altitude)
	{
		// Too high
		return false;
	}

	// Mothballed way types can only be built over existing ways of the same type.
	if((mark_way_for_upgrade_only || besch->is_mothballed()) && (!from->get_weg(besch->get_wtyp()) || !to->get_weg(besch->get_wtyp())))
	{
		return false;
	}

	if(bautyp == schiene_tram && !from->get_weg(road_wt) && !to->get_weg(road_wt))
	{
		// Trams can only be built on roads, or one tile off a road (to allow for depots and rail connexions).
		return false;
	}

	if(bautyp==luft  &&  (from->get_grund_hang()+to->get_grund_hang()!=0  ||  (from->hat_wege()  &&  from->hat_weg(air_wt)==0)  ||  (to->hat_wege()  &&  to->hat_weg(air_wt)==0))) {
		// absolutely no slopes for runways, neither other ways
		return false;
	}

	bool to_flat = false; // to tile will be flattened
	if(from==to) {
		if((bautyp&tunnel_flag)  &&  !slope_t::is_way(from->get_weg_hang())) {
			return false;
		}
	}
	else {
		// check slopes
		bool ok_slope = from->get_weg_hang() == slope_t::flat  ||  ribi_t::doubles(ribi_type(from->get_weg_hang()))==ribi_t::doubles(ribi_type(zv));
		ok_slope &= to->get_weg_hang() == slope_t::flat  ||  ribi_t::doubles(ribi_type(to->get_weg_hang()))==ribi_t::doubles(ribi_type(zv));

		// try terraforming
		if (!ok_slope) {
			uint8 dummy,to_slope;
			if (  (bautyp & terraform_flag) != 0  &&  from->ist_natur()  &&  to->ist_natur()  &&  check_terraforming(from,to,&dummy,&to_slope) ) {
				to_flat = to_slope == slope_t::flat;
			}
			else {
				// slopes not ok and no terraforming possible
				return false;
			}
		}
	}

	// ok, slopes are ok
	bool ok = true;

	// check scenario conditions
	if (welt->get_scenario()->is_work_allowed_here(player, (bautyp&tunnel_flag ? TOOL_BUILD_TUNNEL : TOOL_BUILD_WAY)|GENERAL_TOOL, bautyp&bautyp_mask, to->get_pos()) != NULL) {
		return false;
	}

	// universal check for elevated things ...
	if(bautyp & elevated_flag) 
	{
		if( to->hat_weg(air_wt)  || 
			welt->lookup_hgt( to_pos ) < welt->get_water_hgt( to_pos )  ||  
			!check_for_leitung( zv, to )  ||  
			(!to->ist_karten_boden()  &&  to->get_typ() != grund_t::monorailboden)  ||  
			to->get_typ() == grund_t::brueckenboden  ||  
			to->get_typ() == grund_t::tunnelboden  ) {
			// no suitable ground below!
			return false;
		}

		//max_elevated_way_building_level

		gebaeude_t *gb = to->get_building();
		if(gb) {
			// citybuilding => do not touch
			const haus_tile_besch_t* tile = gb->get_tile();

			// also check for too high buildings ...
			if (tile->get_besch()->get_level() > welt->get_settings().get_max_elevated_way_building_level())
			{
				return false;
			}

			if(tile->get_background(0,1,0)!=IMG_EMPTY) 
			{
				return false;
			}
			// building above houses is expensive ... avoid it!
			*costs += 4;
		}
		// up to now 'to' and 'from' referred to the ground one height step below the elevated way
		// now get the grounds at the right height
		koord3d pos = to->get_pos() + koord3d( 0, 0, env_t::pak_height_conversion_factor );
		grund_t *to2 = welt->lookup(pos);
		if(to2) {
			if(to2->get_weg_nr(0)) {
				// already an elevated ground here => it will have always a way object, that indicates ownership
				ok = to2->get_typ()==grund_t::monorailboden  &&  check_owner(to2->obj_bei(0)->get_owner(),player);
				ok &= to2->get_weg_nr(0)->get_besch()->get_wtyp()==besch->get_wtyp();
			}
			else {
				ok = to2->find<leitung_t>()==NULL;
			}
			if (!ok) {
				return false;
			}
			to = to2;
		}
		else {
			// simulate empty elevated tile
			to_dummy.set_pos(pos);
			to_dummy.set_grund_hang(to->get_grund_hang());
			to = &to_dummy;
		}

		pos = from->get_pos() + koord3d( 0, 0, env_t::pak_height_conversion_factor );
		grund_t *from2 = welt->lookup(pos);
		if(from2) {
			from = from2;
		}
		else {
			// simulate empty elevated tile
			from_dummy.set_pos(pos);
			from_dummy.set_grund_hang(from->get_grund_hang());
			from = &from_dummy;
		}
		// now 'from' and 'to' point to grounds at the right height
	}

	if(  env_t::pak_height_conversion_factor == 2  )
	{
		// cannot build if conversion factor 2, we aren't powerline and way with maximum speed > 0 or powerline 1 tile below except for roads, waterways and tram lines: but mark those as being a "low bridge" type that only allows some vehicles to pass.
		grund_t *to2 = welt->lookup( to->get_pos() + koord3d(0, 0, -1) );
		if(  to2 && (((bautyp&bautyp_mask)!=leitung && to2->get_weg_nr(0) && to2->get_weg_nr(0)->get_besch()->get_topspeed() > 0 && to2->get_weg_nr(0)->get_besch()->get_waytype() != water_wt && to2->get_weg_nr(0)->get_besch()->get_waytype() != road_wt && to2->get_weg_nr(0)->get_besch()->get_waytype() != tram_wt) || to2->get_leitung())  ) 
		{
			return false;
		}
		// tile above cannot have way unless we are a way (not powerline) with a maximum speed of 0 (and is not a road, waterway or tram line as above), or be surface if we are underground
		to2 = welt->lookup( to->get_pos() + koord3d(0, 0, 1) );
		if(  to2  &&  ((to2->get_weg_nr(0) && ((besch->get_topspeed() > 0 && besch->get_waytype() != water_wt && besch->get_waytype() != road_wt && besch->get_waytype() != tram_wt) || (bautyp&bautyp_mask)==leitung))  ||  (bautyp & tunnel_flag) != 0)  )
		{
			return false;
		}
	}

	// universal check for depots/stops/...
	if(  !check_building( from, zv )  ||  !check_building( to, -zv )  ) {
		return false;
	}

	// universal check for bridges: enter bridges in bridge direction
	if( to->get_typ()==grund_t::brueckenboden ) {
		weg_t *weg=to->get_weg_nr(0);
		if(weg && !ribi_t::is_straight(weg->get_ribi_unmasked()|ribi_type(zv))) {
			return false;
		}
	}
	if( from->get_typ()==grund_t::brueckenboden ) {
		weg_t *weg=from->get_weg_nr(0);
		if(weg && !ribi_t::is_straight(weg->get_ribi_unmasked()|ribi_type(zv))) {
			return false;
		}
	}

	// universal check: do not switch to tunnel through cliffs!
	if( from->get_typ() == grund_t::tunnelboden  &&  to->get_typ() != grund_t::tunnelboden  &&  !from->ist_karten_boden() ) {
		return false;
	}
	if( to->get_typ()==grund_t::tunnelboden  &&  from->get_typ() != grund_t::tunnelboden   &&  !to->ist_karten_boden() ) {
		return false;
	}

	// universal check for crossings
	if (to!=from  &&  (bautyp&bautyp_mask)!=leitung) {
		waytype_t const wtyp = (bautyp == river) ? water_wt : (waytype_t)(bautyp & bautyp_mask);
		if(!check_crossing(zv,to,wtyp,player)  ||  !check_crossing(-zv,from,wtyp,player)) {
			return false;
		}
	}

	// universal check for building under powerlines
	if ((bautyp&bautyp_mask)!=leitung) {
		if (!check_for_leitung(zv,to)  ||  !check_for_leitung(-zv,from)) {
			return false;
		}
	}

	bool fundament = to->get_typ()==grund_t::fundament;

	// now check way specific stuff
	settings_t const& s = welt->get_settings();
	switch(bautyp&bautyp_mask) {

		case strasse:
		{
			const weg_t *str=to->get_weg(road_wt);

			// we allow connection to any road
			ok = (str  ||  !fundament)  &&  !to->ist_wasser();
			if(!ok) {
				return false;
			}
			// check for end/start of bridge or tunnel
			// fail if no proper way exists, or the way's ribi are not 0 and are not matching the slope type
			ribi_t::ribi test_ribi = (str ? str->get_ribi_unmasked() : 0) | ribi_type(zv);
			if(to->get_weg_hang()!=to->get_grund_hang()  &&  (str==NULL  ||  !(ribi_t::is_straight(test_ribi) || test_ribi==0 ))) {
				return false;
			}
			// calculate costs
			*costs = str ? 0 : s.way_count_straight;
			if((str==NULL  &&  to->hat_wege())  ||  (str  &&  to->has_two_ways())) {
				*costs += 4;	// avoid crossings
			}
			if(to->get_weg_hang()!=0  &&  !to_flat) {
				*costs += s.way_count_slope;
			}
		}
		break;

		case schiene:
		default:
		{
			const weg_t *sch=to->get_weg(besch->get_wtyp());
			// extra check for AI construction (not adding to existing tracks!)
			if((bautyp&bot_flag)!=0  &&  (sch  ||  to->get_halt().is_bound())) {
				return false;
			}
			// ok, regular construction here
			// if no way there: check for right ground type, otherwise check owner
			ok = sch == NULL ? (!fundament && !to->ist_wasser()) : check_owner(sch->get_owner(),player) || check_access(sch, player);
			if(!ok) {
				return false;
			}
			// check for end/start of bridge or tunnel
			// fail if no proper way exists, or the way's ribi are not 0 and are not matching the slope type
			ribi_t::ribi test_ribi = (sch ? sch->get_ribi_unmasked() : 0) | ribi_type(zv);
			if(to->get_weg_hang()!=to->get_grund_hang()  &&  (sch==NULL  ||  !(ribi_t::is_straight(test_ribi) || test_ribi==0 ))) {
				return false;
			}
			// calculate costs
			*costs = s.way_count_straight;
			if (!sch) *costs += 1; // only prefer existing rails a little
			if((sch  &&  to->has_two_ways())  ||  (sch==NULL  &&  to->hat_wege())) {
				*costs += 4;	// avoid crossings
			}
			if(to->get_weg_hang()!=0  &&  !to_flat) {
				*costs += s.way_count_slope;
			}
		}
		break;

		case schiene_tram: // Dario: Tramway
		{
			const weg_t *sch=to->get_weg(track_wt);
			// roads are checked in check_crossing
			// if no way there: check for right ground type, otherwise check owner
			ok = sch == NULL ? (!fundament && !to->ist_wasser()) : check_owner(sch->get_owner(),player) || check_access(sch, player);
			// tram track allowed in road tunnels, but only along existing roads / tracks
			if(from!=to) {
				if(from->ist_tunnel()) {
					const ribi_t::ribi ribi = from->get_weg_ribi_unmasked(road_wt)  |  from->get_weg_ribi_unmasked(track_wt)  |  ribi_t::doubles(ribi_type(from->get_grund_hang()));
					ok = ok && ((ribi & ribi_type(zv))==ribi_type(zv));
				}
				if(to->ist_tunnel()) {
					const ribi_t::ribi ribi = to->get_weg_ribi_unmasked(road_wt)  |  to->get_weg_ribi_unmasked(track_wt)  |  ribi_t::doubles(ribi_type(to->get_grund_hang()));
					ok = ok && ((ribi & ribi_type(-zv))==ribi_type(-zv));
				}
			}
			if(ok) {
				// calculate costs
				*costs = s.way_count_straight;
				if (!to->hat_weg(track_wt)) *costs += 1; // only prefer existing rails a little
				// prefer own track
				if(to->hat_weg(road_wt)) {
					*costs += s.way_count_straight;
				}
				if(to->get_weg_hang()!=0  &&  !to_flat) {
					*costs += s.way_count_slope;
				}
			}
		}
		break;

		case leitung:
			ok = !to->ist_wasser()  &&  (to->get_weg(air_wt)==NULL);
			ok &= !(to->ist_tunnel() && to->hat_wege());
			if(to->get_weg_nr(0)!=NULL) {
				// only 90 deg crossings, only a single way
				ribi_t::ribi w_ribi= to->get_weg_nr(0)->get_ribi_unmasked();
				ok &= ribi_t::is_straight(w_ribi)  &&  !ribi_t::is_single(w_ribi)  &&  ribi_t::is_straight(ribi_type(zv))  &&  (w_ribi&ribi_type(zv))==0;
			}
			if(to->has_two_ways()) {
				// only 90 deg crossings, only for trams ...
				ribi_t::ribi w_ribi= to->get_weg_nr(1)->get_ribi_unmasked();
				ok &= ribi_t::is_straight(w_ribi)  &&  !ribi_t::is_single(w_ribi)  &&  ribi_t::is_straight(ribi_type(zv))  &&  (w_ribi&ribi_type(zv))==0;
			}
			// do not connect to other powerlines
			{
				leitung_t *lt = to->get_leitung();
				ok &= (lt==NULL)  || lt->get_owner()->allows_access_to(player->get_player_nr()) || check_owner(player, lt->get_owner());
			}

			if(to->get_typ()!=grund_t::tunnelboden) {
				// only fields are allowed
				if(to->get_typ()!=grund_t::boden) {
					ok &= to->get_typ() == grund_t::fundament && to->find<field_t>();
				}
				// no bridges and monorails here in the air
				ok &= (welt->access(to_pos)->get_boden_in_hoehe(to->get_pos().z+1)==NULL);
			}

			// calculate costs
			if(ok) {
				*costs = s.way_count_straight;
				if(  !to->get_leitung()  ) {
					// extra malus for not following an existing line or going on ways
					*costs += s.way_count_double_curve + (to->hat_wege() ? 8 : 0); // prefer existing powerlines
				}
			}
		break;

		case wasser:
		{
			const weg_t *canal = to->get_weg(water_wt);
			// if no way there: check for right ground type, otherwise check owner
			ok = canal == NULL ? !fundament : check_owner(canal->get_owner(), player) || check_access(canal, player);
			// calculate costs
			if(ok) {
				*costs = to->ist_wasser() ||  canal  ? s.way_count_straight : s.way_count_leaving_road; // prefer water very much
				if(to->get_weg_hang()!=0  &&  !to_flat) {
					*costs += s.way_count_slope * 2;
				}
			}
			break;
		}

		case river:
			if(  to->ist_wasser()  ) {
				ok = true;
				// do not care while in ocean
				*costs = 1;
			}
			else {
				// only downstream
				ok = from->get_pos().z>=to->get_pos().z  &&  (to->hat_weg(water_wt)  ||  !to->hat_wege());
				// calculate costs
				if(ok) {
					// prefer existing rivers:
					*costs = to->hat_weg(water_wt) ? 10 : 10+simrand(s.way_count_90_curve, "bool wegbauer_t::is_allowed_step");
					if(to->get_weg_hang()!=0  &&  !to_flat) {
						*costs += s.way_count_slope * 10;
					}
				}
			}
			break;

		case luft: // hsiegeln: runway
			{
				const weg_t *w = to->get_weg(air_wt);
				if(  w  &&  w->get_besch()->get_styp()==type_runway  &&  besch->get_styp()!=type_runway &&  ribi_t::is_single(w->get_ribi_unmasked())  ) {
					// cannot go over the end of a runway with a taxiway
					return false;
				}
				ok = !to->ist_wasser() && (w  ||  !to->hat_wege())  &&  to->find<leitung_t>()==NULL  &&  !fundament;
				// calculate costs
				*costs = s.way_count_straight;
			}
			break;
	}
	return ok;
}


bool wegbauer_t::check_terraforming( const grund_t *from, const grund_t *to, uint8* new_from_slope, uint8* new_to_slope)
{
	// only for normal green tiles
	const slope_t::type from_slope = from->get_weg_hang();
	const slope_t::type to_slope = to->get_weg_hang();
	const sint8 from_hgt = from->get_hoehe();
	const sint8 to_hgt = to->get_hoehe();
	// we may change slope of a tile if it is sloped already
	if(  (from_slope == slope_t::flat  ||  from->get_hoehe() == welt->get_water_hgt( from->get_pos().get_2d() ))
	  &&  (to_slope == slope_t::flat  ||  to->get_hoehe() == welt->get_water_hgt( to->get_pos().get_2d() ))  ) {
		return false;
	}
	else if(  abs( from_hgt - to_hgt ) <= (besch->has_double_slopes() ? 2 : 1)  ) {
		// extra check for double heights
		if(  abs( from_hgt - to_hgt) == 2  &&  (welt->lookup( from->get_pos() - koord3d(0,0,2) ) != NULL  ||  welt->lookup( from->get_pos() + koord3d(0, 0, 2) ) != NULL
		  ||  welt->lookup( to->get_pos() - koord3d(0, 0, 2) ) != NULL  ||  welt->lookup( to->get_pos() + koord3d(0, 0, 2) ) != NULL)  ) {
			return false;
		}
		// monorail above / tunnel below
		if (welt->lookup(from->get_pos() - koord3d(0,0,1))!=NULL  ||  welt->lookup(from->get_pos() + koord3d(0,0,1))!=NULL
			||  welt->lookup(to->get_pos() - koord3d(0,0,1))!=NULL  ||  welt->lookup(to->get_pos() + koord3d(0,0,1))!=NULL) {
				return false;
		}
		// can safely change slope of at least one of the tiles
		if (new_from_slope == NULL) {
			return true;
		}
		// now calculate new slopes
		assert(new_from_slope);
		assert(new_to_slope);
		// direction of way
		const koord dir = (to->get_pos() - from->get_pos()).get_2d();
		sint8 start  = from_hgt * 2;
		sint8 middle = from_hgt * 2;
		sint8 end    = to_hgt * 2;
		// get 3 heights - start (min start from, but should be same), middle (average end from/average start to), end (min end to)
		if(  dir ==  koord::north  ) {
			start  += corner_sw(from_slope) + corner_se(from_slope);
			middle += corner_ne(from_slope) + corner_nw(from_slope);
			end    += corner_ne(to_slope) + corner_nw(to_slope);
		}
		else if(  dir == koord::east  ) {
			start  += corner_sw(from_slope) + corner_nw(from_slope);
			middle += corner_se(from_slope) + corner_ne(from_slope);
			end    += corner_se(to_slope) + corner_ne(to_slope);
		}
		else if(  dir == koord::south  ) {
			start  += corner_ne(from_slope) + corner_nw(from_slope);
			middle += corner_sw(from_slope) + corner_se(from_slope);
			end    += corner_sw(to_slope) + corner_se(to_slope);
		}
		else if(  dir == koord::west  ) {
			start  += corner_se(from_slope) + corner_ne(from_slope);
			middle += corner_sw(from_slope) + corner_nw(from_slope);
			end    += corner_sw(to_slope) + corner_nw(to_slope);
		}
		// work out intermediate height:
		if(  end == start  ) {
			middle = start;
		}
		else {
			//  end < start   to is way?assert from is_way:middle = end + 1
			//  end > start   to is way?assert from is_way:middle = end - 1
			if(  !slope_t::is_way( to_slope )  ) {
				middle = (start + end) / 2;
			}
		}
		// prevent middle being invalid
		if(  middle >> 1 > from_hgt + 2  ) {
			middle = (from_hgt + 2) * 2;
		}
		if(  middle >> 1 > to_hgt + 2  ) {
			middle = (to_hgt + 2) * 2;
		}
		if(  middle >> 1 < from_hgt  ) {
			middle = from_hgt * 2;
		}
		if(  middle >> 1 < to_hgt  ) {
			middle = to_hgt * 2;
		}
		const uint8 m_from = (middle >> 1) - from_hgt;
		const uint8 m_to = (middle >> 1) - to_hgt;

		// write middle heights
		if(  dir ==  koord::north  ) {
			*new_from_slope = corner_sw(from_slope) + corner_se(from_slope) * 3 + m_from * 9              + m_from * 27;
			*new_to_slope =   m_to                + m_to * 3                + corner_ne(to_slope) * 9   + corner_nw(to_slope) * 27;
		}
		else if(  dir == koord::east  ) {
			*new_from_slope = corner_sw(from_slope) + m_from * 3              + m_from * 9              + corner_nw(from_slope) * 27;
			*new_to_slope =   m_to                + corner_se(to_slope) * 3   + corner_ne(to_slope) * 9   + m_to * 27;
		}
		else if(  dir == koord::south  ) {
			*new_from_slope = m_from              + m_from * 3              + corner_ne(from_slope) * 9 + corner_nw(from_slope) * 27;
			*new_to_slope =   corner_sw(to_slope)   + corner_se(to_slope) * 3   + m_to * 9                + m_to * 27;
		}
		else if(  dir == koord::west  ) {
			*new_from_slope = m_from              + corner_se(from_slope) * 3 + corner_ne(from_slope) * 9 + m_from * 27;
			*new_to_slope =   corner_sw(to_slope)   + m_to * 3                + m_to * 9                + corner_nw(to_slope) * 27;
		}
		return true;
	}
	return false;
}

void wegbauer_t::do_terraforming()
{
	uint32 last_terraformed = terraform_index.get_count();

	FOR(vector_tpl<uint32>, const i, terraform_index) { // index in route
		grund_t *from = welt->lookup(route[i]);
		uint8 from_slope = from->get_grund_hang();

		grund_t *to = welt->lookup(route[i+1]);
		uint8 to_slope = to->get_grund_hang();
		// calculate new slopes
		check_terraforming(from, to, &from_slope, &to_slope);
		bool changed = false;
		// change slope of from
		if(  from_slope != from->get_grund_hang()  ) {
			if(  from_slope == slope_t::raised  ) {
				from->set_hoehe( from->get_hoehe() + 2 );
				from->set_grund_hang( slope_t::flat );
				route[i].z = from->get_hoehe();
			}
			else if(  from_slope != slope_t::raised / 2  ) {
				// bit of a hack to recognise single height slopes shifted up 1
				if(  from_slope > slope_t::raised / 2  &&  slope_t::is_single( from_slope-slope_t::raised / 2 )  ) {
					from->set_hoehe( from->get_hoehe() + 1 );
					from_slope -= slope_t::raised / 2;
					route[i].z = from->get_hoehe();
				}
				from->set_grund_hang( from_slope );
			}
			else {
				from->set_hoehe( from->get_hoehe() + 1 );
				from->set_grund_hang( slope_t::flat );
				route[i].z = from->get_hoehe();
			}
			changed = true;
			if (last_terraformed != i) {
				// charge player
				player_t::book_construction_costs(player, welt->get_settings().cst_set_slope, from->get_pos().get_2d(), ignore_wt);
			}
		}
		// change slope of to
		if(  to_slope != to->get_grund_hang()  ) {
			if(  to_slope == slope_t::raised  ) {
				to->set_hoehe( to->get_hoehe() + 2 );
				to->set_grund_hang( slope_t::flat );
				route[i + 1].z = to->get_hoehe();
			}
			else if(  to_slope != slope_t::raised / 2  ) {
				// bit of a hack to recognise single height slopes shifted up 1
				if(  to_slope > slope_t::raised / 2  &&  slope_t::is_single( to_slope-slope_t::raised / 2 )  ) {
					to->set_hoehe( to->get_hoehe() + 1 );
					to_slope -= slope_t::raised / 2;
					route[i + 1].z = to->get_hoehe();
				}
				to->set_grund_hang(to_slope);
			}
			else {
				to->set_hoehe( to->get_hoehe() + 1);
				to->set_grund_hang(slope_t::flat);
				route[i+1].z = to->get_hoehe();
			}
			changed = true;
			// charge player
			player_t::book_construction_costs(player, welt->get_settings().cst_set_slope, to->get_pos().get_2d(), ignore_wt);
			last_terraformed = i+1; // do not pay twice for terraforming one tile
		}
		// recalc slope image of neighbors
		if (changed) {
			for(uint8 j=0; j<2; j++) {
				for(uint8 x=0; x<2; x++) {
					for(uint8 y=0; y<2; y++) {
						grund_t *gr = welt->lookup_kartenboden(route[i+j].get_2d()+koord(x,y));
						if (gr) {
							gr->calc_image();
						}
					}
				}
			}
		}
	}
}

void wegbauer_t::check_for_bridge(const grund_t* parent_from, const grund_t* from, const vector_tpl<koord3d> &ziel)
{
	// wrong starting slope or tile already occupied with a way ...
	if (!slope_t::is_way(from->get_grund_hang())) {
		return;
	}

	/*
	 * now check existing ways:
	 * no tunnels/bridges at crossings and no track tunnels/bridges on roads (but road tunnels/bridges on tram are allowed).
	 * (keep in mind, that our waytype isn't currently on the tile and will be built later)
	 */
	const weg_t *way0 = from->get_weg_nr(0);
	const weg_t *way1 = from->get_weg_nr(1);
	if(  way0  ) {
		switch(  bautyp&bautyp_mask  ) {
			case schiene_tram:
			case strasse: {
				const weg_t *other = way1;
				if (  way0->get_waytype() != besch->get_wtyp()  ) {
					if (  way1  ) {
						// two different ways
						return;
					}
					other = way0;
				}
				if (  other  ) {
					if (  (bautyp&bautyp_mask) == strasse  ) {
						if (  other->get_waytype() != track_wt  ||  other->get_besch()->get_styp()!=type_tram  ) {
							// road only on tram
							return;
						}
					}
					else {
						if (  other->get_waytype() != road_wt  ) {
							// tram only on road
							return;
						}
					}
				}
			}

			default:
				if (way0->get_waytype()!=besch->get_wtyp()  ||  way1!=NULL) {
					// no other ways allowed
					return;
				}
		}
	}

	const koord zv=from->get_pos().get_2d()-parent_from->get_pos().get_2d();
	const ribi_t::ribi ribi = ribi_type(zv);

	// now check ribis of existing ways
	const ribi_t::ribi wayribi = way0 ? way0->get_ribi_unmasked() | (way1 ? way1->get_ribi_unmasked() : (ribi_t::ribi)ribi_t::none) : (ribi_t::ribi)ribi_t::none;
	if (  wayribi & (~ribi)  ) {
		// curves at bridge start
		return;
	}

	// ok, so now we do a closer investigation
	if(  bruecke_besch  && (  ribi_type(from->get_grund_hang()) == ribi_t::backward(ribi_type(zv))  ||  from->get_grund_hang() == 0  )
		&&  brueckenbauer_t::ist_ende_ok(player, from, besch->get_wtyp(),ribi_t::backward(ribi_type(zv)))  ) {
		// Try a bridge.
		const sint32 cost_difference=besch->get_wartung()>0 ? (bruecke_besch->get_wartung()*4l+3l)/besch->get_wartung() : 16;
		// try eight possible lengths ..
		uint32 min_length = 1;
		for (uint8 i = 0; i < 8 && min_length <= welt->get_settings().way_max_bridge_len; ++i) {
			sint8 bridge_height;
			const char *error = NULL;
			koord3d end = brueckenbauer_t::finde_ende( player, from->get_pos(), zv, bruecke_besch, error, bridge_height, true, min_length );
			const grund_t* gr_end = welt->lookup(end);
			if(  gr_end == NULL) {
				// no valid end point found
				min_length++;
				continue;
			}
			uint32 length = koord_distance(from->get_pos(), end);
			if(!ziel.is_contained(end)  &&  brueckenbauer_t::ist_ende_ok(player, gr_end, besch->get_wtyp(), ribi_type(zv))) {
				// If there is a slope on the starting tile, it's taken into account in is_allowed_step, but a bridge will be flat!
				sint8 num_slopes = (from->get_grund_hang() == slope_t::flat) ? 1 : -1;
				// On the end tile, we haven't to subtract way_count_slope, since is_allowed_step isn't called with this tile.
				num_slopes += (gr_end->get_grund_hang() == slope_t::flat) ? 1 : 0;
				next_gr.append(next_gr_t(welt->lookup(end), length * cost_difference + num_slopes*welt->get_settings().way_count_slope, build_straight | build_tunnel_bridge));
				min_length = length+1;
			}
			else {
				break;
			}
		}
		return;
	}

	if(  tunnel_besch  &&  ribi_type(from->get_grund_hang()) == ribi_type(zv)  ) {
		// uphill hang ... may be tunnel?
		const sint32 cost_difference = besch->get_wartung() > 0 ? (tunnel_besch->get_wartung() * 4l + 3l) / besch->get_wartung() : 16;
		koord3d end = tunnelbauer_t::finde_ende( player, from->get_pos(), zv, tunnel_besch);
		if(  end != koord3d::invalid  &&  !ziel.is_contained(end)  ) {
			uint32 length = koord_distance(from->get_pos(), end);
			next_gr.append(next_gr_t(welt->lookup(end), length * cost_difference, build_straight | build_tunnel_bridge ));
			return;
		}
	}
}


wegbauer_t::wegbauer_t(player_t* player_) : next_gr(32)
{
	n      = 0;
	player     = player_;
	bautyp = strasse;   // kann mit route_fuer() gesetzt werden
	maximum = 2000;// CA $ PER TILE

	keep_existing_ways = false;
	keep_existing_city_roads = false;
	keep_existing_faster_ways = false;
	build_sidewalk = false;
	mark_way_for_upgrade_only = false;
}


/**
 * If a way is built on top of another way, should the type
 * of the former way be kept or replaced (true == keep)
 * @author Hj. Malthaner
 */
void wegbauer_t::set_keep_existing_ways(bool yesno)
{
	keep_existing_ways = yesno;
	keep_existing_faster_ways = false;
}


void wegbauer_t::set_keep_existing_faster_ways(bool yesno)
{
	keep_existing_ways = false;
	keep_existing_faster_ways = yesno;
}


void wegbauer_t::route_fuer(bautyp_t wt, const weg_besch_t *b, const tunnel_besch_t *tunnel, const bruecke_besch_t *br)
{
	bautyp = wt;
	besch = b;
	bruecke_besch = br;
	tunnel_besch = tunnel;
	if(wt&tunnel_flag  &&  tunnel==NULL) {
		dbg->fatal("wegbauer_t::route_fuer()","needs a tunnel description for an underground route!");
	}
	if((wt&bautyp_mask)==luft) {
		wt &= bautyp_mask | bot_flag;
	}
	if(player==NULL) {
		bruecke_besch = NULL;
		tunnel_besch = NULL;
	}
	else if(  bautyp != river  ) {
#ifdef AUTOMATIC_BRIDGES
		if(  bruecke_besch == NULL  ) {
			bruecke_besch = brueckenbauer_t::find_bridge(b->get_wtyp(), 25, welt->get_timeline_year_month());
		}
#endif
#ifdef AUTOMATIC_TUNNELS
		if(  tunnel_besch == NULL  ) {
			tunnel_besch = tunnelbauer_t::find_tunnel(b->get_wtyp(), 25, welt->get_timeline_year_month());
		}
#endif
	}
  DBG_MESSAGE("wegbauer_t::route_fuer()",
         "setting way type to %d, besch=%s, bruecke_besch=%s, tunnel_besch=%s",
         bautyp,
         besch ? besch->get_name() : "NULL",
         bruecke_besch ? bruecke_besch->get_name() : "NULL",
         tunnel_besch ? tunnel_besch->get_name() : "NULL"
         );
}


void get_mini_maxi( const vector_tpl<koord3d> &ziel, koord3d &mini, koord3d &maxi )
{
	mini = maxi = ziel[0];
	FOR(vector_tpl<koord3d>, const& current, ziel) {
		if( current.x < mini.x ) {
			mini.x = current.x;
		} else if( current.x > maxi.x ) {
			maxi.x = current.x;
		}
		if( current.y < mini.y ) {
			mini.y = current.y;
		} else if( current.y > maxi.y ) {
			maxi.y = current.y;
		}
		if( current.z < mini.z ) {
			mini.z = current.z;
		} else if( current.z > maxi.z ) {
			maxi.z = current.z;
		}
	}
}


/* this routine uses A* to calculate the best route
 * beware: change the cost and you will mess up the system!
 * (but you can try, look at simuconf.tab)
 */
sint32 wegbauer_t::intern_calc_route(const vector_tpl<koord3d> &start, const vector_tpl<koord3d> &ziel)
{
	// we clear it here probably twice: does not hurt ...
	route.clear();
	terraform_index.clear();

	// check for existing koordinates
	bool has_target_ground = false;
	FOR(vector_tpl<koord3d>, const& i, ziel) {
		has_target_ground |= welt->lookup(i) != 0;
	}
	if( !has_target_ground ) {
		return -1;
	}

	// calculate the minimal cuboid containing 'ziel'
	koord3d mini, maxi;
	get_mini_maxi( ziel, mini, maxi );

	// memory in static list ...
	if(!route_t::MAX_STEP)
	{
		route_t::INIT_NODES(welt->get_settings().get_max_route_steps(), welt->get_size());
	}

	static binary_heap_tpl <route_t::ANode *> queue;

	// get exclusively a tile list
	route_t::ANode *nodes;
	uint8 ni = route_t::GET_NODES(&nodes);

	// initialize marker field
	marker_t& marker = marker_t::instance(welt->get_size().x, welt->get_size().y);

	// clear the queue (should be empty anyhow)
	queue.clear();

	// some obj for the search
	grund_t *to;
	koord3d gr_pos;	// just the last valid pos ...
	route_t::ANode *tmp=NULL;
	uint32 step = 0;
	const grund_t* gr=NULL;

	FOR(vector_tpl<koord3d>, const& i, start) {
		gr = welt->lookup(i);

		// is valid ground?
		sint32 dummy;
		if( !gr || !is_allowed_step(gr,gr,&dummy) ) {
			// DBG_MESSAGE("wegbauer_t::intern_calc_route()","cannot start on (%i,%i,%i)",start.x,start.y,start.z);
			continue;
		}
		tmp = &(nodes[step]);
		step ++;
		if (route_t::max_used_steps < step)
			route_t::max_used_steps = step;

		tmp->parent = NULL;
		tmp->gr = gr;
		tmp->f = calc_distance(i, mini, maxi);
		tmp->g = 0;
		tmp->dir = 0;
		tmp->count = 0;

		queue.insert(tmp);
	}

	if( queue.empty() ) {
		// no valid ground to start.
		// release nodes after last use of any node (like tmp in getting the cost)
		route_t::RELEASE_NODES(ni);
		return -1;
	}

	INT_CHECK("wegbauer 347");

	// to speed up search, but may not find all shortest ways
	uint32 min_dist = 99999999;

//DBG_MESSAGE("route_t::itern_calc_route()","calc route from %d,%d,%d to %d,%d,%d",ziel.x, ziel.y, ziel.z, start.x, start.y, start.z);
	do {
		route_t::ANode *test_tmp = queue.pop();

		if(marker.test_and_mark(test_tmp->gr)) {
			// we were already here on a faster route, thus ignore this branch
			// (trading speed against memory consumption)
			continue;
		}

		tmp = test_tmp;
		gr = tmp->gr;
		gr_pos = gr->get_pos();

#ifdef DEBUG_ROUTES
DBG_DEBUG("insert to close","(%i,%i,%i)  f=%i",gr->get_pos().x,gr->get_pos().y,gr->get_pos().z,tmp->f);
#endif

		// already there
		if(  ziel.is_contained(gr_pos)  ||  tmp->g>maximum) {
			// we added a target to the closed list: we are finished
			break;
		}

		// the four possible directions plus any additional stuff due to already existing brides plus new ones ...
		next_gr.clear();

		// only one direction allowed ...
		const ribi_t::ribi straight_dir = tmp->parent!=NULL ? ribi_type(gr->get_pos().get_2d()-tmp->parent->gr->get_pos().get_2d()) : (ribi_t::ribi)ribi_t::all;

		// test directions
		// .. use only those that are allowed by current slope
		// .. do not go backward
		const ribi_t::ribi slope_dir = (slope_t::is_way_ns(gr->get_weg_hang()) ? ribi_t::northsouth : ribi_t::none) | (slope_t::is_way_ew(gr->get_weg_hang()) ? ribi_t::eastwest : ribi_t::none);
		const ribi_t::ribi test_dir = (tmp->count & build_straight)==0  ?  slope_dir  & ~ribi_t::backward(straight_dir)
		                                                                :  straight_dir;

		// testing all four possible directions
		for(ribi_t::ribi r=1; (r&16)==0; r<<=1) {
			if((r & test_dir)==0) {
				// not allowed to go this direction
				continue;
			}

			bool do_terraform = false;
			const koord zv(r);
			if(!gr->get_neighbour(to,invalid_wt,r)  ||  !check_slope(gr, to)) {
				// slopes do not match
				// terraforming enabled?
				if (bautyp==river  ||  (bautyp & terraform_flag) == 0) {
					continue;
				}
				// check terraforming (but not in curves)
				if (gr->get_grund_hang()==0  ||  (tmp->parent!=NULL  &&  tmp->parent->parent!=NULL  &&  r==straight_dir)) {
					to = welt->lookup_kartenboden(gr->get_pos().get_2d() + zv);
					if (to==NULL  ||  (check_slope(gr, to)  &&  gr->get_vmove(r)!=to->get_vmove(ribi_t::backward(r)))) {
						continue;
					}
					else {
						do_terraform = true;
					}
				}
				else {
					continue;
				}
			}

			// something valid?
			if(marker.is_marked(to)) {
				continue;
			}

			sint32 new_cost = 0;
			bool is_ok = is_allowed_step(gr,to,&new_cost);

			if(is_ok) {
				// now add it to the array ...
				next_gr.append(next_gr_t(to, new_cost, do_terraform ? build_straight | terraform : 0));
			}
			else if(tmp->parent!=NULL  &&  r==straight_dir  &&  (tmp->count & build_tunnel_bridge)==0) {
				// try to build a bridge or tunnel here, since we cannot go here ...
				check_for_bridge(tmp->parent->gr,gr,ziel);
			}
		}

		// now check all valid ones ...
		FOR(vector_tpl<next_gr_t>, const& r, next_gr) {
			to = r.gr;

			// new values for cost g
			uint32 new_g = tmp->g + r.cost;

			settings_t const& s = welt->get_settings();
			// check for curves (usually, one would need the lastlast and the last;
			// if not there, then we could just take the last
			uint8 current_dir;
			if(tmp->parent!=NULL) {
				current_dir = ribi_type( tmp->parent->gr->get_pos().get_2d(), to->get_pos().get_2d() );
				if(tmp->dir!=current_dir) {
					new_g += s.way_count_curve;
					if(tmp->parent->dir!=tmp->dir) {
						// discourage double turns
						new_g += s.way_count_double_curve;
					}
					else if(ribi_t::is_perpendicular(tmp->dir,current_dir)) {
						// discourage v turns heavily
						new_g += s.way_count_90_curve;
					}
				}
				else if(bautyp==leitung  &&  ribi_t::is_bend(current_dir)) {
					new_g += s.way_count_double_curve;
				}
				// extra malus leave an existing road after only one tile
				waytype_t const wt = besch->get_wtyp();
				if (tmp->parent->gr->hat_weg(wt) && !gr->hat_weg(wt) && to->hat_weg(wt)) {
					// but only if not straight track
					if(!ribi_t::is_straight(tmp->dir)) {
						new_g += s.way_count_leaving_road;
					}
				}
			}
			else {
				 current_dir = ribi_type( gr->get_pos().get_2d(), to->get_pos().get_2d() );
			}

			const uint32 new_dist = calc_distance( to->get_pos(), mini, maxi );

			// special check for kinks at the end
			if(new_dist==0  &&  current_dir!=tmp->dir) {
				// discourage turn on last tile
				new_g += s.way_count_double_curve;
			}

			if (new_dist == 0 && r.flag & terraform) {
				// no terraforming near target
				continue;
			}
			if(new_dist<min_dist) {
				min_dist = new_dist;
			}
			else if(new_dist>min_dist+50) {
				// skip, if too far from current minimum tile
				// will not find some ways, but will be much faster ...
				// also it will avoid too big detours, which is probably also not the way, the builder intended
				continue;
			}


			const uint32 new_f = new_g+new_dist;

			if((step&0x03)==0) {
				INT_CHECK( "wegbauer 1347" );
#ifdef DEBUG_ROUTES
				if((step&1023)==0) {reliefkarte_t::get_karte()->calc_map();}
#endif
			}

			// not in there or taken out => add new
			route_t::ANode *k=&(nodes[step]);
			step++;
			if (route_t::max_used_steps < step)
				route_t::max_used_steps = step;

			k->parent = tmp;
			k->gr = to;
			k->g = new_g;
			k->f = new_f;
			k->dir = current_dir;
			// count is unused here, use it as flag-variable instead
			k->count = r.flag;

			queue.insert( k );

#ifdef DEBUG_ROUTES
DBG_DEBUG("insert to open","(%i,%i,%i)  f=%i",to->get_pos().x,to->get_pos().y,to->get_pos().z,k->f);
#endif
		}

	} while (!queue.empty() && step < route_t::MAX_STEP);

#ifdef DEBUG_ROUTES
DBG_DEBUG("wegbauer_t::intern_calc_route()","steps=%i  (max %i) in route, open %i, cost %u",step,route_t::MAX_STEP,queue.get_count(),tmp->g);
#endif
	INT_CHECK("wegbauer 194");

	long cost = -1;
//DBG_DEBUG("reached","%i,%i",tmp->pos.x,tmp->pos.y);
	// target reached?
	if(  !ziel.is_contained(gr->get_pos())  ||  tmp->parent==NULL  ) {
	}
	else if(  step>=route_t::MAX_STEP  ) {
		dbg->warning("wegbauer_t::intern_calc_route()","Too many steps (%i>=max %i) in route (too long/complex)",step,route_t::MAX_STEP);
	}
	else {
		cost = tmp->g;
		// reached => construct route
		while(tmp != NULL) {
			route.append(tmp->gr->get_pos());
			if (tmp->count & terraform) {
				terraform_index.append(route.get_count()-1);
			}
			tmp = tmp->parent;
		}
	}

	// release nodes after last use of any node (like tmp in getting the cost)
	route_t::RELEASE_NODES(ni);
	return cost;
}


void wegbauer_t::intern_calc_straight_route(const koord3d start, const koord3d ziel)
{
	bool ok=true;

	const grund_t* test_bd = welt->lookup(start);
	if (test_bd == NULL) {
		// not building
		return;
	}
	sint32 dummy_cost;
	if(!is_allowed_step(test_bd,test_bd,&dummy_cost)) {
		// no legal ground to start ...
		return;
	}
	if ((bautyp&tunnel_flag) && !test_bd->ist_tunnel()) {
		// start tunnelbuilding in tunnels
		return;
	}
	test_bd = welt->lookup(ziel);
	if((bautyp&tunnel_flag)==0  &&  test_bd  &&  !is_allowed_step(test_bd,test_bd,&dummy_cost)) {
		// ... or to end
		return;
	}
	// we have to reach target height if no tunnel building or (target ground does not exists or is underground).
	// in full underground mode if there is no tunnel under cursor, kartenboden gets selected
	const bool target_3d = (bautyp&tunnel_flag)==0  ||  test_bd==NULL  ||  !test_bd->ist_karten_boden();

	koord3d pos=start;

	route.clear();
	route.append(start);
	terraform_index.clear();
	bool check_terraform = start.x==ziel.x  ||  start.y==ziel.y;

	while(pos.get_2d()!=ziel.get_2d()  &&  ok) {

		bool do_terraform = false;
		// shortest way
		ribi_t::ribi diff;
		if(abs(pos.x-ziel.x)>=abs(pos.y-ziel.y)) {
			diff = (pos.x>ziel.x) ? ribi_t::west : ribi_t::east;
		}
		else {
			diff = (pos.y>ziel.y) ? ribi_t::north : ribi_t::south;
		}
		if(bautyp&tunnel_flag) {
			// create fake tunnel grounds if needed
			bool bd_von_new = false, bd_nach_new = false;
			grund_t *bd_von = welt->lookup(pos);
			if(  bd_von == NULL ) {
				bd_von = new tunnelboden_t(pos, slope_t::flat);
				bd_von_new = true;
			}
			// take care of slopes
			pos.z = bd_von->get_vmove(diff);

			// check next tile
			grund_t *bd_nach = welt->lookup(pos + diff);
			if(  !bd_nach  ) {
				// check for slope down ...
				bd_nach = welt->lookup(pos + diff + koord3d(0,0,-1));
				if(  !bd_nach  ) {
					bd_nach = welt->lookup(pos + diff + koord3d(0,0,-2));
				}
				if(  bd_nach  &&  bd_nach->get_weg_hang() == slope_t::flat  ) {
					// Don't care about _flat_ tunnels below.
					bd_nach = NULL;
				}
			}
			if(  bd_nach == NULL  ){
				bd_nach = new tunnelboden_t(pos + diff, slope_t::flat);
				bd_nach_new = true;
			}
			// check for tunnel and right slope
			ok = ok && bd_nach->ist_tunnel() && bd_nach->get_vmove(ribi_t::backward(diff))==pos.z;
			// all other checks are done here (crossings, stations etc)
			ok = ok && is_allowed_step(bd_von, bd_nach, &dummy_cost);

			// advance position
			pos = bd_nach->get_pos();

			// check new tile: ground must be above tunnel and below sea
			grund_t *gr = welt->lookup_kartenboden(pos.get_2d());
			ok = ok  &&  (gr->get_hoehe() > pos.z)  &&  (!gr->ist_wasser()  ||  (welt->lookup_hgt(pos.get_2d()) > pos.z) );

			if (bd_von_new) {
				delete bd_von;
			}
			if (bd_nach_new) {
				delete bd_nach;
			}
		}
		else {
			grund_t *bd_von = welt->lookup(pos);
			if (bd_von==NULL) {
				ok = false;
			}
			else
			{
				grund_t *bd_nach = NULL;
				if (!bd_von->get_neighbour(bd_nach, invalid_wt, diff) ||  !check_slope(bd_von, bd_nach)) {
					// slopes do not match
					// terraforming enabled?
					if (bautyp==river  ||  (bautyp & terraform_flag) == 0) {
						break;
					}
					// check terraforming (but not in curves)
					ok = false;
					if (check_terraform) {
						bd_nach = welt->lookup_kartenboden(bd_von->get_pos().get_2d() + diff);
						if (bd_nach==NULL  ||  (check_slope(bd_von, bd_nach)  &&  bd_von->get_vmove(diff)!=bd_nach->get_vmove(ribi_t::backward(diff)))) {
							ok = false;
						}
						else {
							do_terraform = true;
							ok = true;
						}
					}
				}
				// allowed ground?
				ok = ok  &&  bd_nach  &&  is_allowed_step(bd_von,bd_nach,&dummy_cost);
				if (ok) {
					pos = bd_nach->get_pos();
				}
			}
			check_terraform = pos.x==ziel.x  ||  pos.y==ziel.y;
		}

		route.append(pos);
		if (do_terraform) {
			terraform_index.append(route.get_count()-2);
		}
		DBG_MESSAGE("wegbauer_t::calc_straight_route()","step %s = %i",koord(diff).get_str(),ok);
	}
	ok = ok && ( target_3d ? pos==ziel : pos.get_2d()==ziel.get_2d() );

	// we can built a straight route?
	if(ok) {
DBG_MESSAGE("wegbauer_t::intern_calc_straight_route()","found straight route max_n=%i",get_count()-1);
	}
	else {
		route.clear();
		terraform_index.clear();
	}
}


// special for starting/landing runways
bool wegbauer_t::intern_calc_route_runways(koord3d start3d, const koord3d ziel3d)
{
	route.clear();
	terraform_index.clear();

	const koord start=start3d.get_2d();
	const koord ziel=ziel3d.get_2d();
	// check for straight line!
	const ribi_t::ribi ribi = ribi_type( start, ziel );
	if(  !ribi_t::is_straight(ribi)  ) {
		// only straight runways!
		return false;
	}
	const ribi_t::ribi ribi_straight = ribi_t::doubles(ribi);

	// not too close to the border?
	if(	 !(welt->is_within_limits(start-koord(5,5))  &&  welt->is_within_limits(start+koord(5,5)))  ||
		 !(welt->is_within_limits(ziel-koord(5,5))  &&  welt->is_within_limits(ziel+koord(5,5)))  ) {
		if(player==welt->get_active_player()) {
			create_win( new news_img("Zu nah am Kartenrand"), w_time_delete, magic_none);
			return false;
		}
	}

	// now try begin and endpoint
	const koord zv(ribi);
	// end start
	const grund_t *gr = welt->lookup_kartenboden(start);
	const weg_t *weg = gr->get_weg(air_wt);
	if(weg  &&  !ribi_t::is_straight(weg->get_ribi()|ribi_straight)  ) {
		// cannot connect with curve at the end
		return false;
	}
	if(  weg  &&  weg->get_besch()->get_styp()==type_flat  ) {
		//  could not continue taxiway with runway
		return false;
	}
	// check end
	gr = welt->lookup_kartenboden(ziel);
	weg = gr->get_weg(air_wt);

	if(  weg  &&  weg->get_besch()->get_styp()==type_flat  ) {
		//  could not continue taxiway with runway
		return false;
	}
	// now try a straight line with no crossings and no curves at the end
	const int dist=koord_distance( ziel, start );
	grund_t *from = welt->lookup_kartenboden(start);
	for(  int i=0;  i<=dist;  i++  ) {
		grund_t *to = welt->lookup_kartenboden(start+zv*i);
		sint32 dummy;
		if (!is_allowed_step(from, to, &dummy)) {
			return false;
		}
		weg = to->get_weg(air_wt);
		weg_t* previous_way = from->get_weg(air_wt);
		if( !previous_way && weg  &&  weg->get_besch()->get_styp()==type_runway  &&  (ribi_t::is_threeway(weg->get_ribi_unmasked()|ribi_straight))  &&  (weg->get_ribi_unmasked()|ribi_straight)!=ribi_t::all  ) {
			// only fourway crossings of runways allowed, no threeways => fail
			return false;
		}
		
		if(!previous_way && to->get_pos() == ziel3d && weg && !ribi_t::is_straight(weg->get_ribi()|ribi_straight)  ) {
		// cannot connect with curve at the end
		return false;
	}
		from = to;
	}
	// now we can build here
	route.clear();
	terraform_index.clear();
	route.resize(dist + 2);
	for(  int i=0;  i<=dist;  i++  ) {
		route.append(welt->lookup_kartenboden(start + zv * i)->get_pos());
	}
	return true;
}


/*
 * calc_straight_route (maximum one curve, including diagonals)
 */
void wegbauer_t::calc_straight_route(koord3d start, const koord3d ziel)
{
	DBG_MESSAGE("wegbauer_t::calc_straight_route()","from %d,%d,%d to %d,%d,%d",start.x,start.y,start.z, ziel.x,ziel.y,ziel.z );
	if(bautyp==luft  &&  besch->get_styp()==type_runway) {
		// these are straight anyway ...
		intern_calc_route_runways(start, ziel);
	}
	else {
		intern_calc_straight_route(start,ziel);
		if (route.empty()) {
			intern_calc_straight_route(ziel,start);
		}
	}
}


void wegbauer_t::calc_route(const koord3d &start, const koord3d &ziel)
{
	vector_tpl<koord3d> start_vec(1), ziel_vec(1);
	start_vec.append(start);
	ziel_vec.append(ziel);
	calc_route(start_vec, ziel_vec);
}

/* calc_route
 *
 */
void wegbauer_t::calc_route(const vector_tpl<koord3d> &start, const vector_tpl<koord3d> &ziel)
{
#ifdef DEBUG_ROUTES
uint32 ms = dr_time();
#endif
	INT_CHECK("simbau 740");

	if(bautyp==luft  &&  besch->get_styp()==type_runway) {
		assert( start.get_count() == 1  &&  ziel.get_count() == 1 );
		intern_calc_route_runways(start[0], ziel[0]);
	}
	else if(bautyp==river) {
		assert( start.get_count() == 1  &&  ziel.get_count() == 1 );
		// river only go downwards => start and end are clear ...
		if(  start[0].z > ziel[0].z  ) {
			intern_calc_route( start, ziel );
		}
		else {
			intern_calc_route( ziel, start );
		}
		while (route.get_count()>1  &&  welt->lookup(route[0])->get_grund_hang() == slope_t::flat  &&  welt->lookup(route[1])->ist_wasser()) {
			// remove leading water ...
			route.remove_at(0);
		}
	}
	else {
		keep_existing_city_roads |= (bautyp&bot_flag)!=0;
		sint32 cost2 = intern_calc_route(start, ziel);
		INT_CHECK("wegbauer 1165");

		if(cost2<0) {
			// not successful: try backwards
			intern_calc_route(ziel,start);
			return;
		}

#ifdef REVERSE_CALC_ROUTE_TOO
		vector_tpl<koord3d> route2(0);
		vector_tpl<uint32> terraform_index2(0);
		swap(route, route2);
		swap(terraform_index, terraform_index2);
		long cost = intern_calc_route(ziel, start);
		INT_CHECK("wegbauer 1165");

		// the cheaper will survive ...
		if(  cost2 < cost  ||  cost < 0  ) {
			swap(route, route2);
			swap(terraform_index, terraform_index2);
		}
#endif
	}
	INT_CHECK("wegbauer 778");
#ifdef DEBUG_ROUTES
DBG_MESSAGE("calc_route::calc_route", "took %i ms",dr_time()-ms);
#endif
}

void wegbauer_t::baue_tunnel_und_bruecken()
{
	if(bruecke_besch==NULL  &&  tunnel_besch==NULL) {
		return;
	}
	// check for bridges and tunnels (no tunnel/bridge at last/first tile)
	for(uint32 i=1; i<get_count()-2; i++) {
		koord d = (route[i + 1] - route[i]).get_2d();
		koord zv = koord (sgn(d.x), sgn(d.y));

		// ok, here is a gap ...
		if(d.x > 1 || d.y > 1 || d.x < -1 || d.y < -1) {

			if(d.x*d.y!=0) {
				dbg->error("wegbauer_t::baue_tunnel_und_bruecken()","cannot built a bridge between %s (n=%i, max_n=%i) and %s", route[i].get_str(),i,get_count()-1,route[i+1].get_str());
				continue;
			}

			DBG_MESSAGE("wegbauer_t::baue_tunnel_und_bruecken", "built bridge %p between %s and %s", bruecke_besch, route[i].get_str(), route[i + 1].get_str());

			const grund_t* start = welt->lookup(route[i]);
			const grund_t* end   = welt->lookup(route[i + 1]);

			if(start->get_weg_hang()!=start->get_grund_hang()) {
				// already a bridge/tunnel there ...
				continue;
			}
			if(end->get_weg_hang()!=end->get_grund_hang()) {
				// already a bridge/tunnel there ...
				continue;
			}

			if(start->get_grund_hang()==0  ||  start->get_grund_hang()==slope_type(zv*(-1))) {
				// bridge here, since the route is saved backwards, we have to build it at the posterior end
				brueckenbauer_t::baue( player, route[i+1], bruecke_besch);
			}
			else {
				// tunnel
				tunnelbauer_t::baue( player, route[i].get_2d(), tunnel_besch, true );
			}
			INT_CHECK( "wegbauer 1584" );
		}
		// Don't build short tunnels/bridges if they block a bridge/tunnel behind!
		else if(  bautyp != leitung  &&  koord_distance(route[i + 2], route[i + 1]) == 1  ) {
			grund_t *gr_i = welt->lookup(route[i]);
			grund_t *gr_i1 = welt->lookup(route[i+1]);
			if(  gr_i->get_weg_hang() != gr_i->get_grund_hang()  ||  gr_i1->get_weg_hang() != gr_i1->get_grund_hang()  ) {
				// Here is already a tunnel or a bridge.
				continue;
			}
			slope_t::type h = gr_i->get_weg_hang();
			waytype_t const wt = besch->get_wtyp();
			if(h!=slope_t::flat  &&  slope_t::opposite(h)==gr_i1->get_weg_hang()) {
				// either a short mountain or a short dip ...
				// now: check ownership
				weg_t *wi = gr_i->get_weg(wt);
				weg_t *wi1 = gr_i1->get_weg(wt);
				if(wi->get_owner()==player  &&  wi1->get_owner()==player) {
					// we are the owner
					if(  h != slope_type(zv)  ) {
						// its a bridge
						if( bruecke_besch ) {
							wi->set_ribi(ribi_type(h));
							wi1->set_ribi(ribi_type(slope_t::opposite(h)));
							brueckenbauer_t::baue( player, route[i], bruecke_besch);
						}
					}
					else if( tunnel_besch ) {
						// make a short tunnel
						wi->set_ribi(ribi_type(slope_t::opposite(h)));
						wi1->set_ribi(ribi_type(h));
						tunnelbauer_t::baue( player, route[i].get_2d(), tunnel_besch, true, besch );
					}
					INT_CHECK( "wegbauer 1584" );
				}
			}
		}
	}
}


/*
 * returns the amount needed to built this way
 * author prissi
 */
sint64 wegbauer_t::calc_costs()
{
	if(besch->is_mothballed())
	{
		// It is free to mothball a way. No other calculations are needed, as mothballed types
		// can only be built as downgrades.
		return 0ll;
	}
	
	sint64 costs=0;
	koord3d offset = koord3d( 0, 0, bautyp & elevated_flag ? env_t::pak_height_conversion_factor : 0 );

	sint32 single_cost = 0;
	sint32 new_speedlimit;

	if( bautyp&tunnel_flag ) {
		assert( tunnel_besch );
		new_speedlimit = tunnel_besch->get_topspeed();
	}
	else {
		new_speedlimit = besch->get_topspeed();
	}

	// calculate costs for terraforming
	uint32 last_terraformed = terraform_index.get_count();

	FOR(vector_tpl<uint32>, const i, terraform_index) { // index in route
		grund_t *from = welt->lookup(route[i]);
		uint8 from_slope = from->get_grund_hang();

		grund_t *to = welt->lookup(route[i+1]);
		uint8 to_slope = to->get_grund_hang();
		// calculate new slopes
		check_terraforming(from, to, &from_slope, &to_slope);
		// change slope of from
		if (from_slope != from->get_grund_hang()) {
			if (last_terraformed != i) {
				costs -= welt->get_settings().cst_set_slope;
			}
		}
		// change slope of to
		if (to_slope != to->get_grund_hang()) {
			costs -= welt->get_settings().cst_set_slope;
			last_terraformed = i+1; // do not pay twice for terraforming one tile
		}
	}

	for(uint32 i=0; i<get_count(); i++) {
		sint32 old_playerseedlimit = -1;
		sint64 replace_cost = 0;
		bool upgrading = false;

		const grund_t* gr = welt->lookup(route[i] + offset);
		if( gr ) {
			if( bautyp&tunnel_flag ) {
				const tunnel_t *tunnel = gr->find<tunnel_t>();
				assert( tunnel );
				if( tunnel->get_besch() == tunnel_besch ) {
					continue; // Nothing to pay on this tile.
				}
				old_playerseedlimit = tunnel->get_besch()->get_topspeed();
				single_cost = tunnel_besch->get_preis();
			}
			else {
				single_cost = besch->get_preis();
				if(  besch->get_wtyp() == powerline_wt  ) {
					if( leitung_t *lt=gr->get_leitung() ) {
						old_playerseedlimit = lt->get_besch()->get_topspeed();
					}
				}
				else {
					if (weg_t const* const weg = gr->get_weg(besch->get_wtyp())) {
						replace_cost = weg->get_besch()->get_upgrade_group() == besch->get_upgrade_group() ? besch->get_way_only_cost() : besch->get_preis(); 
						upgrading = true;
						if( weg->get_besch() == besch ) {
							continue; // Nothing to pay on this tile.
						}
						if(  besch->get_styp() == 0  &&  weg->get_besch()->get_styp() == type_tram  &&  gr->get_weg_nr(0)->get_waytype() == road_wt  ) {
							// Don't replace a tram on a road with a normal track.
							continue;
						}
						old_playerseedlimit = weg->get_besch()->get_topspeed();
					}
					else if (besch->get_wtyp()==water_wt  &&  gr->ist_wasser()) {
						old_playerseedlimit = new_speedlimit;
					}
				}
			}

			sint64 forge_cost = upgrading ? 0 : welt->get_settings().get_forge_cost(besch->get_waytype());
			const koord3d pos = gr->get_pos();

			if(!upgrading && !(bautyp & tunnel_flag) && !(bautyp & elevated_flag) && route.get_count() > 1)
			{
				for(int n = 0; n < 8; n ++)
				{
					const koord kn = pos.get_2d().neighbours[n] + pos.get_2d();
					if(!welt->is_within_grid_limits(kn))
					{
						continue;
					}
					const koord3d kn3d(kn, welt->lookup_hgt(kn));
					grund_t* to = welt->lookup(kn3d);
					const weg_t* connecting_way = to ? to->get_weg(besch->get_waytype()) : NULL;
					const ribi_t::ribi connecting_ribi = connecting_way ? connecting_way->get_ribi() : ribi_t::all;
					const ribi_t::ribi ribi = route.get_short_ribi(i);
					if(route.is_contained(kn3d) || (ribi_t::is_single(ribi) && ribi_t::is_single(connecting_ribi)))
					{
						continue;
					}
					const grund_t* gr_neighbour = welt->lookup_kartenboden(kn);
					if(gr_neighbour && gr_neighbour->get_weg(besch->get_waytype()))
					{
						// This is a parallel way of the same type - reduce the forge cost.
						forge_cost *= welt->get_settings().get_parallel_ways_forge_cost_percentage(besch->get_waytype());
						forge_cost /= 100ll;
						break;
					}
				}
			}

			single_cost += forge_cost;

			const obj_t* obj = gr->obj_bei(0);
			if(!upgrading && (obj == NULL || obj->get_owner() != player))
			{
				// Only add the cost of the land if the player does not
				// already own this land.

				// get_land_value returns a *negative* value.
				single_cost -= welt->get_land_value(gr->get_pos());
			}

			// eventually we have to remove trees
			for(  uint8 i=0;  i<gr->get_top();  i++  ) {
				obj_t *obj = gr->obj_bei(i);
				switch(obj->get_typ()) {
					case obj_t::baum:
						costs -= welt->get_settings().cst_remove_tree;
						break;
					case obj_t::groundobj:
						costs += ((groundobj_t *)obj)->get_besch()->get_preis();
						break;
					default: break;
				}
			}
		}
		if(upgrading) 
		{
			costs += replace_cost;
		}
		else if(!gr)
		{
			// No ground -building a new elevated way. Do not add the land value as it is still possible to build underneath an elevated way.
			costs += (welt->get_settings().get_forge_cost(besch->get_waytype()) + besch->get_preis());
		}
		else
		{
			costs += max(replace_cost, single_cost);
		}

		// last tile cannot be start of tunnel/bridge
		if(i+1<get_count()) {
			koord d = (route[i + 1] - route[i]).get_2d();
			// ok, here is a gap ... => either bridge or tunnel
			if(d.x > 1 || d.y > 1 || d.x < -1 || d.y < -1) {
				koord zv = koord (sgn(d.x), sgn(d.y));
				const grund_t* start = welt->lookup(route[i]);
				const grund_t* end   = welt->lookup(route[i + 1]);
				if(start->get_weg_hang()!=start->get_grund_hang()) {
					// already a bridge/tunnel there ...
					continue;
				}
				if(end->get_weg_hang()!=end->get_grund_hang()) {
					// already a bridge/tunnel there ...
					continue;
				}
				if(start->get_grund_hang()==0  ||  start->get_grund_hang()==slope_type(zv*(-1))) {
					// bridge
					costs += bruecke_besch->get_preis()*(sint64)(koord_distance(route[i], route[i+1])+1);
					continue;
				}
				else {
					// tunnel
					costs += tunnel_besch->get_preis()*(sint64)(koord_distance(route[i], route[i+1])+1);
					continue;
				}
			}
		}
	}

	DBG_MESSAGE("wegbauer_t::calc_costs()","construction estimate: %f",costs/100.0);
	return costs;
}


// Builds the inside of tunnels
bool wegbauer_t::baue_tunnelboden()
{
	sint64 cost = 0;
	for(uint32 i=0; i<get_count(); i++) {

		grund_t* gr = welt->lookup(route[i]);

		if (besch == NULL)
		{
			besch = tunnel_besch->get_weg_besch();
		}
		if(besch ==NULL) {
			// now we search a matching way for the tunnels top speed
			// ignore timeline to get consistent results
			besch = wegbauer_t::weg_search( tunnel_besch->get_waytype(), tunnel_besch->get_topspeed(), 0, type_flat );
		}

		if(gr==NULL) {
			// make new tunnelboden
			tunnelboden_t* tunnel = new tunnelboden_t(route[i], 0);
			welt->access(route[i].get_2d())->boden_hinzufuegen(tunnel);
			if(tunnel_besch->get_waytype()!=powerline_wt) {
				weg_t *weg = weg_t::alloc(tunnel_besch->get_waytype());
				weg->set_besch(besch);
				tunnel->neuen_weg_bauen(weg, route.get_ribi(i), player);
				tunnel->obj_add(new tunnel_t(route[i], player, tunnel_besch));
				const slope_t::type hang = gr ? gr->get_weg_hang() : slope_t::flat;
				if(hang != slope_t::flat) 
				{
					const uint slope_height = (hang & 7) ? 1 : 2;
					if(slope_height == 1)
					{
						weg->set_max_speed(min(besch->get_topspeed_gradient_1(), tunnel_besch->get_topspeed_gradient_1()));
					}
					else
					{
						weg->set_max_speed(min(besch->get_topspeed_gradient_2(), tunnel_besch->get_topspeed_gradient_2()));
					}
				}
				else
				{
					weg->set_max_speed(min(besch->get_topspeed(), tunnel_besch->get_topspeed()));
				}
				weg->set_max_axle_load(tunnel_besch->get_max_axle_load());
				weg->add_way_constraints(besch->get_way_constraints());
			} else {
				tunnel->obj_add(new tunnel_t(route[i], player, tunnel_besch));
				leitung_t *lt = new leitung_t(tunnel->get_pos(), player);
				lt->set_besch(besch);
				tunnel->obj_add( lt );
				lt->finish_rd();
				player_t::add_maintenance( player, -lt->get_besch()->get_wartung(), powerline_wt);
			}
			tunnel->calc_image();
			cost -= tunnel_besch->get_preis();
			player_t::add_maintenance( player,  tunnel_besch->get_wartung(), tunnel_besch->get_finance_waytype() );
		}
		else if(gr->get_typ()==grund_t::tunnelboden) {
			// check for extension only ...
			if(tunnel_besch->get_waytype()!=powerline_wt) {
				gr->weg_erweitern( tunnel_besch->get_waytype(), route.get_ribi(i) );

				tunnel_t *tunnel = gr->find<tunnel_t>();
				assert( tunnel );
				// take the faster way
				if(  !keep_existing_faster_ways  ||  (tunnel->get_besch()->get_topspeed() < tunnel_besch->get_topspeed())  ) {

					tunnel->set_besch(tunnel_besch);
					weg_t *weg = gr->get_weg(tunnel_besch->get_waytype());
					weg->set_besch(besch);
					weg->set_max_speed(tunnel_besch->get_topspeed());
					// respect max speed of catenary
					wayobj_t const* const wo = gr->get_wayobj(tunnel_besch->get_waytype());
					if (wo  &&  wo->get_besch()->get_topspeed() < weg->get_max_speed()) {
						weg->set_max_speed( wo->get_besch()->get_topspeed() );
					}
					gr->calc_image();

					cost -= tunnel_besch->get_preis();
				}
			} else {
				leitung_t *lt = gr->get_leitung();
				if(!lt) {
					lt = new leitung_t(gr->get_pos(), player);
					lt->set_besch(besch);
					gr->obj_add( lt );
				} else {
					lt->leitung_t::finish_rd();	// only change powerline aspect
					player_t::add_maintenance( player, -lt->get_besch()->get_wartung(), powerline_wt);
				}
			}

			tunnel_t *tunnel = gr->find<tunnel_t>();
			assert( tunnel );
			// take the faster way
			if(  !keep_existing_faster_ways  ||  (tunnel->get_besch()->get_topspeed() < tunnel_besch->get_topspeed())  ) {
				tunnel->set_besch(tunnel_besch);
				weg_t *weg = gr->get_weg(tunnel_besch->get_waytype());
				weg->set_besch(besch);
				const slope_t::type hang = gr->get_weg_hang();
				if(hang != slope_t::flat) 
				{
					const uint slope_height = (hang & 7) ? 1 : 2;
					if(slope_height == 1)
					{
						weg->set_max_speed(min(besch->get_topspeed_gradient_1(), tunnel_besch->get_topspeed_gradient_1()));
					}
					else
					{
						weg->set_max_speed(min(besch->get_topspeed_gradient_2(), tunnel_besch->get_topspeed_gradient_2()));
					}
				}
				else
				{
					weg->set_max_speed(min(besch->get_topspeed(), tunnel_besch->get_topspeed()));
				}
				weg->set_max_axle_load(tunnel_besch->get_max_axle_load());
				// respect max speed of catenary
				wayobj_t const* const wo = gr->get_wayobj(tunnel_besch->get_waytype());
				if (wo  &&  wo->get_besch()->get_topspeed() < weg->get_max_speed()) {
					weg->set_max_speed( wo->get_besch()->get_topspeed() );
				}
				gr->calc_image();
				// respect speed limit of crossing
				weg->count_sign();

				cost -= tunnel_besch->get_preis();
			}
		}
	}
	player_t::book_construction_costs(player, cost, route[0].get_2d(), tunnel_besch->get_waytype());
	return true;
}



void wegbauer_t::baue_elevated()
{
	FOR(koord3d_vector_t, & i, route) {
		planquadrat_t* const plan = welt->access(i.get_2d());

		grund_t* const gr0 = plan->get_boden_in_hoehe(i.z);
		i.z += env_t::pak_height_conversion_factor;
		grund_t* const gr  = plan->get_boden_in_hoehe(i.z);

		if(gr==NULL) {
			slope_t::type hang = gr0 ? gr0->get_grund_hang() : 0;
			// add new elevated ground
			monorailboden_t* const monorail = new monorailboden_t(i, hang);
			plan->boden_hinzufuegen(monorail);
			monorail->calc_image();
		}
	}
}



void wegbauer_t::baue_strasse()
{
	// This is somewhat strange logic --neroden
	if ( player != NULL && build_sidewalk && player->is_public_service() ) {
		player = NULL;
	}

	// init undo
	if(player!=NULL) {
		// Some roads have no owner, so we must check for an owner
		player->init_undo(road_wt,get_count());
	}

	for(  uint32 i=0;  i<get_count();  i++  ) {
		if((i&3)==0) {
			INT_CHECK( "wegbauer 1584" );
		}

		const koord k = route[i].get_2d();
		grund_t* gr = welt->lookup(route[i]);
		sint64 cost = 0;

		if(mark_way_for_upgrade_only)
		{
			// Only mark the way for upgrading when it needs renewing: do not build anything now.
			weg_t* const way = gr->get_weg(besch->get_wtyp());
			if((bautyp & elevated_flag) == (way->get_besch()->get_styp() == type_elevated))
			{			
				way->set_replacement_way(besch);
			}
		}
		else
		{
			bool extend = gr->weg_erweitern(road_wt, route.get_short_ribi(i));

			if(extend) {
				weg_t * weg = gr->get_weg(road_wt);

				// keep faster ways or if it is the same way ... (@author prissi)
				if(  weg->get_besch()==besch  ||  keep_existing_ways
					||  (  keep_existing_city_roads  &&  weg->hat_gehweg()  )
					||  (  ( keep_existing_faster_ways || (!player->is_public_service() && weg->is_public_right_of_way())) &&  ! ( besch->is_at_least_as_good_as(weg->get_besch()) )  )
					||  (  player!=NULL  &&  weg-> is_deletable(player)!=NULL  )
					||  (  gr->get_typ()==grund_t::monorailboden && (bautyp&elevated_flag)==0  )
					) {
					//nothing to be done
	//DBG_MESSAGE("wegbauer_t::baue_strasse()","nothing to do at (%i,%i)",k.x,k.y);
				}
				else
				{
					if(besch->get_upgrade_group() == weg->get_besch()->get_upgrade_group())
					{
						cost = besch->get_way_only_cost();
					}
					else
					{
						// Cost of downgrading is the cost of the inferior way (was previously the higher of the two costs in 10.15 and earlier, from Standard).
						cost = besch->get_preis();
					}

					weg->set_besch(besch);
					// respect speed limit of crossing
					weg->count_sign();
					// respect max speed of catenary
					wayobj_t const* const wo = gr->get_wayobj(besch->get_wtyp());
					if (wo  &&  wo->get_besch()->get_topspeed() < weg->get_max_speed()) {
						weg->set_max_speed( wo->get_besch()->get_topspeed() );
					}

					// For now, have the city fix adoption/sidewalk issues during road upgrade.
					// These issues arise from city expansion and contraction, so reconsider this
					// after city limits work better.
					bool city_adopts_this = weg->should_city_adopt_this(player);
					if(build_sidewalk || weg->hat_gehweg() || city_adopts_this) {
						strasse_t *str = static_cast<strasse_t *>(weg);
						str->set_gehweg(true);
						weg->set_public_right_of_way();
					}

					if (city_adopts_this) {
						weg->set_owner(NULL);
					} else {
						weg->set_owner(player);
						// Set maintenance costs here
						// including corrections for diagonals.
						weg->finish_rd();
					}
				}
			}
			else {
				// make new way
				const obj_t* obj = gr->obj_bei(0);
				strasse_t * str = new strasse_t();

				str->set_besch(besch);
				if (build_sidewalk) {
					str->set_gehweg(build_sidewalk);
					str->set_public_right_of_way();
				}
				cost -= gr->neuen_weg_bauen(str, route.get_short_ribi(i), player, &route) + besch->get_preis();
				// respect speed limit of crossing
				str->count_sign();
				// prissi: into UNDO-list, so we can remove it later
				if(player!=NULL) 
				{
					// intercity roads have no owner, so we must check for an owner
					player->add_undo( route[i] );
				}
			
				if(player == NULL || player->is_public_service())
				{
					// If there is no owner here, this is an inter-city road built by the game on initiation;
					// therefore, set it as a public right of way.
					// Similarly, if this is owned by the public player, set it as a public right of way.
					str->set_public_right_of_way();
				}
			}
			gr->calc_image();	// because it may be a crossing ...
			
			reliefkarte_t::get_karte()->calc_map_pixel(k);
			player_t::book_construction_costs(player, -cost, k, road_wt);
		} 
		welt->set_recheck_road_connexions();
	} // for
}


void wegbauer_t::baue_schiene()
{
	if(get_count() > 1) {
		// init undo
		player->init_undo(besch->get_wtyp(), get_count());

		// built tracks
		for(  uint32 i=0;  i<get_count();  i++  ) {
			sint64 cost = 0;
			grund_t* gr = welt->lookup(route[i]);
			ribi_t::ribi ribi = route.get_short_ribi(i);

			if(gr->get_typ()==grund_t::wasser) {
				// not building on the sea ...
				continue;
			}

			if(mark_way_for_upgrade_only)
			{
				// Only mark the way for upgrading when it needs renewing: do not build anything now.
				weg_t* const way = gr->get_weg(besch->get_wtyp());
				if((bautyp & elevated_flag) == (way->get_besch()->get_styp() == type_elevated))
				{			
					way->set_replacement_way(besch);
				}
			}
			else
			{
				bool const extend = gr->weg_erweitern(besch->get_wtyp(), ribi);

				if(extend) {
					weg_t* const weg = gr->get_weg(besch->get_wtyp());
					bool change_besch = true;

					// do not touch fences, tram way etc. if there is already same way with different type
					// keep faster ways or if it is the same way ... (@author prissi)
					if (weg->get_besch() == besch																	||
							(besch->get_styp() == 0 && weg->get_besch()->get_styp() == type_tram && gr->has_two_ways())     ||
							keep_existing_ways                                                                      ||
							(player != NULL && weg-> is_deletable(player) != NULL)											||
							(keep_existing_faster_ways && !(besch->is_at_least_as_good_as(weg->get_besch())) )		||
							(gr->get_typ() == grund_t::monorailboden && !(bautyp & elevated_flag)  &&  gr->get_weg_nr(0)->get_waytype()==besch->get_wtyp()))
					{
						//nothing to be done
						change_besch = false;
					}

					// build tram track over crossing -> remove crossing
					if(  gr->has_two_ways()  &&  besch->get_styp()==type_tram  &&  weg->get_besch()->get_styp() != type_tram  ) {
						if(  crossing_t *cr = gr->find<crossing_t>(2)  ) {
							// change to tram track
							cr->mark_image_dirty( cr->get_image(), 0);
							cr->cleanup(player);
							delete cr;
							change_besch = true;
							// tell way we have no crossing any more, restore speed limit
							gr->get_weg_nr(0)->clear_crossing();
							gr->get_weg_nr(0)->set_besch( gr->get_weg_nr(0)->get_besch() );
							gr->get_weg_nr(1)->clear_crossing();
						}
					}

					if(  change_besch  ) {
						// we take ownership => we take care to maintain the roads completely ...
						player_t *p = weg->get_owner();
						cost -= weg->get_besch()->get_upgrade_group() == besch->get_upgrade_group() ? besch->get_way_only_cost() : besch->get_preis();
						weg->set_besch(besch);
						if(besch->is_mothballed())
						{
							// Free to downgrade to mothballed ways
							cost = 0;
						}
						// respect max speed of catenary
						wayobj_t const* const wo = gr->get_wayobj(besch->get_wtyp());
						if (wo  &&  wo->get_besch()->get_topspeed() < weg->get_max_speed()) {
							weg->set_max_speed( wo->get_besch()->get_topspeed() );
						}
						const wayobj_t* wayobj = gr->get_wayobj(weg->get_waytype());
						if(wayobj != NULL)
						{
							weg->add_way_constraints(wayobj->get_besch()->get_way_constraints());
						}					
						weg->set_owner(player);
						// respect speed limit of crossing
						weg->count_sign();
					}
				}
				else 
				{
					const obj_t* obj = gr->obj_bei(0);
				
					weg_t* const sch = weg_t::alloc(besch->get_wtyp());
					sch->set_pos(gr->get_pos());
					sch->set_besch(besch);
					const wayobj_t* wayobj = gr->get_wayobj(sch->get_waytype());
					if(wayobj != NULL)
					{
						sch->add_way_constraints(wayobj->get_besch()->get_way_constraints());
					}
					cost = gr->neuen_weg_bauen(sch, ribi, player, &route) - besch->get_preis();
					// respect speed limit of crossing
					sch->count_sign();
					// connect canals to sea
					if(  besch->get_wtyp() == water_wt  ) {
						for(  int j = 0;  j < 4;  j++  ) {
							grund_t *sea = welt->lookup_kartenboden( route[i].get_2d() + koord::nsew[j] );
							if(  sea  &&  sea->ist_wasser()  ) {
								gr->weg_erweitern( water_wt, ribi_t::nsew[j] );
								sea->calc_image();
							}
						}
					}
					// prissi: into UNDO-list, so we can remove it later
					player->add_undo( route[i] );
				}

				player_t::book_construction_costs(player, cost, gr->get_pos().get_2d(), besch->get_finance_waytype());
			}

			gr->calc_image();
			reliefkarte_t::get_karte()->calc_map_pixel( gr->get_pos().get_2d() );

			if((i&3)==0) 
			{
				INT_CHECK( "wegbauer 1584" );
			}
		}
	}
}



void wegbauer_t::baue_leitung()
{
	if(  get_count() < 1  ) {
		return;
	}

	// no undo
	player->init_undo(powerline_wt,get_count());

	for(  uint32 i=0;  i<get_count();  i++  ) {
		grund_t* gr = welt->lookup(route[i]);

		leitung_t* lt = gr->get_leitung();
		bool build_powerline = false;
		// ok, really no lt here ...
		if(lt==NULL) {
			if(gr->ist_natur()) {
				// remove trees etc.
				sint64 cost = gr->remove_trees();
				player_t::book_construction_costs(player, -cost, gr->get_pos().get_2d(), powerline_wt);
			}
			lt = new leitung_t(route[i], player );
			gr->obj_add(lt);

			// prissi: into UNDO-list, so we can remove it later
			player->add_undo( route[i] );
			build_powerline = true;
		}
		else {
			// modernize the network
			if( !keep_existing_faster_ways  ||  lt->get_besch()->get_topspeed() < besch->get_topspeed()  ) {
				build_powerline = true;
				player_t::add_maintenance( lt->get_owner(),  -lt->get_besch()->get_wartung(), powerline_wt );
			}
		}
		if (build_powerline) {
			lt->set_besch(besch);
			player_t::book_construction_costs(player, -besch->get_preis(), gr->get_pos().get_2d(), powerline_wt);
			// this adds maintenance
			lt->leitung_t::finish_rd();
			reliefkarte_t::get_karte()->calc_map_pixel( gr->get_pos().get_2d() );
		}

		if((i&3)==0) {
			INT_CHECK( "wegbauer 1584" );
		}
	}
}



// this can drive any river, even a river that has max_speed=0
class fluss_test_driver_t : public test_driver_t
{
	bool check_next_tile(const grund_t* gr) const { return gr->get_weg_ribi_unmasked(water_wt)!=0; }
	virtual ribi_t::ribi get_ribi(const grund_t* gr) const { return gr->get_weg_ribi_unmasked(water_wt); }
	virtual waytype_t get_waytype() const { return invalid_wt; }
	virtual int get_cost(const grund_t *, const sint32, koord) { return 1; }
	virtual bool is_target(const grund_t *cur,const grund_t *) { return cur->ist_wasser()  &&  cur->get_grund_hang()==slope_t::flat; }
};


// make a river
void wegbauer_t::baue_fluss()
{
	/* since the constraints of the wayfinder ensures that a river flows always downwards
	 * we can assume that the first tiles are the ocean.
	 * Usually the wayfinder would find either direction!
	 * route.front() tile at the ocean, route.back() the spring of the river
	 */

	// Do we join an other river?
	uint32 start_n = 0;
	for(  uint32 idx=start_n;  idx<get_count();  idx++  ) {
		if(  welt->lookup(route[idx])->hat_weg(water_wt)  ||  welt->lookup(route[idx])->get_hoehe() == welt->get_water_hgt(route[idx].get_2d())  ) {
			start_n = idx;
		}
	}
	if(  start_n == get_count()-1  ) {
		// completely joined another river => nothing to do
		return;
	}

	// first check then lower riverbed
	const sint8 start_h = route[start_n].z;
	uint32 end_n = get_count();
	uint32 i = start_n;
	while(i<get_count()) {
		// first find all tiles that are on the same level as tile i
		// and check whether we can lower all of them
		// if lowering fails we do not continue river building
		bool ok = true;
		uint32 j;
		for(j=i; j<get_count() &&  ok; j++) {
			// one step higher?
			if (route[j].z > route[i].z) break;
			// check
			ok = welt->can_ebne_planquadrat(NULL, route[j].get_2d(), max(route[j].z-1, start_h));
		}
		// now lower all tiles that have the same height as tile i
		for(uint32 k=i; k<j; k++) {
			welt->ebne_planquadrat(NULL, route[k].get_2d(), max(route[k].z-1, start_h));
		}
		if (!ok) {
			end_n = j;
			break;
		}
		i = j;
	}
	// nothing to built ?
	if (start_n >= end_n-1) {
		return;
	}

	// now build the river
	grund_t *gr_first = NULL;
	for(  uint32 i=start_n;  i<end_n;  i++  ) {
		grund_t* gr = welt->lookup_kartenboden(route[i].get_2d());
		if(  gr_first == NULL) {
			gr_first = gr;
		}
		if(  gr->get_typ()!=grund_t::wasser  ) {
			// get direction
			ribi_t::ribi ribi = i<end_n-1 ? route.get_short_ribi(i) : ribi_type(route[i-1]-route[i]);
			bool extend = gr->weg_erweitern(water_wt, ribi);
			if(  !extend  ) {
				weg_t *sch=weg_t::alloc(water_wt);
				sch->set_besch(besch);
				gr->neuen_weg_bauen(sch, ribi, NULL);
			}
		}
	}
	gr_first->calc_image(); // to calculate ribi of water tiles

	// we will make rivers gradually larger by stepping up their width
	if(  env_t::river_types>1  &&  start_n<get_count()) {
		/* since we will stop at the first crossing with an existent river,
		 * we cannot make sure, we have the same destination;
		 * thus we use the routefinder to find the sea
		 */
		route_t to_the_sea;
		fluss_test_driver_t river_tester;
		if(to_the_sea.find_route(welt, welt->lookup_kartenboden(route[start_n].get_2d())->get_pos(), &river_tester, 0, ribi_t::all, 0, 1, 0, 0x7FFFFFFF, false)) {
			FOR(koord3d_vector_t, const& i, to_the_sea.get_route()) {
				if (weg_t* const w = welt->lookup(i)->get_weg(water_wt)) {
					int type;
					for(  type=env_t::river_types-1;  type>0;  type--  ) {
						// lookup type
						if(  w->get_besch()==alle_wegtypen.get(env_t::river_type[type])  ) {
							break;
						}
					}
					// still room to expand
					if(  type>0  ) {
						// thus we enlarge
						w->set_besch( alle_wegtypen.get(env_t::river_type[type-1]) );
						if(w->get_besch()->get_max_axle_load() > 0)
						{
							// It does not make sense for unnavigable rivers to be public rights of way.
							w->set_public_right_of_way();
						}
						w->calc_image();
					}
				}
			}
		}
	}
}



void wegbauer_t::baue()
{
	if(get_count()<2  ||  get_count() > maximum) {
DBG_MESSAGE("wegbauer_t::baue()","called, but no valid route.");
		// no valid route here ...
		return;
	}
	DBG_MESSAGE("wegbauer_t::baue()", "type=%d max_n=%d start=%d,%d end=%d,%d", bautyp, get_count() - 1, route.front().x, route.front().y, route.back().x, route.back().y);

#ifdef DEBUG_ROUTES
long ms=dr_time();
#endif

	if(!mark_way_for_upgrade_only)
	{
		if ( (bautyp&terraform_flag)!=0  &&  (bautyp&(tunnel_flag|elevated_flag))==0  &&  bautyp!=river) {
			// do the terraforming
			do_terraforming();
		}
		// first add all new underground tiles ... (and finished if successful)
		if(bautyp&tunnel_flag) {
			baue_tunnelboden();
			return;
		}

		// add elevated ground for elevated tracks
		if(bautyp&elevated_flag) {
			baue_elevated();
		}
	}


INT_CHECK("simbau 1072");
	switch(bautyp&bautyp_mask) {
		case wasser:
		case schiene:
		case schiene_tram: // Dario: Tramway
		case monorail:
		case maglev:
		case narrowgauge:
		case luft:
			DBG_MESSAGE("wegbauer_t::baue", "schiene");
			baue_schiene();
			break;
		case strasse:
			baue_strasse();
			DBG_MESSAGE("wegbauer_t::baue", "strasse");
			break;
		case leitung:
			baue_leitung();
			break;
		case river:
			baue_fluss();
			break;
		default:
			break;
	}

	INT_CHECK("simbau 1087");
	baue_tunnel_und_bruecken();

	INT_CHECK("simbau 1087");

#ifdef DEBUG_ROUTES
DBG_MESSAGE("wegbauer_t::baue", "took %u ms", dr_time() - ms );
#endif
}



/*
 * This function calculates the distance of pos to the cuboid
 * spanned up by mini and maxi.
 * The result is already weighted according to
 * welt->get_settings().get_way_count_{straight,slope}().
 */
uint32 wegbauer_t::calc_distance( const koord3d &pos, const koord3d &mini, const koord3d &maxi )
{
	uint32 dist = 0;
	if( pos.x < mini.x ) {
		dist += mini.x - pos.x;
	} else if( pos.x > maxi.x ) {
		dist += pos.x - maxi.x;
	}
	if( pos.y < mini.y ) {
		dist += mini.y - pos.y;
	} else if( pos.y > maxi.y ) {
		dist += pos.y - maxi.y;
	}
	settings_t const& s = welt->get_settings();
	dist *= s.way_count_straight;
	if( pos.z < mini.z ) {
		dist += (mini.z - pos.z) * s.way_count_slope;
	} else if( pos.z > maxi.z ) {
		dist += (pos.z - maxi.z) * s.way_count_slope;
	}
	return dist;
}

bool wegbauer_t::check_access(const weg_t* way, const player_t* player) const
{
	return way && (way->is_public_right_of_way() || way->get_owner() == NULL || way->get_owner() == player || player == NULL || way->get_owner()->allows_access_to(player->get_player_nr()));
}
