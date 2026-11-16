#include "cc.h"
#include <math.h>
#include <string.h>
/* Port of the original PIT tone sequencer (0x7DA..0x895). The DOS code is
 * never executed: only its divisor/duration tables and priority rule are used. */
typedef struct {
	uint16_t divisor, ticks;
} Tone;
#include "sound_data.h"
typedef struct {
	const Tone *tones;
	int count, priority;
} SoundSpec;
#define SPEC(name, priority) {tones_##name, sizeof tones_##name / sizeof tones_##name[0], priority}
static const SoundSpec sounds[SOUND_COUNT] = {SPEC(shot, 0), SPEC(hit, 1), SPEC(hurt, 2),
	SPEC(death, 4), SPEC(pickup, 3), SPEC(door, 4), SPEC(teleport, 2), SPEC(arrival, 4),
	SPEC(life, 4), SPEC(theme, 4), SPEC(panorama, 4)};
#undef SPEC
static const double pit_frequency = 14318180. / 12.;
void sound_mix(AudioState *audio, float *output, int frames) {
	memset(output, 0, (size_t)frames * sizeof *output);
	if (!audio->enabled || audio->rate <= 0)
		return;
	/* The original PC speaker is monophonic. A higher-priority effect replaces
	 * the active one; lower-priority requests do not create overlapping tones. */
	SoundVoice *v = &audio->voices[0];
	if (v->effect < 0 || v->effect >= SOUND_COUNT)
		return;
	const SoundSpec *spec = &sounds[v->effect];
	double samples_per_tick = audio->rate * 65536. / pit_frequency;
	for (int n = 0; n < frames && v->frame < v->length; n++) {
		while (v->note + 1 < spec->count &&
			v->frame >= llround(v->ticks * samples_per_tick)) {
			v->note++;
			v->ticks += spec->tones[v->note].ticks;
			v->phase = 0;
		}
		double frequency = pit_frequency / spec->tones[v->note].divisor;
		v->phase += frequency / audio->rate;
		v->phase -= floor(v->phase);
		/* Centre the square wave: capture's DC offset is not a game sound. */
		double edge = fmin(1, fmin(v->frame / (audio->rate * .001),
					      (v->length - v->frame) / (audio->rate * .002)));
		output[n] = (float)((v->phase < .5 ? .15 : -.15) * edge);
		v->frame++;
	}
}
static void audio_callback(void *userdata, Uint8 *stream, int bytes) {
	sound_mix(userdata, (float *)stream, bytes / (int)sizeof(float));
}
bool sound_init(Resources *r) {
	AudioState *a = &r->audio;
	if (SDL_InitSubSystem(SDL_INIT_AUDIO))
		return false;
	SDL_AudioSpec wanted = {0}, obtained;
	wanted.freq = 48000;
	wanted.format = AUDIO_F32SYS;
	wanted.channels = 1;
	wanted.samples = 256;
	wanted.callback = audio_callback;
	wanted.userdata = a;
	a->device = SDL_OpenAudioDevice(NULL, 0, &wanted, &obtained, 0);
	if (!a->device)
		return false;
	a->rate = obtained.freq;
	a->enabled = true;
	SDL_PauseAudioDevice(a->device, 0);
	return true;
}
void sound_close(Resources *r) {
	if (r->audio.device)
		SDL_CloseAudioDevice(r->audio.device);
	memset(&r->audio, 0, sizeof r->audio);
}
void sound_set_enabled(Resources *r, bool enabled) {
	AudioState *a = &r->audio;
	if (a->device)
		SDL_LockAudioDevice(a->device);
	a->enabled = enabled;
	if (!enabled)
		memset(a->voices, 0, sizeof a->voices);
	if (a->device)
		SDL_UnlockAudioDevice(a->device);
}
void sound_stop(Resources *r) {
	AudioState *a = &r->audio;
	if (a->device)
		SDL_LockAudioDevice(a->device);
	memset(a->voices, 0, sizeof a->voices);
	if (a->device)
		SDL_UnlockAudioDevice(a->device);
}
void sound_play(Resources *r, int effect) {
	AudioState *a = &r->audio;
	if (effect < 0 || effect >= SOUND_COUNT)
		return;
	if (a->device)
		SDL_LockAudioDevice(a->device);
	SoundVoice *v = &a->voices[0];
	bool active = v->frame < v->length;
	if (a->enabled && a->rate > 0 &&
		(!active || sounds[effect].priority >= sounds[v->effect].priority)) {
		const SoundSpec *spec = &sounds[effect];
		int ticks = 0;
		for (int n = 0; n < spec->count; n++)
			ticks += spec->tones[n].ticks;
		*v = (SoundVoice){.effect = effect,
			.length = (int)llround(ticks * a->rate * 65536. / pit_frequency),
			.ticks = spec->tones[0].ticks};
	}
	if (a->device)
		SDL_UnlockAudioDevice(a->device);
}
