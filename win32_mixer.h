#if !defined(WIN32_MIXER_H)
#define WIN32_MIXER_H
/*
    win32 port by Bill Nalen
    bill@nalens.com
*/


/* GKrellM Volume plugin
 |  Copyright (C) 1999-2000 Sjoerd Simons
 |
 |  Author:  Sjoerd Simons  sjoerd@luon.net
 |
 |  This program is free software which I release under the GNU General Public
 |  License. You may redistribute and/or modify this program under the terms
 |  of that license as published by the Free Software Foundation; either
 |  version 2 of the License, or (at your option) any later version.
 |
 |  This program is distributed in the hope that it will be useful,
 |  but WITHOUT ANY WARRANTY; without even the implied warranty of
 |  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 |  GNU General Public License for more details.
 |
 |  To get a copy of the GNU General Puplic License,  write to the
 |  Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "mixer.h"

#include <src/gkrellm.h>
#include <src/win32-plugin.h>
#include <mmsystem.h>

typedef struct {
    HMIXER hMixer;
    DWORD controlID[3];
    DWORD minimum[3];
    DWORD maximum[3];
} win32_mixer_t;

double rint(double arg);

mixer_ops_t *init_win32_mixer(void);
#endif
