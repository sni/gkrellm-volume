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

#define VOLUME_MAJOR_VERSION 2
#define VOLUME_MINOR_VERSION 1
#define VOLUME_EXTRA_VERSION 12

#define CONFIG_KEYWORD "volume_plugin_config"

#define LOCATION MON_APM 
/* The location of the plugin, choose between :
|*   MON_CLOCK, MON_CPU, MON_PROC, MON_DISK,
|*   MON_INET, MON_NET, MON_FS, MON_MAIL,       
|*   MON_APM, or MON_UPTIME                    
*/

/* per slider flags */
enum {
 IS_PRESSED =0,
 SAVE_VOLUME,
 BALANCE,
 MUTED
};

/* global flags */
enum {
  MUTEALL =0
};
/* flags macro's */
#define SET_FLAG(s,x) (s |= (1 << x))
#define DEL_FLAG(s,x) (s = s & ~(1<<x))
#define GET_FLAG(s,x) (s & (1<<x))
/******/

typedef struct Slider Slider;
typedef struct Mixer Mixer;

typedef struct{
  GkrellmKrell *krell;
  GkrellmPanel *panel;
  GkrellmDecal *decal;
  int flags;
  Slider *slider;
} Bslider;

struct  Slider {
  GkrellmKrell *krell;
  GkrellmPanel *panel;
  GkrellmDecalbutton *button;
  mixer_t *mixer;
  Mixer *parent;
  int dev;
  int flags;
  int pleft,pright;
  int balance; /* [-100..100] */
  Slider *next;
  Bslider *bal;
};



struct Mixer {
  char *id;
  mixer_t *mixer;
  Slider *Sliderz;
  Mixer *next;
};

