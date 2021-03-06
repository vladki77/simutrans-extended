/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_SCENARIO_FRAME_H
#define GUI_SCENARIO_FRAME_H


#include "savegame_frame.h"
#include "../utils/cbuffer_t.h"



class scenario_frame_t : public savegame_frame_t
{
private:
	cbuffer_t path;

protected:
	/**
	 * Action that's started by the press of a button.
	 * @author Hansj�rg Malthaner
	 */
	virtual bool item_action(const char *fullpath);

	/**
	 * Action, started after X-Button pressing
	 * @author V. Meyer
	 */
	virtual bool del_action(const char *f) { return item_action(f); }

	// returns extra file info
	virtual const char *get_info(const char *fname);

	// true, if valid
	virtual bool check_file( const char *filename, const char *suffix );
public:
	/**
	* Set the window associated helptext
	* @return the filename for the helptext, or NULL
	* @author Hj. Malthaner
	*/
	virtual const char * get_help_filename() const { return "scenario.txt"; }

	scenario_frame_t();
};

#endif
