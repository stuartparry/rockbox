/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2002 Robert E. Hak
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#ifndef __MENU_H__
#define __MENU_H__

#include <stdbool.h>

/* button definitions */
#ifdef HAVE_RECORDER_KEYPAD
#define MENU_NEXT   BUTTON_DOWN
#define MENU_PREV   BUTTON_UP
#define MENU_EXIT   (BUTTON_LEFT | BUTTON_REL)
#define MENU_EXIT2  (BUTTON_OFF | BUTTON_REL)
#define MENU_EXIT3  (BUTTON_F1 | BUTTON_REL)
#define MENU_ENTER  (BUTTON_RIGHT | BUTTON_REL)
#define MENU_ENTER2 (BUTTON_PLAY | BUTTON_REL)

#elif defined HAVE_PLAYER_KEYPAD
#define MENU_NEXT  BUTTON_RIGHT
#define MENU_PREV  BUTTON_LEFT
#define MENU_EXIT  (BUTTON_STOP | BUTTON_REL)
#define MENU_EXIT2 (BUTTON_MENU | BUTTON_REL)
#define MENU_ENTER (BUTTON_PLAY | BUTTON_REL)

#elif defined HAVE_ONDIO_KEYPAD
#define MENU_NEXT  BUTTON_DOWN
#define MENU_PREV  BUTTON_UP
#define MENU_EXIT  (BUTTON_LEFT | BUTTON_REL)
#define MENU_EXIT2 (BUTTON_MENU | BUTTON_REL)
#define MENU_ENTER (BUTTON_RIGHT | BUTTON_REL)

#endif

struct menu_item {
    unsigned char *desc; /* string or ID */
    bool (*function) (void); /* return true if USB was connected */
};

int menu_init(const struct menu_item* mitems, int count, int (*callback)(int, int),
              const char *button1, const char *button2, const char *button3);
void menu_exit(int menu);

void put_cursorxy(int x, int y, bool on);

 /* Returns below define, or number of selected menu item*/
int menu_show(int m);
#define MENU_ATTACHED_USB -1
#define MENU_SELECTED_EXIT -2

bool menu_run(int menu);
int menu_cursor(int menu);
char* menu_description(int menu, int position);
void menu_delete(int menu, int position);
int menu_count(int menu);
bool menu_moveup(int menu);
bool menu_movedown(int menu);
void menu_draw(int menu);
void menu_insert(int menu, int position, char *desc, bool (*function) (void));

#endif /* End __MENU_H__ */
