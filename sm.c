/*
  sm.lv2 is a loudspeaker management and master volume plugin 

  Copyright (C) 2018 Jörn Nettingsmeier

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
  
  Cubic interpolating delay based on work by James McCartney, 
  Andy Wingo, and Steve Harris.
*/


#include <math.h>
#include <stdlib.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"

#define NCHANS 2		// number of channels for plugin 
#define SM_URI "http://stackingdwarves.net/lv2/sm"

#define NCHANPORTS 7		// number of per-channel ports
#define NGLOBALS 1		// number of global ports
#define MAXDLY 0.05f		// the maximum delay, in seconds
#define LP_SMOOTHING 7.0f	// heuristic value for gain paramter smoothing
#define DB_CO(_dbgain) ((_dbgain) > -119.0f ? powf(10.0f, (_dbgain) * 0.05f) : 0.0f)
#define CALC_DLY(_ms, _hz) (_hz * fmin(MAXDLY, _ms * 0.001))

typedef enum {
        // NGLOBALS global ports
	SM_gain   =  0,
	// NCHANPORTS per-channel ports
	SM_in       = 1,
	SM_out      = 2,
	SM_att      = 3,
	SM_attOn    = 4,
	SM_delay    = 5,
	SM_delayOn  = 6,
	SM_active   = 7
	// so use multiples for additional channels
} PortTypes;

typedef struct {
	// global ports
	struct {
		float *gain;
	} gports;
	// per-channel ports
	struct {
		// audio port buffers per channel
		const float *in[NCHANS];
		float *out[NCHANS];
		// control port data per channel
		float *att[NCHANS];	// in dB
		float *attOn[NCHANS];	// 0 is off
		float *delay[NCHANS];	// in ms
		float *delayOn[NCHANS];	// 0 is off
		float *active[NCHANS];	// 0 is off
	} cports;
	// internal data
	double rate;			// sample rate in Hz
	float *dlybuf[NCHANS];		// a ringbuffer
	unsigned int dlybufmask[NCHANS];// used as fast modulo replacement for 2^n ringbuffer
	long writep[NCHANS];		// in samples
	float current_delay[NCHANS];	// in samples
	float target_delay[NCHANS];	// in samples
	float current_gain[NCHANS];	// as coefficient
	float target_gain[NCHANS];	// as coefficient
} Sm;

// Cubic interpolation function
static inline float cube_interp(
	const float fr, 
	const float inm1, 
	const float in, 
	const float inp1,
	const float inp2)
{
	return in + 0.5f * fr * (
		inp1 - inm1 + fr * (
			4.0f * inp1 + 2.0f * inm1 - 5.0f * in - inp2 + fr * (
				3.0f * (in - inp1) - inm1 + inp2
			)
		)
	);
}

static LV2_Handle instantiate(
        const LV2_Descriptor* descriptor,
        double rate,
        const char* bundle_path,
        const LV2_Feature* const* features
)
{
	Sm *sm = (Sm*) calloc(1, sizeof(Sm));
	unsigned int nelem = 1;
	
	sm->rate = rate;
	
	while (nelem < MAXDLY * rate) nelem <<= 1; // round up delay buffer to next power of two for easy masking
	
	for (int i=0; i<NCHANS; i++) {
		sm->dlybuf[i] = (float*) calloc(nelem, sizeof(float));
		if (sm->dlybuf[i]) {
			sm->dlybufmask[i] = nelem - 1;
		} else {
			sm->dlybufmask[i] = 0;
		}
		sm->writep[i] = 0;
        }
	return (LV2_Handle)sm;
}

static void connect_port(
        LV2_Handle instance,
        uint32_t port,
        void* data
)
{
	// slight ugliness to keep channel count flexible at compile time
        int ptype = (port < NGLOBALS ? port : (port - NGLOBALS) % NCHANPORTS + NGLOBALS);
        int pindex = (port - NGLOBALS) / NCHANPORTS;
        
        Sm *sm = (Sm*) instance;

	switch ((PortTypes)ptype) {
        case SM_gain:
		sm->gports.gain = (float*)data;
		break;
	case SM_in:
		sm->cports.in[pindex] = (const float*)data;
		break;
	case SM_out:
		sm->cports.out[pindex] = (float*)data;
		break;
	case SM_att:
		sm->cports.att[pindex] = (float*)data;
		break;
	case SM_attOn:
		sm->cports.attOn[pindex] = (float*)data;
		break;
	case SM_delay:
		sm->cports.delay[pindex] = (float*)data;
		break;
	case SM_delayOn:
		sm->cports.delayOn[pindex] = (float*)data;
		break;
	case SM_active:
		sm->cports.active[pindex] = (float*)data;
		break;
	}
}

