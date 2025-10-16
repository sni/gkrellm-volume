/*
    win32 port by Bill Nalen
    bill@nalens.com
*/

// based on

// VolumeControl.cpp: implementation of the CVolumeControl class.
//
// Author:  Bill Oatman
// Version: 1.0
//          http://www.netacc.net/~waterbry/BillsApps.htm
//
//////////////////////////////////////////////////////////////////////


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

#include <math.h>
#include "mixer.h"
#include "win32_mixer.h"

#define WIN32MIXER(x) ((win32_mixer_t *)x->priv)
static mixer_ops_t * get_mixer_ops(void);

void fixName(char* name)
{
    unsigned int i;

    for (i = 0; i < strlen(name); i++) {
        if (name[i] == ' ') 
            name[i] = '_';
    }
}

/* tries to open a mixer device, returns NULL on error or otherwise an mixer_t
 * struct */
static mixer_t *
win32_mixer_open(char *id) {
    mixer_t *result = NULL;
    win32_mixer_t *win32result = NULL;
    int nr;
    MIXERCAPS mxCaps;
    int numMixers;
    MIXERLINE mxl;
    MIXERCONTROL mxc;
    MIXERLINECONTROLS mxlc;
    HMIXER hMixer;
    int j, numConn;

    fixName(id);

    numMixers = mixerGetNumDevs();
	if (numMixers != 0)
	{
	    hMixer = NULL;
	    ZeroMemory(&mxCaps, sizeof(MIXERCAPS));

        if (mixerOpen(&hMixer, 0, 0, 0, MIXER_OBJECTF_MIXER) != MMSYSERR_NOERROR) {
			return NULL;
        }

        if (mixerGetDevCaps((UINT)hMixer, &mxCaps, sizeof(MIXERCAPS)) != MMSYSERR_NOERROR) {
			return NULL;
        }

		// get dwLineID
		mxl.cbStruct = sizeof(MIXERLINE);
		mxl.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;

        if (strcmp(id, "Master") != 0) {
            mxl.dwDestination = 0;
            mixerGetLineInfo((HMIXEROBJ)hMixer,&mxl,MIXER_OBJECTF_HMIXER | MIXER_GETLINEINFOF_DESTINATION);

            numConn = mxl.cConnections;
		    for (j = 0; j < numConn; j++)
		    {
			    // ...get info upon that source line
			    mxl.cbStruct = sizeof(MIXERLINE);
			    mxl.dwDestination = 0;
			    mxl.dwSource = j;

			    mixerGetLineInfo((HMIXEROBJ)hMixer, &mxl, MIXER_GETLINEINFOF_SOURCE);

                fixName(mxl.szShortName);

                if (strcmp(mxl.szShortName, id) == 0) {
                    result = malloc(sizeof(mixer_t));
                    win32result = malloc(sizeof(win32_mixer_t));
                    result->name = g_strdup("Master");
                    result->hMixer = hMixer;
                    result->priv = win32result;
                    break;
                }
            }
        }
        else {        
            if(mixerGetLineInfo((HMIXEROBJ)hMixer, &mxl, MIXER_OBJECTF_HMIXER | MIXER_GETLINEINFOF_COMPONENTTYPE) != MMSYSERR_NOERROR)
	    		return NULL;
            result = malloc(sizeof(mixer_t));
            win32result = malloc(sizeof(win32_mixer_t));
            result->name = g_strdup("Master");
            result->hMixer = hMixer;
            result->priv = win32result;
        }

        if (result == NULL)
            return NULL;

        nr = -1;

        // now retrieve the devices

    result->ops = get_mixer_ops();
    result->dev_realnames = malloc(sizeof(char *) * 3);
    result->dev_names = malloc(sizeof(char *) * 3);
		// Volume control
		mxlc.cbStruct = sizeof(MIXERLINECONTROLS);
		mxlc.dwLineID = mxl.dwLineID;
		mxlc.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
		mxlc.cControls = 1;
		mxlc.cbmxctrl = sizeof(MIXERCONTROL);
		mxlc.pamxctrl = &mxc;
        if(mixerGetLineControls((HMIXEROBJ)hMixer,&mxlc,MIXER_OBJECTF_HMIXER | MIXER_GETLINECONTROLSF_ONEBYTYPE) == MMSYSERR_NOERROR) {
		    // save record dwControlID
            ++nr;
		    win32result->minimum[nr] = mxc.Bounds.dwMinimum;
		    win32result->maximum[nr] = mxc.Bounds.dwMaximum;
		    win32result->controlID[nr] = mxc.dwControlID;
            result->dev_realnames[nr] = g_strdup("Vol");
            result->dev_names[nr] = g_strdup("Vol");
        }

        // Bass control
		mxlc.cbStruct = sizeof(MIXERLINECONTROLS);
		mxlc.dwLineID = mxl.dwLineID;
		mxlc.dwControlType = MIXERCONTROL_CONTROLTYPE_BASS;
		mxlc.cControls = 1;
		mxlc.cbmxctrl = sizeof(MIXERCONTROL);
		mxlc.pamxctrl = &mxc;
        if(mixerGetLineControls((HMIXEROBJ)hMixer,&mxlc,MIXER_OBJECTF_HMIXER | MIXER_GETLINECONTROLSF_ONEBYTYPE) == MMSYSERR_NOERROR) {
            ++nr;
		    win32result->minimum[nr] = mxc.Bounds.dwMinimum;
		    win32result->maximum[nr] = mxc.Bounds.dwMaximum;
		    win32result->controlID[nr] = mxc.dwControlID;
            result->dev_rnames[nr] = g_strdup("Bass");
            result->dev_names[nr] = g_strdup("Bass");
        }

        // treble control
		mxlc.cbStruct = sizeof(MIXERLINECONTROLS);
		mxlc.dwLineID = mxl.dwLineID;
		mxlc.dwControlType = MIXERCONTROL_CONTROLTYPE_TREBLE;
		mxlc.cControls = 1;
		mxlc.cbmxctrl = sizeof(MIXERCONTROL);
		mxlc.pamxctrl = &mxc;
        if(mixerGetLineControls((HMIXEROBJ)hMixer,&mxlc,MIXER_OBJECTF_HMIXER | MIXER_GETLINECONTROLSF_ONEBYTYPE) == MMSYSERR_NOERROR) {
            ++nr;
		    win32result->minimum[nr] = mxc.Bounds.dwMinimum;
		    win32result->maximum[nr] = mxc.Bounds.dwMaximum;
		    win32result->controlID[nr] = mxc.dwControlID;
            result->dev_rnames[nr] = g_strdup("Treb");
            result->dev_names[nr] = g_strdup("Treb");
        }
	}
	else
		return NULL;

    result->nrdevices = nr + 1;

    return result;
}

