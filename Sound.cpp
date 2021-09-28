#include "Sound.hpp"
#include "load_wav.hpp"
#include "load_opus.hpp"

#include <SDL.h>

#include <list>
#include <cassert>
#include <exception>
#include <iostream>
#include <algorithm>
#include <random>

//local (to this file) data used by the audio system:
namespace {
	std::vector<float> mix_buffer;
	uint64_t next_crackle = 0;
	uint64_t crackle_duration = 0;
	uint64_t global_sample = 0;
	float crackle_amount = 0.0f;

	//handy constants:
	constexpr uint32_t const AUDIO_RATE = 48000; //sampling rate
	constexpr uint32_t const MIX_SAMPLES = 1024; //number of samples to mix per call of mix_audio callback; n.b. SDL requires this to be a power of two

	//The audio device:
	SDL_AudioDeviceID device = 0;

	//list of all currently playing samples:
	std::list< std::shared_ptr< Sound::PlayingSample > > playing_samples;

}

// TODO: use a nicer shared pointers impl
Sound::GlitchSynth synths[Sound::NUM_SYNTHS];

void Sound::GlitchSynth::set_attack(float amp, uint64_t at) {
	attack_amplitude = amp;
	attack_threshold = at;
}

void Sound::GlitchSynth::set_decay(float amp, uint64_t at) {
	decay_amplitude = amp;
	decay_threshold = at;
}

// sustain doesn't have a time because it's held as long as the player holds a key down
void Sound::GlitchSynth::set_sustain(float amp) {
	sustain_amplitude = amp;
}

void Sound::GlitchSynth::set_release(float amp, uint64_t at) {
	release_amplitude = amp;
	release_threshold = at;
}

// stop current note, setup for playing new note
// TODO: find cause of ADSR not following smoothly
void Sound::GlitchSynth::play(float frequency) {
	cycle_length = uint64_t(AUDIO_RATE/frequency);
	current_sample_number = 0;
	release_start = 0;
	adsr_state = ADSR_ATTACK;
	do_release = false;
}

// add own samples to given buffer
void Sound::GlitchSynth::generate_samples(int n, std::vector<float>& buffer) {
	int half_cycle = int(cycle_length/2);
	uint64_t ad_threshold = attack_threshold + decay_threshold;
	static std::mt19937 mt;

	for (int i = 0; i < n; i++) {
		int cycle_position = int(current_sample_number % cycle_length);
		float s = 0;
		switch (osc) {
			case OSC_SQUARE: // +1 for half cycle, -1 for other half
				// o p t i m i z e d
				s = float((cycle_position <= half_cycle) - (cycle_position > half_cycle));
				break;

			case OSC_SAW: // from -1 to +1 over a cycle
				s = (2.0f * cycle_position/float(cycle_length)) - 1.0f;
				break;

			case OSC_SINE: // standard sine
				s = 2.0f * std::sinf(3.1415926f * (cycle_position/float(cycle_length))) - 1.0f;
				break;

			case OSC_NOISE: // uniform noise. TODO: use noise with a peaked distribution
				s = 2.0f * (mt()/float(mt.max())) - 1.0f;
				break;
		}

		float amp = 0.0f;
		float t = 0;

		// run ADSR envelope
		switch(adsr_state) {
			case ADSR_ATTACK:
				// interpolate from 0 to attack_amplitude
				amp = attack_amplitude * (current_sample_number/float(attack_threshold));
				if ((current_sample_number + 1) > attack_threshold) {
					adsr_state = ADSR_DECAY;
				}
				// need to be able to execute a release ASAP
				else if (do_release) {
					adsr_state = ADSR_RELEASE;
					release_start = current_sample_number;
					do_release = false;
				}
				break;
			
			case ADSR_DECAY:
				// interpolate from attack_amplitude to decay_amplitude
				t = (current_sample_number - attack_threshold) / float(decay_threshold);
				amp = attack_amplitude * (1.0f - t) + decay_amplitude * t;
				if ((current_sample_number + 1) > ad_threshold) {
					adsr_state = ADSR_SUSTAIN;
				}
				else if (do_release) {
					adsr_state = ADSR_RELEASE;
					release_start = current_sample_number;
					do_release = false;
				}
				break;

			case ADSR_SUSTAIN:
				// hold at sustain_amplitude till key is released
				if (do_release) {
					adsr_state = ADSR_RELEASE;
					release_start = current_sample_number;
					do_release = false;
				}
				amp = sustain_amplitude;
				break;

			case ADSR_RELEASE:
				// interpolate from sustain_amplitude to release_amplitude
				t = (current_sample_number - release_start) / float(release_threshold);
				amp = sustain_amplitude * (1.0f - t) + release_amplitude * t;
				if ((current_sample_number + 1) > (release_start + release_threshold)) {
					adsr_state = ADSR_END;
				}
				break;

			case ADSR_END:
				// hold release_amplitude
				amp = release_amplitude;
				break;

			default:
				amp = 0.0f;
		}

		buffer[i] += s * volume * amp;
		current_sample_number++;
	}
}


