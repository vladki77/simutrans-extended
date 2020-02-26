/*
 * Copyright (c) 1997 - 2001 Hansj�rg Malthaner
 *
 * This file is part of the Simutrans project under the artistic licence.
 * (see licence.txt)
 */

#include <stdio.h>
#include <string.h>

#include "../simworld.h"
#include "../simobj.h"
#include "../utils/simrandom.h"
#include "wolke.h"

#include "../dataobj/loadsave.h"

#include "../tpl/vector_tpl.h"

vector_tpl<const skin_desc_t *>wolke_t::all_clouds(0);

bool wolke_t::register_desc(const skin_desc_t* desc)
{
	// avoid duplicates with same name
	FOR(vector_tpl<skin_desc_t const*>, & i, all_clouds) {
		if (strcmp(i->get_name(), desc->get_name()) == 0) {
			i = desc;
			return true;
		}
	}
	return all_clouds.append_unique( desc );
}



wolke_t::wolke_t(koord3d pos, sint8 x_off, sint8 y_off, sint16 h_off, sint16 speed, sint16 life, const skin_desc_t* desc ) :
#ifdef INLINE_OBJ_TYPE
    obj_no_info_t(obj_t::sync_wolke, pos)
#else
    obj_no_info_t(pos)
#endif
{
	cloud_nr = all_clouds.index_of(desc);
	smoke_height = h_off;
	smoke_speed = speed;
	smoke_life = life;
	base_y_off = y_off;
	set_xoff( x_off );
	set_yoff( clamp( y_off - h_off, -128, 127 ));
	purchase_time = 0;
}



wolke_t::~wolke_t()
{
	mark_image_dirty( get_image(), 0 );
	if(  purchase_time != smoke_life - 1 ) {
		welt->sync_way_eyecandy.remove( this );
	}
}


wolke_t::wolke_t(loadsave_t* const file) :
#ifdef INLINE_OBJ_TYPE
	obj_no_info_t(obj_t::sync_wolke)
#else
	obj_no_info_t()
#endif
{
	rdwr(file);
}


image_id wolke_t::get_image() const
{
	const skin_desc_t *desc = all_clouds[cloud_nr];
	return desc->get_image_id( (purchase_time*desc->get_count())/smoke_life);
}



void wolke_t::rdwr(loadsave_t *file)
{
	// not saving clouds! (and loading only for compatibility)
	assert(file->is_loading());

	obj_t::rdwr( file );

	cloud_nr = 0;
	purchase_time = 0;

	uint32 ldummy = 0;
	file->rdwr_long(ldummy);

	uint16 dummy = 0;
	file->rdwr_short(dummy);
	file->rdwr_short(dummy);

	// do not remove from this position, since there will be nothing
	obj_t::set_flag(obj_t::not_on_map);
}



sync_result wolke_t::sync_step(uint32 delta_t)
{
	const image_id old_img = get_image();

	purchase_time += delta_t;
	if (purchase_time >= smoke_life - 1) {
		// delete wolke ...
		purchase_time = smoke_life - 1;
		return SYNC_DELETE;
	}
	const image_id new_img = get_image();

	// move cloud up
	const sint8 new_yoff = clamp( base_y_off - smoke_height - ((purchase_time * OBJECT_OFFSET_STEPS * smoke_speed) >> 12), -128, 127 );
	if (new_yoff != get_yoff() || new_img != old_img) {
		// move cloud randomly sideways - be consistent with airplanes - wind blows from NE (right)
		const sint8 new_xoff = get_xoff() - smoke_speed * WIND_SPEED;
		// move/change cloud ... (happens much more often than image change => image change will be always done when drawing)
		if (!get_flag(obj_t::dirty)) {
			set_flag(obj_t::dirty);
			mark_image_dirty(old_img, 0);
			if (new_img != old_img) {
				mark_image_dirty(new_img, 0);
			}
		}
		set_yoff(new_yoff);
		set_xoff(new_xoff);
	}
	return SYNC_OK;
}


// called during map rotation
void wolke_t::rotate90()
{
	// most basic: rotate coordinate
	koord3d new_pos = get_pos();
	new_pos.rotate90( welt->get_size().y-1 );
	set_pos(new_pos);

	// rotate the base offset
	sint8 new_xoff = -2 * base_y_off;
	base_y_off = get_xoff()/2;
	set_xoff(new_xoff);

	// .. and recalc height offsets
	set_yoff( clamp( base_y_off - smoke_height - ((purchase_time * OBJECT_OFFSET_STEPS * smoke_speed) >> 12), -128, 127 ));
}

/***************************** just for compatibility, the old raucher and smoke clouds *********************************/

raucher_t::raucher_t(loadsave_t *file) : 
#ifdef INLINE_OBJ_TYPE
	obj_t(obj_t::raucher)
#else
	obj_t()
#endif
{
	assert(file->is_loading());
	obj_t::rdwr( file );
	const char *s = NULL;
	file->rdwr_str(s);
	free(const_cast<char *>(s));

	// do not remove from this position, since there will be nothing
	obj_t::set_flag(obj_t::not_on_map);
}


async_wolke_t::async_wolke_t(loadsave_t *file) : 
#ifdef INLINE_OBJ_TYPE
	obj_t(obj_t::async_wolke)
#else
	obj_t()
#endif
{
	// not saving clouds!
	assert(file->is_loading());

	obj_t::rdwr( file );

	uint32 dummy;
	file->rdwr_long(dummy);

	// do not remove from this position, since there will be nothing
	obj_t::set_flag(obj_t::not_on_map);
}
