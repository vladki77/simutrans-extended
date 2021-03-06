/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef PATHES_H
#define PATHES_H


/**
 * This header defines all paths used be simutrans relative to the game directory.
 *
 * two defines for all paths - if You want the root path,  use:
 *	#define _PATH ""
 *	#define _PATH_X ""
 * else use
 *	#define _PATH "somewhere"
 *	#define _PATH_X _PATH "/"
 *
 * @author Volker Meyer
 * @date  18.06.2003
 */

#define FONT_PATH	    "font"
#define FONT_PATH_X	    FONT_PATH "/"

#define SAVE_PATH	    "save"
#define SAVE_PATH_X	    SAVE_PATH "/"
#define SAVE_PATH_X_LEN (sizeof(SAVE_PATH_X) - 1)

#define SCRENSHOT_PATH	    "screenshot"
#define SCRENSHOT_PATH_X    SCRENSHOT_PATH "/"

#endif