//public-facing data:

//global volume control:
Sound::Ramp< float > Sound::volume = Sound::Ramp< float >(1.0f);

//global listener information:
Sound::Listener Sound::listener;

//This audio-mixing callback is defined below:
void mix_audio(void *, Uint8 *buffer_, int len);

//------------------------ public-facing --------------------------------

Sound::Sample::Sample(std::string const &filename) {
	if (filename.size() >= 4 && filename.substr(filename.size()-4) == ".wav") {
		load_wav(filename, &data);
	} else if (filename.size() >= 5 && filename.substr(filename.size()-5) == ".opus") {
		load_opus(filename, &data);
	} else {
		throw std::runtime_error("Sample '" + filename + "' doesn't end in either \".png\" or \".opus\" -- unsure how to load.");
	}
}

Sound::Sample::Sample(std::vector< float > const &data_) : data(data_) {
}




void Sound::lock() {
	if (device) SDL_LockAudioDevice(device);
}

void Sound::unlock() {
	if (device) SDL_UnlockAudioDevice(device);
}

void Sound::init() {
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		std::cerr << "Failed to initialize SDL audio subsytem:\n" << SDL_GetError() << std::endl;
		std::cerr << "  (Will continue without audio.)\n" << std::endl;
		return;
	}

	//Based on the example on https://wiki.libsdl.org/SDL_OpenAudioDevice
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = AUDIO_RATE;
	want.format = AUDIO_F32SYS;
	want.channels = 2;
	want.samples = MIX_SAMPLES;
	want.callback = mix_audio;

	mix_buffer.reserve(MIX_SAMPLES);
	crackle_duration = 200;

	for (int i = 0; i < Sound::NUM_SYNTHS; i++) {
		synths[i].is_on = false;
	}
	
	device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
	if (device == 0) {
		std::cerr << "Failed to open audio device:\n" << SDL_GetError() << std::endl;
		std::cerr << "  (Will continue without audio.)\n" << std::endl;
	} else {
		//start audio playback:
		SDL_PauseAudioDevice(device, 0);
		std::cout << "Audio output initialized." << std::endl;
	}
}


void Sound::shutdown() {
	if (device != 0) {
		//stop audio playback:
		SDL_PauseAudioDevice(device, 1);
		SDL_CloseAudioDevice(device);
		device = 0;
	}
}


std::shared_ptr< Sound::PlayingSample > Sound::play(Sample const &sample, float volume, float pan) {
	std::shared_ptr< Sound::PlayingSample > playing_sample = std::make_shared< Sound::PlayingSample >(sample, volume, pan, false);
	lock();
	playing_samples.emplace_back(playing_sample);
	unlock();
	return playing_sample;
}

std::shared_ptr< Sound::PlayingSample > Sound::play_3D(Sample const &sample, float volume, glm::vec3 const &position, float half_volume_radius) {
	std::shared_ptr< Sound::PlayingSample > playing_sample = std::make_shared< Sound::PlayingSample >(sample, volume, position, half_volume_radius, false);
	lock();
	playing_samples.emplace_back(playing_sample);
	unlock();
	return playing_sample;
}

std::shared_ptr< Sound::PlayingSample > Sound::loop(Sample const &sample, float volume, float pan) {
	std::shared_ptr< Sound::PlayingSample > playing_sample = std::make_shared< Sound::PlayingSample >(sample, volume, pan, true);
	lock();
	playing_samples.emplace_back(playing_sample);
	unlock();
	return playing_sample;
}



std::shared_ptr< Sound::PlayingSample > Sound::loop_3D(Sample const &sample, float volume, glm::vec3 const &position, float half_volume_radius) {
	std::shared_ptr< Sound::PlayingSample > playing_sample = std::make_shared< Sound::PlayingSample >(sample, volume, position, half_volume_radius, true);
	lock();
	playing_samples.emplace_back(playing_sample);
	unlock();
	return playing_sample;
}


void Sound::stop_all_samples() {
	lock();
	for (auto &s : playing_samples) {
		s->stop();
	}
	unlock();
}

void Sound::set_volume(float new_volume, float ramp) {
	lock();
	volume.set(new_volume, ramp);
	unlock();
}

//------------------

void Sound::PlayingSample::set_volume(float new_volume, float ramp) {
	Sound::lock();
	if (!stopping) {
		volume.set(new_volume, ramp);
	}
	Sound::unlock();
}

