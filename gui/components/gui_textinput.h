/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_TEXTINPUT_H
#define GUI_COMPONENTS_GUI_TEXTINPUT_H


#include "gui_action_creator.h"
#include "gui_component.h"
#include "../../simcolor.h"
#include "../../display/simgraph.h"
#include "../../utils/cbuffer_t.h"


/*
 * A simple text input field. It has no Text Buffer,
 * only a pointer to a buffer created by someone else.
 *
 * @date 19-Apr-01
 * @author Hj. Malthaner
 */
class gui_textinput_t :
	public gui_action_creator_t,
	public gui_component_t
{
protected:

	/**
	 * The string buffer
	 * @author Hj. Malthaner
	 */
	char *text;

	// text, which has not yet inputted (i.e. by an IME)
	cbuffer_t composition;
	size_t composition_target_start;
	size_t composition_target_length;

	/**
	 * Maximum length of the string buffer
	 * @author Hj. Malthaner
	 */
	size_t max;

	/**
	 * position of head cursor to the text
	 * represents front end of the selected text portion
	 * @author hsiegeln
	 */
	size_t head_cursor_pos;

	/**
	 * position of tail cursor to the text
	 * represent rear end of the selected text portion
	 * @author Knightly
	 */
	size_t tail_cursor_pos;

	/**
	  * offset for controlling horizontal text scroll
	  * Dwachs: made private to check for mouse induced cursor moves
	  */
	scr_coord_val scroll_offset;

	/**
	 * text alignment
	 * @author: Dwachs
	 */
	uint8 align;

	COLOR_VAL textcol;

	// true if there were changed but no notification was sent yet
	bool text_dirty;

	/**
	 * reference time for regulating cursor blinking
	 * @author Knightly
	 */
	uint32 cursor_reference_time;

	/**
	 * whether focus has been received
	 * @author Knightly
	 */
	bool focus_recieved;

	/**
	 * determine new cursor position from event coordinates
	 * @author Knightly
	 */
	size_t calc_cursor_pos(const int x);

	/**
	 * Remove selected text portion, if any.
	 * Returns true if some selected text is actually deleted.
	 * @author Knightly
	 */
	bool remove_selection();

public:
	gui_textinput_t();

	/**
	 * Sets the Text buffer
	 *
	 * @author Hj. Malthaner
	 */
	void set_text(char *text, size_t max);

	// text which is not yet inputed (i.e. for east asian text), assuming either native or utf8 encoding
	void set_composition_status( char *composition, int target_start, int target_length );

	/**
	 * Return the Text buffer
	 *
	 * @author Hj. Malthaner
	 */
	char *get_text() const { return text; }
	const char *get_composition() const { return composition.get_str(); }

	bool infowin_event(event_t const*) OVERRIDE;

	/**
	 * Draw the component
	 * @author Hj. Malthaner
	 */
	virtual void draw(scr_coord offset) OVERRIDE;

	// x position of the current cursor (for IME purposes)
	scr_coord_val get_current_cursor_x() { return calc_cursor_pos(head_cursor_pos); };

	/**
	 * Detect change of focus state and determine whether cursor should be displayed,
	 * and call the function that performs the actual display
	 * @author Knightly
	 */
	void display_with_focus(scr_coord offset, bool has_focus);

	// function that performs the actual display
	virtual void display_with_cursor(scr_coord offset, bool cursor_active, bool cursor_visible);

	// to allow for right-aligned text
	void set_alignment(uint8 _align){ align = _align;}

	// to set text color
	void set_color(COLOR_VAL col){ textcol = col;}
};


class gui_hidden_textinput_t : public gui_textinput_t
{
	// and set the cursor right when clicking with the mouse
	bool infowin_event(event_t const*) OVERRIDE;

	// function that performs the actual display; just draw with stars ...
	virtual void display_with_cursor(scr_coord offset, bool cursor_active, bool cursor_visible) OVERRIDE;
};


#endif