static void 
win32_mixer_close(mixer_t *mixer) {
    int i;

	if (WIN32MIXER(mixer)->hMixer != NULL)
	{
		mixerClose(WIN32MIXER(mixer)->hMixer);
	}

    for (i=0;i < mixer->nrdevices; i++) {
 		   WIN32MIXER(mixer)->controlID[i] = 0;
        g_free(mixer->dev_names[i]);
        g_free(mixer->dev_realnames[i]);
    }
    g_free(mixer->name);
    g_free(mixer->dev_names);
    g_free(mixer->dev_realnames);
    g_free(mixer->priv);
    g_free(mixer);

	return;
}

/* get the full scale of a device and get/set the volume */
long win32_mixer_device_get_fullscale(mixer_t *mixer,int devid) {
    return WIN32MIXER(mixer)->maximum[devid] - 
           WIN32MIXER(mixer)->minimum[devid];
}

void win32_mixer_device_get_volume(mixer_t *mixer, int devid,int *left,int *right) {
    MIXERCONTROLDETAILS_UNSIGNED mxcdVolume;
	MIXERCONTROLDETAILS mxcd;

    if (WIN32MIXER(mixer)->hMixer == NULL) {
        *left = 0;
        *right = 0;
		return;
    }

	mxcd.cbStruct = sizeof(MIXERCONTROLDETAILS);
	mxcd.dwControlID = mixer->controlID[devid];
	mxcd.cChannels = 1;
	mxcd.cMultipleItems = 0;
	mxcd.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
	mxcd.paDetails = &mxcdVolume;
    if (mixerGetControlDetails((HMIXEROBJ) WIN32MIXER(mixer)->hMixer, &mxcd, MIXER_OBJECTF_HMIXER | MIXER_GETCONTROLDETAILSF_VALUE) != MMSYSERR_NOERROR) {
        *left = 0;
        *right = 0;
		return;
    }

    *left = mxcdVolume.dwValue - WIN32MIXER(mixer)->minimum[devid];
    *right = mxcdVolume.dwValue - WIN32MIXER(mixer)->minimum[devid];
    //return mxcdVolume.dwValue - mixer->minimum[devid];
}