void Sound::PlayingSample::set_pan(float new_pan, float ramp) {
	if (!(pan.value == pan.value)) return; //ignore if not in '2D' mode
	Sound::lock();
	pan.set(new_pan, ramp);
	Sound::unlock();
}

void Sound::PlayingSample::set_position(glm::vec3 const &new_position, float ramp) {
	if (pan.value == pan.value) return; //ignore if not in '3D' mode
	Sound::lock();
	position.set(new_position, ramp);
	Sound::unlock();
}

void Sound::PlayingSample::set_half_volume_radius(float new_radius, float ramp) {
	if (pan.value == pan.value) return; //ignore if not in '3D' mode
	Sound::lock();
	half_volume_radius.set(new_radius, ramp);
	Sound::unlock();
}

void Sound::PlayingSample::stop(float ramp) {
	Sound::lock();
	if (!(stopping || stopped)) {
		stopping = true;
		volume.target = 0.0f;
		volume.ramp = ramp;
	} else {
		volume.ramp = std::min(volume.ramp, ramp);
	}
	Sound::unlock();
}

//------------------

void Sound::Listener::set_position_right(glm::vec3 const &new_position, glm::vec3 const &new_right, float ramp) {
	Sound::lock();
	position.set(new_position, ramp);
	//some extra code to make sure right is always a unit vector:
	if (new_right == glm::vec3(0.0f)) {
		right.set(glm::vec3(1.0f, 0.0f, 0.0f), ramp);
	} else {
		right.set(glm::normalize(new_right), ramp);
	}
	Sound::unlock();
}

//------------------------ internals --------------------------------


//helper: equal-power panning
inline void compute_pan_weights(float pan, float *left, float *right) {
	//clamp pan to -1 to 1 range:
	pan = std::max(-1.0f, std::min(1.0f, pan));

	//want left^2 + right^2 = 1.0, so use angles:
	float ang = 0.5f * 3.1415926f * (0.5f * (pan + 1.0f));
	*left = std::cos(ang);
	*right = std::sin(ang);
}

//helper: 3D audio panning
void compute_pan_from_listener_and_position(
	glm::vec3 const &listener_position,
	glm::vec3 const &listener_right,
	glm::vec3 const &source_position,
	float source_half_radius,
	float *left, float *right
	) {
	glm::vec3 to = source_position - listener_position;
	float distance = glm::length(to);
	//start by panning based on direction.
	//note that for a LR fade to sound uniform, sound power (squared magnitude) should remain constant.
	if (distance == 0.0f) {
		*left = *right = std::sqrt(2.0f);
	} else {
		//amt ranges from -1 (most left) to 1 (most right):
		float amt = glm::dot(listener_right, to) / distance;
		//turn into an angle from 0.0f (most left) to pi/2 (most right):
		float ang = 0.5f * 3.1415926f * (0.5f * (amt + 1.0f));
		*left = std::cos(ang);
		*right = std::sin(ang);

		//squared distance attenuation is realistic if there are no walls,
		// but I'm going to use linear because it's sounds better to me.
		// (feel free to change it, of course)
		//want att = 0.5f at distance == half_volume_radius
		float att = 1.0f / (1.0f + (distance / source_half_radius));
		*left *= att;
		*right *= att;
	}
}

//helper: ramp updates...
constexpr float const RAMP_STEP = float(MIX_SAMPLES) / float(AUDIO_RATE);

//helper: ...for single values:
void step_value_ramp(Sound::Ramp< float > &ramp) {
	if (ramp.ramp < RAMP_STEP) {
		ramp.value = ramp.target;
		ramp.ramp = 0.0f;
	} else {
		ramp.value += (RAMP_STEP / ramp.ramp) * (ramp.target - ramp.value);
		ramp.ramp -= RAMP_STEP;
	}
}

//helper: ...for 3D positions:
void step_position_ramp(Sound::Ramp< glm::vec3 > &ramp) {
	if (ramp.ramp < RAMP_STEP) {
		ramp.value = ramp.target;
		ramp.ramp = 0.0f;
	} else {
		ramp.value = glm::mix(ramp.value, ramp.target, RAMP_STEP / ramp.ramp);
		ramp.ramp -= RAMP_STEP;
	}
}

