#include "Mode.hpp"

#include "Scene.hpp"
#include "Sound.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

// B A S S
constexpr int BASS_SYNTH = 0; 

// Drums
constexpr int HAT_SYNTH = 1;
constexpr int SNARE_SYNTH = 2;
constexpr int KICK_SYNTH = 3;

// meowdleeeooooowwldelooww
constexpr int PLAYER_LEAD_SYNTH = 4;

// copies the lead half-octave higher for that layered "supersaw" sound
constexpr int PLAYER_SUPER_SYNTH = 5;

// note frequencies analyzed from a real synth
// TODO: not quite correct
static float freq_table[12] = {
	261.f,  //C4
	277.f,  //C#4
	293.f,  //D4
	311.f,  //D#4
	329.f,  //E4
	349.f,  //F4
	370.f,  //F#4
	391.f,  //G4
	415.f,  //G#4
	440.f,  //A4
	467.f,  //A#4
	494.f   //B4
};

struct GlitchMode : Mode {
	GlitchMode();
	virtual ~GlitchMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	//input tracking:
	struct Button {
		uint8_t downs = 0;
		uint8_t pressed = 0;
	} a_b, w_b, s_b, e_b, d_b, f_b, t_b, g_b, y_b, h_b, u_b, j_b; // keyboard synth buttons

	struct SynthLoop {
		// rudimentary note-on, note-off commands
		std::vector<int> note_commands;

		// reference to the synth playing this loop
		Sound::GlitchSynth& synth;

		int current_idx = 0;

		SynthLoop(Sound::GlitchSynth& s) : synth(s) {}

	};

	enum {
		UP,
		DOWN
	} direction = UP;
	int target_note = -1;
	bool new_target = false;
	std::vector<SynthLoop> loops;

	//local copy of the game scene (so code can change it during gameplay):
	Scene scene;

	float loop_delay = 0.0f;

	Scene::Transform* sphere_transforms[5] = {0};
	Scene::Transform* cylinder_transform = nullptr;
	glm::vec3 cylinder_position;
	//camera:
	Scene::Camera *camera = nullptr;

};