static void  
win32_mixer_device_set_volume(mixer_t *mixer, int devid,int left,int right) {
    MIXERCONTROLDETAILS_UNSIGNED mxcdVolume;
	MIXERCONTROLDETAILS mxcd;

	if (WIN32MIXER(mixer)->hMixer == NULL)
		return;

    if (left >= right) 
        mxcdVolume.dwValue = left + WIN32MIXER(mixer)->minimum[devid];
    else
        mxcdVolume.dwValue = right + WIN32MIXER(mixer)->minimum[devid];

	mxcd.cbStruct = sizeof(MIXERCONTROLDETAILS);
	mxcd.dwControlID = WIN32MIXER(mixer)->controlID[devid];
	mxcd.cChannels = 1;
	mxcd.cMultipleItems = 0;
	mxcd.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
	mxcd.paDetails = &mxcdVolume;
	if (mixerSetControlDetails((HMIXEROBJ)WIN32MIXER(mixer)->hMixer, &mxcd, MIXER_OBJECTF_HMIXER | MIXER_SETCONTROLDETAILSF_VALUE) != MMSYSERR_NOERROR)
		return;
}

mixer_idz_t *
win32_mixer_get_id_list(void) {
	MIXERLINE mxl;
    HMIXER hMixer;
    MIXERCAPS mxCaps;
    int numMixers;
    mixer_idz_t *result = NULL;
    int j, numConn;

    result = mixer_id_list_add("Master", result); 

    numMixers = mixerGetNumDevs();
	if (numMixers != 0)
	{
	    hMixer = NULL;
	    ZeroMemory(&mxCaps, sizeof(MIXERCAPS));

        if (mixerOpen(&hMixer, 0, 0, 0, MIXER_OBJECTF_MIXER) != MMSYSERR_NOERROR) {
			return NULL;
        }

        if (mixerGetDevCaps((UINT)hMixer, &mxCaps, sizeof(MIXERCAPS)) != MMSYSERR_NOERROR) {
			return NULL;
        }

		// get dwLineID
		mxl.cbStruct = sizeof(MIXERLINE);
		mxl.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;

        mxl.dwDestination = 0;
        mixerGetLineInfo((HMIXEROBJ)hMixer,&mxl,MIXER_OBJECTF_HMIXER | MIXER_GETLINEINFOF_DESTINATION);

        numConn = mxl.cConnections;
		for (j = 0; j < numConn; j++)
		{
			// ...get info upon that source line
			mxl.cbStruct = sizeof(MIXERLINE);
			mxl.dwDestination = 0;
			mxl.dwSource = j;

			mixerGetLineInfo((HMIXEROBJ)hMixer, &mxl, MIXER_GETLINEINFOF_SOURCE);

            fixName(mxl.szShortName);

            result = mixer_id_list_add(mxl.szShortName, result);
        }
    }

  return result;
}

double rint(double arg)
{
    int iarg = (int) arg;

    if (arg - iarg < 0.5)
        return floor(arg);
    else
        return ceil(arg);
}

static mixer_ops_t win32_mixer_ops = {
    .mixer_get_id_list = win32_mixer_get_id_list,
    .mixer_open = win32_mixer_open,
    .mixer_close = win32_mixer_close,
    .mixer_device_get_fullscale = win32_mixer_device_get_fullscale,
    .mixer_device_get_volume = win32_mixer_device_get_volume,
    .mixer_device_set_volume = win32_mixer_device_set_volume
};

static mixer_ops_t *
get_mixer_ops(void) {
    return &win32_mixer_ops;
}

mixer_ops_t *
init_win32_mixer(void) {
    return get_mixer_ops();
}
