/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "labellist_frame_t.h"
#include "labellist_stats_t.h"

#include "../dataobj/translator.h"
#include "../simcolor.h"


/**
 * This variable defines the sort order (ascending or descending)
 * Values: 1 = ascending, 2 = descending)
 * @author Markus Weber
 */
bool labellist_frame_t::sortreverse = false;
bool labellist_frame_t::filter_state = true;

/**
 * This variable defines by which column the table is sorted
 * Values: 0 = label name
 *         1 = label koord
 *         2 = label owner
 * @author Markus Weber
 */
labellist::sort_mode_t labellist_frame_t::sortby = labellist::by_name;
static uint8 default_sortmode = 0;

const char *labellist_frame_t::sort_text[labellist::SORT_MODES] = {
	"hl_btn_sort_name",
	"koord",
	"player"
};

labellist_frame_t::labellist_frame_t() :
	gui_frame_t( translator::translate("labellist_title") ),
	sort_label(translator::translate("hl_txt_sort")),
	stats(sortby,sortreverse,filter_state),
	scrolly(&stats)
{
	sort_label.set_pos(scr_coord(BUTTON1_X, 2));
	add_component(&sort_label);

	sortedby.set_pos(scr_coord(BUTTON1_X, 14));
	sortedby.set_size(scr_size(D_BUTTON_WIDTH*1.5, D_BUTTON_HEIGHT));
	sortedby.set_max_size(scr_size(D_BUTTON_HEIGHT, LINESPACE * 4));

	for (int i = 0; i < labellist::SORT_MODES; i++) {
		sortedby.append_element(new gui_scrolled_list_t::const_text_scrollitem_t(translator::translate(sort_text[i]), SYSCOL_TEXT));
	}
	sortedby.set_selection(default_sortmode);

	sortedby.add_listener(this);
	add_component(&sortedby);

	// sort ascend/descend button
	sort_asc.init(button_t::arrowup_state, "", scr_coord(BUTTON1_X + D_BUTTON_WIDTH * 1.5 + D_H_SPACE, 14+1), scr_size(D_ARROW_UP_WIDTH, D_ARROW_UP_HEIGHT));
	sort_asc.set_tooltip(translator::translate("hl_btn_sort_asc"));
	sort_asc.add_listener(this);
	sort_asc.pressed = sortreverse;
	add_component(&sort_asc);

	sort_desc.init(button_t::arrowdown_state, "", sort_asc.get_pos() + scr_coord(D_ARROW_UP_WIDTH + 2, 0), scr_size(D_ARROW_DOWN_WIDTH, D_ARROW_DOWN_HEIGHT));
	sort_desc.set_tooltip(translator::translate("hl_btn_sort_desc"));
	sort_desc.add_listener(this);
	sort_desc.pressed = !sortreverse;
	add_component(&sort_desc);

	filter.init( button_t::square_state, "Active player only", scr_coord(BUTTON2_X + D_BUTTON_WIDTH*1.5+10,14+1) );
	filter.pressed = filter_state;
	add_component(&filter);
	filter.add_listener( this );

	scrolly.set_pos(scr_coord(0,14+D_BUTTON_HEIGHT+2));
	scrolly.set_show_scroll_x(true);
	scrolly.set_scroll_amount_y(LINESPACE+1);
	add_component(&scrolly);

	display_list();

	set_windowsize(scr_size(D_DEFAULT_WIDTH, D_TITLEBAR_HEIGHT+18*(LINESPACE+1)+14+D_BUTTON_HEIGHT+2+1));
	set_min_windowsize(scr_size(D_DEFAULT_WIDTH, D_TITLEBAR_HEIGHT+4*(LINESPACE+1)+14+D_BUTTON_HEIGHT+2+1));

	set_resizemode(diagonal_resize);
	resize(scr_coord(0,0));
}



/**
 * This method is called if an action is triggered
 * @author Markus Weber/Volker Meyer
 */
bool labellist_frame_t::action_triggered( gui_action_creator_t *comp,value_t /* */)
{
	if(comp == &sortedby) {
		int tmp = sortedby.get_selection();
		if (tmp >= 0 && tmp < sortedby.count_elements())
		{
			sortedby.set_selection(tmp);
			set_sortierung((labellist::sort_mode_t)tmp);
		}
		else {
			sortedby.set_selection(0);
			set_sortierung(labellist::by_name);
		}
		default_sortmode = (uint8)tmp;
		display_list();
	}
	else if(comp == &sort_asc || comp == &sort_desc) {
		set_reverse(!get_reverse());
		display_list();
		sort_asc.pressed = sortreverse;
		sort_desc.pressed = !sortreverse;
	}
	else if (comp == &filter) {
		filter_state ^= 1;
		filter.pressed = filter_state;
		display_list();
	}
	return true;
}



/**
 * resize window in response to a resize event
 * @author Hj. Malthaner
 * @date   16-Oct-2003
 */
void labellist_frame_t::resize(const scr_coord delta)
{
	gui_frame_t::resize(delta);
	scr_size size = get_windowsize()-scr_size(0,D_TITLEBAR_HEIGHT+14+D_BUTTON_HEIGHT+2+1);
	scrolly.set_size(size);
	sortedby.set_max_size(scr_size(D_BUTTON_WIDTH*1.5, scrolly.get_size().h));
}



/**
* This function refreshes the label list
* @author Markus Weber/Volker Meyer
*/
void labellist_frame_t::display_list()
{
	stats.get_unique_labels(sortby, sortreverse, filter_state);
	stats.recalc_size();
}