//helper: ...for 3D directions:
void step_direction_ramp(Sound::Ramp< glm::vec3 > &ramp) {
	if (ramp.ramp < RAMP_STEP) {
		ramp.value = ramp.target;
		ramp.ramp = 0.0f;
	} else {
		//find normal to the plane containing value and target:
		glm::vec3 norm = glm::cross(ramp.value, ramp.target);
		if (norm == glm::vec3(0.0f)) {
			if (ramp.target.x <= ramp.target.y && ramp.target.x <= ramp.target.z) {
				norm = glm::vec3(1.0f, 0.0f, 0.0f);
			} else if (ramp.target.y <= ramp.target.z) {
				norm = glm::vec3(0.0f, 1.0f, 0.0f);
			} else {
				norm = glm::vec3(0.0f, 0.0f, 1.0f);
			}
			norm -= ramp.target * glm::dot(ramp.target, norm);
		}
		norm = glm::normalize(norm);
		//find perpendicular to target in this plane:
		glm::vec3 perp = glm::cross(norm, ramp.target);

		//find angle from target to value:
		float angle = std::acos(glm::clamp(glm::dot(ramp.value, ramp.target), -1.0f, 1.0f));

		//figure out new target value by moving angle toward target:
		angle *= (ramp.ramp - RAMP_STEP) / ramp.ramp;

		ramp.value = ramp.target * std::cos(angle) + perp * std::sin(angle);
		ramp.ramp -= RAMP_STEP;
	}
}


//The audio callback -- invoked by SDL when it needs more sound to play:
void mix_audio(void *, Uint8 *buffer_, int len) {
	assert(buffer_); //should always have some audio buffer
	
	// for crackle effect
	static std::mt19937 mt;

	struct LR {
		float l;
		float r;
	};
	static_assert(sizeof(LR) == 8, "Sample is packed");
	assert(len == MIX_SAMPLES * sizeof(LR)); //should always have the expected number of samples
	LR *buffer = reinterpret_cast< LR * >(buffer_);

	int on_counter = 0;

	// zero out buffer
	for (int i = 0; i < MIX_SAMPLES; i++) {
		mix_buffer[i] = 0;
	}

	// only run active synths
	for (int i = 0; i < Sound::NUM_SYNTHS; i++) {
		if (synths[i].is_on) {
			// get the ith synth to add its samples to mix_buffer
			synths[i].generate_samples(MIX_SAMPLES, mix_buffer);
			on_counter++;
		}
	}

	// don't waste time running LPF and crackle for empty samples
	if (on_counter == 0) {
		for (auto s = 0; s < MIX_SAMPLES; s++) {
			buffer[s].l = 0;
			buffer[s].r = 0;
		}
	}
	else {
		
		// budget low pass filter (ye olde average) to make the synth sound bearable
		// output[i] = avg(sample[i-2], sample[i-1], sample[i], sample[i+1], sample[i+2])
		// an actually competent implementation would use a FFT and a gaussian filter shape or something

		// first, deal with edge cases
		buffer[0].l = mix_buffer[0] / on_counter;
		buffer[1].l = mix_buffer[1] / on_counter;
		buffer[MIX_SAMPLES-1].l = mix_buffer[MIX_SAMPLES-1] / on_counter;
		buffer[MIX_SAMPLES-2].l = mix_buffer[MIX_SAMPLES-2] / on_counter;
		buffer[MIX_SAMPLES-3].l = mix_buffer[MIX_SAMPLES-3] / on_counter;
		buffer[0].r = mix_buffer[0] / on_counter;
		buffer[1].r = mix_buffer[1] / on_counter;
		buffer[MIX_SAMPLES-1].r = mix_buffer[MIX_SAMPLES-1] / on_counter;
		buffer[MIX_SAMPLES-2].r = mix_buffer[MIX_SAMPLES-2] / on_counter;
		buffer[MIX_SAMPLES-3].r = mix_buffer[MIX_SAMPLES-3] / on_counter;

		float running_buffer = 0.0f;
		
		float crackle_factor = 1.0f;

		// initialize running sum
		for (int i = 0; i < 5; i++) {
			running_buffer += mix_buffer[i] / on_counter;
		}

		// do LPF and crackle
		for (uint32_t s = 2; s < MIX_SAMPLES-3; ++s) {
			global_sample++;

			// create crackling "sparks" in the output sound, as if our player character is malfunctioning
			// this relieves some of the suffocation of the monotonous bassline and drums
			// TODO: use rain sounds etc. to do this instead
			if (global_sample >= next_crackle) {
				crackle_duration = 2000 + uint64_t((mt()/float(mt.max())) * 2000);
				next_crackle = global_sample + 5000 + uint64_t((mt()/float(mt.max())) * 50000);
				crackle_amount = 0.8f + 0.2f * mt()/float(mt.max());
			}
			if (crackle_duration == 0) {
				crackle_factor = 1.0f;
			}
			else {
				crackle_duration--;
				crackle_factor = (1.0f - crackle_amount) * (mt()/float(mt.max())) + crackle_amount;
			}
			float mix = crackle_factor * running_buffer / 5.0f;
			buffer[s].l = mix;
			buffer[s].r = mix;

			// update the running sum so we don't recompute ALL of the sums every single time
			running_buffer = running_buffer - (mix_buffer[s-2]/on_counter) + (mix_buffer[s+3]/on_counter);
		}
	}
}


