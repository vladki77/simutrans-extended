/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_SETTINGS_FRAME_H
#define GUI_SETTINGS_FRAME_H


#include "gui_frame.h"
#include "components/gui_tab_panel.h"
#include "components/gui_button.h"
#include "components/gui_scrollpane.h"

#include "settings_stats.h"
#include "components/action_listener.h"

class settings_t;


/**
 * All messages since the start of the program
 * @author prissi
 */
class settings_frame_t : public gui_frame_t, action_listener_t
{
private:
	settings_t* sets;
	gui_tab_panel_t	tabs;

	settings_general_stats_t general;
	gui_scrollpane_t scrolly_general;
	settings_display_stats_t display;
	gui_scrollpane_t scrolly_display;
	settings_economy_stats_t economy;
	gui_scrollpane_t scrolly_economy;
	settings_routing_stats_t routing;
	gui_scrollpane_t scrolly_routing;
	settings_costs_stats_t   costs;
	gui_scrollpane_t scrolly_costs;
	settings_climates_stats_t climates;
	gui_scrollpane_t scrolly_climates;

	gui_tab_panel_t	 tabs_extended;
	settings_extended_general_stats_t exp_general;
	gui_scrollpane_t scrolly_exp_general;
	settings_extended_revenue_stats_t exp_revenue;
	gui_scrollpane_t scrolly_exp_revenue;

	button_t revert_to_default, revert_to_last_save;

public:
	settings_frame_t(settings_t*);

	/**
	 * Set the window associated helptext
	 * @return the filename for the helptext, or NULL
	 * @author Hj. Malthaner
	 */
	const char *get_help_filename() const OVERRIDE {return "settings.txt";}

	/**
	* resize window in response to a resize event
	* @author Hj. Malthaner
	*/
	void resize(const scr_coord delta) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	// does not work during new world dialogue
	virtual bool has_sticky() const OVERRIDE { return false; }

	bool infowin_event(event_t const*) OVERRIDE;
};

#endif