static void activate(LV2_Handle instance)
{
        Sm *sm = (Sm*)instance;
        for (int i=0; i<NCHANS; i++) {
                sm->current_gain[i] = 0.0f;
                sm->target_gain[i] = 0.0f;
                sm->current_delay[i] = 0.0f;
                sm->target_delay[i] = 0.0f;
                sm->writep[i] = 0;
        }
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
	Sm                 *sm = (Sm*)instance;

	const float* const *in = sm->cports.in;
	float* const      *out = sm->cports.out;
	float* const   *dlybuf = sm->dlybuf;

	const float g = DB_CO(*(sm->gports.gain));

	const float lp = LP_SMOOTHING / n_samples;
	const float lp_i = 1.0f - lp;
	
	for (int i=0; i < NCHANS; i++) {

		// compute attenuation gain if enabled
		const float a = (*(sm->cports.attOn[i]) ? DB_CO(*(sm->cports.att[i])) : 1.0f) ;
		// compute total channel gain if enabled
		sm->target_gain[i] = (*(sm->cports.active[i]) ? g * a : 0.0f);
		// compute channel delay if enabled
		sm->target_delay[i] = (*(sm->cports.delayOn[i]) ? CALC_DLY(*(sm->cports.delay[i]), sm->rate) : 0.0f);
		
		// Has the delay setting changed?
		if (sm->target_delay[i] == sm->current_delay[i]) {
			// No. Nudge to the nearest whole-sample delay for efficient lossless operation.
			for (uint32_t pos = 0; pos < n_samples; pos++) {
				long readp = sm->writep[i] - (long)round(sm->current_delay[i]);
				sm->dlybuf[i][sm->writep[i] & sm->dlybufmask[i]] = in[i][pos];
				sm->writep[i]++;
	        		sm->current_gain[i] = sm->current_gain[i] * lp_i + sm->target_gain[i] * lp;
			        out[i][pos] = sm->dlybuf[i][readp & sm->dlybufmask[i]] * sm->current_gain[i];
			}
		} else {
			// Yes. Move to new delay using cubic spline interpolation (click-free, temporarily lossy).
			float slope = (sm->target_delay[i] - sm->current_delay[i]) / n_samples;
			for (uint32_t pos = 0; pos < n_samples; pos++) {
				long readp;
				float frac, read;
				sm->writep[i]++;
				sm->current_delay[i] += slope;
				readp = sm->writep[i] - (long)sm->current_delay[i];
				frac = sm->current_delay[i] - (long)sm->current_delay[i];
				read = cube_interp (frac,
					sm->dlybuf[i][(readp - (long)slope) & sm->dlybufmask[i]], 
					sm->dlybuf[i][readp & sm->dlybufmask[i]], 
					sm->dlybuf[i][(readp + (long)slope) & sm->dlybufmask[i]], 
					sm->dlybuf[i][(readp + 2*(long)slope) & sm->dlybufmask[i]]
				);
				sm->dlybuf[i][sm->writep[i] & sm->dlybufmask[i]] = in[i][pos];

	        		sm->current_gain[i] = sm->current_gain[i] * lp_i + sm->target_gain[i] * lp;
			        out[i][pos] = read * sm->current_gain[i];
			}
			sm->current_delay[i] = sm->target_delay[i];
		}

	}
}

static void
deactivate(LV2_Handle instance)
{
}
static void
cleanup(LV2_Handle instance)
{
        Sm *sm = (Sm*) instance;
	for (int i=0; i<NCHANS; i++) {
                free(sm->dlybuf[i]);
        }
	free(instance);
}

static const void*
extension_data(const char* uri)
{
	return NULL;
}

static const LV2_Descriptor descriptor = {
	SM_URI,
	instantiate,
	connect_port,
	activate,
	run,
	deactivate,
	cleanup,
	extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch (index) {
	case 0:  return &descriptor;
	default: return NULL;
	}
}
