/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef SIMLINEMGMT_H
#define SIMLINEMGMT_H


#include "linehandle_t.h"
#include "simtypes.h"
#include "tpl/vector_tpl.h"

class loadsave_t;
class schedule_t;
class player_t;
class schedule_list_gui_t;

class simlinemgmt_t
{
public:
	~simlinemgmt_t();

	/*
	 * add a line
	 * @author hsiegeln
	 */
	void add_line(linehandle_t new_line);

	/*
	 * delete a line
	 * @author hsiegeln
	 */
	void delete_line(linehandle_t line);

	/// Used for takeovers
	void deregister_line(linehandle_t line);

	/*
	 * update a line -> apply updated schedule to all convoys
	 * @author hsiegeln
	 */
	static void update_line(linehandle_t line, bool do_not_renew_stops = false);

	/*
	 * load or save the linemanagement
	 */
	void rdwr(loadsave_t * file, player_t * player);

	/*
	 * sort the lines by name
	 */
	void sort_lines();

	/*
	 * will register all stops for all lines
	 */
	void register_all_stops();

	/*
	 * called after game is fully loaded;
	 */
	void finish_rd();

	void rotate90( sint16 y_size );

	void new_month();

	/**
	 * creates a line with an empty schedule
	 * @author hsiegeln
	 */
	linehandle_t create_line(int ltype, player_t * player);

	/**
	 * Creates a line and sets its schedule
	 * @author prissi
	 */
	linehandle_t create_line(int ltype, player_t * player, schedule_t * schedule);


	/**
	 * If there was a line with id=0 map it to non-zero handle
	 */
	linehandle_t get_line_with_id_zero() const {return line_with_id_zero; }

	/*
	 * fill the list with all lines of a certain type
	 * type == simline_t::line will return all lines
	 */
	void get_lines(int type, vector_tpl<linehandle_t>* lines, uint8 freight_type_bits = 0, bool show_empty_line = false) const;

	// Added by : Knightly
	// Purpose	: Return all managed lines
	const vector_tpl<linehandle_t> &get_all_lines() const { return all_managed_lines; }

	uint32 get_line_count() const { return all_managed_lines.get_count(); }

	/**
	 * Will open the line management window and offer information about the line
	 * @author isidoro
	 */
	void show_lineinfo(player_t *player, linehandle_t line);

	vector_tpl<linehandle_t> const& get_line_list() const { return all_managed_lines; }

private:
	vector_tpl<linehandle_t> all_managed_lines;

	linehandle_t line_with_id_zero;
};

#endif
