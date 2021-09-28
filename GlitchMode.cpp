#include "GlitchMode.hpp"

#include "LitColorTextureProgram.hpp"

#include "DrawLines.hpp"
#include "Mesh.hpp"
#include "Load.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>

extern Sound::GlitchSynth synths[Sound::NUM_SYNTHS];

GLuint glitch_meshes_for_lit_color_texture_program = 0;

Load< MeshBuffer > glitch_meshes(LoadTagDefault, []() -> MeshBuffer const * {
	MeshBuffer const *ret = new MeshBuffer(data_path("glitch.pnct"));
	glitch_meshes_for_lit_color_texture_program = ret->make_vao_for_program(lit_color_texture_program->program);
	return ret;
});

Load< Scene > glitch_scene(LoadTagDefault, []() -> Scene const * {
	return new Scene(data_path("glitch.scene"), [&](Scene &scene, Scene::Transform *transform, std::string const &mesh_name){
		Mesh const &mesh = glitch_meshes->lookup(mesh_name);
		scene.drawables.emplace_back(transform);
		Scene::Drawable &drawable = scene.drawables.back();

		drawable.pipeline = lit_color_texture_program_pipeline;

		drawable.pipeline.vao = glitch_meshes_for_lit_color_texture_program;
		drawable.pipeline.type = mesh.type;
		drawable.pipeline.start = mesh.start;
		drawable.pipeline.count = mesh.count;

	});
});

GlitchMode::GlitchMode() : scene(*glitch_scene) {
	static std::mt19937 mt;

	//get pointer to camera for convenience:
	if (scene.cameras.size() != 1) throw std::runtime_error("Expecting scene to have exactly one camera, but it has " + std::to_string(scene.cameras.size()));
	camera = &scene.cameras.front();

	// get handles to spheres
	for (auto& transform : scene.transforms) {
		if (transform.name == "Sphere") {
			sphere_transforms[0] = &transform;
		}
		else if (transform.name == "Sphere.001") {
			sphere_transforms[1] = &transform;
		}
		else if (transform.name == "Sphere.002") {
			sphere_transforms[2] = &transform;
		}
		else if (transform.name == "Sphere.003") {
			sphere_transforms[3] = &transform;
		}
		else if (transform.name == "Sphere.004") {
			sphere_transforms[4] = &transform;
		}
		else if (transform.name == "Cylinder") {
			cylinder_transform = &transform;
		}
	}

	assert(sphere_transforms[0]);
	assert(sphere_transforms[1]);
	assert(sphere_transforms[2]);
	assert(sphere_transforms[3]);
	assert(sphere_transforms[4]);
	assert(cylinder_transform);
	cylinder_position = cylinder_transform->position;
	
	

	for (int i = 0; i < 5; i++) {
		sphere_transforms[i]->position.z = 8.0f * mt()/float(mt.max()) - 4.0f;
		sphere_transforms[i]->position.x = 8.0f * mt()/float(mt.max()) - 4.0f;
		sphere_transforms[i]->position.y = 8.0f * mt()/float(mt.max()) - 4.0f;
	}


	// TODO: set these using an asset pipeline
	// source for GlitchSynth in Sound.hpp and Sound.cpp
	// despite a moderate amount of effort, it still sounds pretty bad
	// TODO: implement an actually decent LPF, reverb, and compressor
	synths[BASS_SYNTH].set_attack(1.0f, 100);
	synths[BASS_SYNTH].set_decay(0.8f, 200);
	synths[BASS_SYNTH].set_sustain(0.8f);
	synths[BASS_SYNTH].set_release(0.0f, 20000);
	synths[BASS_SYNTH].osc = Sound::GlitchSynth::OSC_SINE;
	synths[BASS_SYNTH].volume = 1.0f;

	synths[HAT_SYNTH].set_attack(1.0f, 100);
	synths[HAT_SYNTH].set_decay(0.3f, 500);
	synths[HAT_SYNTH].set_sustain(0.0f);
	synths[HAT_SYNTH].set_release(0.0f, 1);
	synths[HAT_SYNTH].osc = Sound::GlitchSynth::OSC_NOISE;
	synths[HAT_SYNTH].volume = 0.5f;

	synths[SNARE_SYNTH].set_attack(1.0f, 1000);
	synths[SNARE_SYNTH].set_decay(0.3f, 2000);
	synths[SNARE_SYNTH].set_sustain(0.0f);
	synths[SNARE_SYNTH].set_release(0.0f, 3000);
	synths[SNARE_SYNTH].osc = Sound::GlitchSynth::OSC_SAW;
	synths[SNARE_SYNTH].volume = 0.5f;
	
	synths[KICK_SYNTH].set_attack(1.0f, 500);
	synths[KICK_SYNTH].set_decay(0.8f, 500);
	synths[KICK_SYNTH].set_sustain(0.0f);
	synths[KICK_SYNTH].set_release(0.0f, 1);
	synths[KICK_SYNTH].osc = Sound::GlitchSynth::OSC_SQUARE;
	synths[KICK_SYNTH].volume = 1.0f;

	synths[PLAYER_LEAD_SYNTH].set_attack(1.0f, 500);
	synths[PLAYER_LEAD_SYNTH].set_decay(0.7f, 500);
	synths[PLAYER_LEAD_SYNTH].set_sustain(0.7f);
	synths[PLAYER_LEAD_SYNTH].set_release(0.0f, 10000);
	synths[PLAYER_LEAD_SYNTH].osc = Sound::GlitchSynth::OSC_SINE;
	synths[PLAYER_LEAD_SYNTH].volume = 0.5f;

	synths[PLAYER_SUPER_SYNTH].set_attack(1.0f, 500);
	synths[PLAYER_SUPER_SYNTH].set_decay(0.7f, 500);
	synths[PLAYER_SUPER_SYNTH].set_sustain(0.7f);
	synths[PLAYER_SUPER_SYNTH].set_release(0.0f, 10000);
	synths[PLAYER_SUPER_SYNTH].osc = Sound::GlitchSynth::OSC_SAW;
	synths[PLAYER_SUPER_SYNTH].volume = 0.1f;


	loops.emplace_back(synths[BASS_SYNTH]);
	loops.emplace_back(synths[HAT_SYNTH]);
	loops.emplace_back(synths[SNARE_SYNTH]);
	loops.emplace_back(synths[KICK_SYNTH]);

	// 4 indices per beat.
	// loop notation:
	// 	1. positive number: start playing note id, 1 means C0, 2 means C#0, and so on
	// 	2. any negative number: stop playing current note - triggers ADSR_RELEASE
	// 	3. 0: ignore, just for timing

	// TODO: generate these procedurally for some variety, hardcoding for now

	// garbage bassline
	// warning: this sounds terrible
	for (int i = 0; i < 32; i++) {
		loops[BASS_SYNTH].note_commands.push_back(24 + mt() % 12);
		for (int _i = 0; _i < 11; _i++) {
			loops[BASS_SYNTH].note_commands.push_back(0);
		}
		loops[BASS_SYNTH].note_commands.push_back(-1);
		loops[BASS_SYNTH].note_commands.push_back(0);
		loops[BASS_SYNTH].note_commands.push_back(0);
		loops[BASS_SYNTH].note_commands.push_back(0);
	}
	
	// monotonous hi-hat
	loops[HAT_SYNTH].note_commands = {1, -1, 0, 0};

	// uninspired snare
	loops[SNARE_SYNTH].note_commands = {0, 0, 0, 0,  0, 0, 0, 0,  36+1, -1, 0, 0,  0, 0, 0, 0};

	// insipid kick
	loops[KICK_SYNTH].note_commands = {12+1, -1, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 24+1, -1,
									   12+1, -1, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0};



}

GlitchMode::~GlitchMode() {
}


bool GlitchMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	// map keys to synth notes
	if (evt.type == SDL_KEYDOWN) {
		int played_note = -1;
		if (evt.key.keysym.sym == SDLK_a) {
			if (!a_b.pressed){
				synths[PLAYER_LEAD_SYNTH].is_on = true;
				synths[PLAYER_SUPER_SYNTH].is_on = true;
				synths[PLAYER_LEAD_SYNTH].play(freq_table[0]);
				synths[PLAYER_SUPER_SYNTH].play(freq_table[(0+7)%12] / 2.0f);
				played_note = 0;
			}
			a_b.pressed = true;
		}
		else if (evt.key.keysym.sym == SDLK_w) {
			if (!w_b.pressed){
				synths[PLAYER_LEAD_SYNTH].is_on = true;
				synths[PLAYER_SUPER_SYNTH].is_on = true;
				synths[PLAYER_LEAD_SYNTH].play(freq_table[1]);
				synths[PLAYER_SUPER_SYNTH].play(freq_table[(1+7)%12]  / 2.0f);
				played_note = 1;
			}
			w_b.pressed = true;
		}
		else if (evt.key.keysym.sym == SDLK_s) {
			if (!s_b.pressed){
				synths[PLAYER_LEAD_SYNTH].is_on = true;
				synths[PLAYER_SUPER_SYNTH].is_on = true;
				synths[PLAYER_LEAD_SYNTH].play(freq_table[2]);
				synths[PLAYER_SUPER_SYNTH].play(freq_table[(2+7)%12]  / 2.0f);
				played_note = 2;
			}
			s_b.pressed = true;
		}
		else if (evt.key.keysym.sym == SDLK_e) {
			if (!e_b.pressed){
				synths[PLAYER_LEAD_SYNTH].is_on = true;
				synths[PLAYER_SUPER_SYNTH].is_on = true;
				synths[PLAYER_LEAD_SYNTH].play(freq_table[3]);
				synths[PLAYER_SUPER_SYNTH].play(freq_table[(3+7)%12] / 2.0f);
				played_note = 3;
			}
			e_b.pressed = true;
		}
		else if (evt.key.keysym.sym == SDLK_d) {
			if (!d_b.pressed){
				synths[PLAYER_LEAD_SYNTH].is_on = true;
				synths[PLAYER_SUPER_SYNTH].is_on = true;
				synths[PLAYER_LEAD_SYNTH].play(freq_table[4]);
				synths[PLAYER_SUPER_SYNTH].play(freq_table[(4+7)%12] / 2.0f);
				played_note = 4;
			}
			d_b.pressed = true;
		}
		else if (evt.key.keysym.sym == SDLK_f) {
			if (!f_b.pressed){
				synths[PLAYER_LEAD_SYNTH].is_on = true;
				synths[PLAYER_SUPER_SYNTH].is_on = true;
				synths[PLAYER_LEAD_SYNTH].play(freq_table[5]);
				synths[PLAYER_SUPER_SYNTH].play(freq_table[(5+7)%12] / 2.0f);
				played_note = 5;
			}
			f_b.pressed = true;
		}
		else if (evt.key.keysym.sym == SDLK_t) {
			if (!t_b.pressed){
				synths[PLAYER_LEAD_SYNTH].is_on = true;
				synths[PLAYER_SUPER_SYNTH].is_on = true;
				synths[PLAYER_LEAD_SYNTH].play(freq_table[6]);
				synths[PLAYER_SUPER_SYNTH].play(freq_table[(6+7)%12] / 2.0f);
				played_note = 6;
			}
			t_b.pressed = true;
		}
		else if (evt.key.keysym.sym == SDLK_g) {
			if (!g_b.pressed){
				synths[PLAYER_LEAD_SYNTH].is_on = true;
				synths[PLAYER_SUPER_SYNTH].is_on = true;
				synths[PLAYER_LEAD_SYNTH].play(freq_table[7]);
				synths[PLAYER_SUPER_SYNTH].play(freq_table[(7+7)%12] / 2.0f);
				played_note = 7;
			}
			g_b.pressed = true;
		}
		else if (evt.key.keysym.sym == SDLK_y) {
			if (!y_b.pressed){
				synths[PLAYER_LEAD_SYNTH].is_on = true;
				synths[PLAYER_SUPER_SYNTH].is_on = true;
				synths[PLAYER_LEAD_SYNTH].play(freq_table[8]);
				synths[PLAYER_SUPER_SYNTH].play(freq_table[(8+7)%12] / 2.0f);
				played_note = 8;
			}
			y_b.pressed = true;
		}
		else if (evt.key.keysym.sym == SDLK_h) {
			if (!h_b.pressed){
				synths[PLAYER_LEAD_SYNTH].is_on = true;
				synths[PLAYER_SUPER_SYNTH].is_on = true;
				synths[PLAYER_LEAD_SYNTH].play(freq_table[9]);
				synths[PLAYER_SUPER_SYNTH].play(freq_table[(9+7)%12] / 2.0f);
				played_note = 9;
			}
			h_b.pressed = true;
		}
		else if (evt.key.keysym.sym == SDLK_u) {
			if (!u_b.pressed){
				synths[PLAYER_LEAD_SYNTH].is_on = true;
				synths[PLAYER_SUPER_SYNTH].is_on = true;
				synths[PLAYER_LEAD_SYNTH].play(freq_table[10]);
				synths[PLAYER_SUPER_SYNTH].play(freq_table[(10+7)%12] / 2.0f);
				played_note = 10;
			}
			u_b.pressed = true;
		}
		else if (evt.key.keysym.sym == SDLK_j) {
			if (!j_b.pressed){
				synths[PLAYER_LEAD_SYNTH].is_on = true;
				synths[PLAYER_SUPER_SYNTH].is_on = true;
				synths[PLAYER_LEAD_SYNTH].play(freq_table[11]);
				synths[PLAYER_SUPER_SYNTH].play(freq_table[(11+7)%12] / 2.0f);
				played_note = 11;
			}
			j_b.pressed = true;
		}

		if (new_target) {
			if (played_note == target_note) {
				direction = UP;
			}
			else {
				direction = DOWN;
			}
			new_target = false;
		}
		
	} else if (evt.type == SDL_KEYUP) {
		if (evt.key.keysym.sym == SDLK_a) {
			synths[PLAYER_LEAD_SYNTH].do_release = true;
			synths[PLAYER_SUPER_SYNTH].do_release = true;
			a_b.pressed = false;
		}
		else if (evt.key.keysym.sym == SDLK_w) {
			synths[PLAYER_LEAD_SYNTH].do_release = true;
			synths[PLAYER_SUPER_SYNTH].do_release = true;
			w_b.pressed = false;
		}
		else if (evt.key.keysym.sym == SDLK_s) {
			synths[PLAYER_LEAD_SYNTH].do_release = true;
			synths[PLAYER_SUPER_SYNTH].do_release = true;
			s_b.pressed = false;
		}
		else if (evt.key.keysym.sym == SDLK_e) {
			synths[PLAYER_LEAD_SYNTH].do_release = true;
			synths[PLAYER_SUPER_SYNTH].do_release = true;
			e_b.pressed = false;
		}
		else if (evt.key.keysym.sym == SDLK_d) {
			synths[PLAYER_LEAD_SYNTH].do_release = true;
			synths[PLAYER_SUPER_SYNTH].do_release = true;
			d_b.pressed = false;
		}
		else if (evt.key.keysym.sym == SDLK_f) {
			synths[PLAYER_LEAD_SYNTH].do_release = true;
			synths[PLAYER_SUPER_SYNTH].do_release = true;
			f_b.pressed = false;
		}
		else if (evt.key.keysym.sym == SDLK_t) {
			synths[PLAYER_LEAD_SYNTH].do_release = true;
			synths[PLAYER_SUPER_SYNTH].do_release = true;
			t_b.pressed = false;
		}
		else if (evt.key.keysym.sym == SDLK_g) {
			synths[PLAYER_LEAD_SYNTH].do_release = true;
			synths[PLAYER_SUPER_SYNTH].do_release = true;
			g_b.pressed = false;
		}
		else if (evt.key.keysym.sym == SDLK_y) {
			synths[PLAYER_LEAD_SYNTH].do_release = true;
			synths[PLAYER_SUPER_SYNTH].do_release = true;
			y_b.pressed = false;
		}
		else if (evt.key.keysym.sym == SDLK_h) {
			synths[PLAYER_LEAD_SYNTH].do_release = true;
			synths[PLAYER_SUPER_SYNTH].do_release = true;
			h_b.pressed = false;
		}
		else if (evt.key.keysym.sym == SDLK_u) {
			synths[PLAYER_LEAD_SYNTH].do_release = true;
			synths[PLAYER_SUPER_SYNTH].do_release = true;
			u_b.pressed = false;
		}
		else if (evt.key.keysym.sym == SDLK_j) {
			synths[PLAYER_LEAD_SYNTH].do_release = true;
			synths[PLAYER_SUPER_SYNTH].do_release = true;
			j_b.pressed = false;
		}
	}

	return false;
}

void GlitchMode::update(float elapsed) {
	static std::mt19937 mt;

	// update synth loops
	// TODO: this is not a great frame-independent update loop, fix lag
	loop_delay += elapsed;
	if (loop_delay >= 0.13f) {
		for (int i = 0; i < loops.size(); i++) {
			int command = loops[i].note_commands[loops[i].current_idx];
			loops[i].current_idx = (loops[i].current_idx + 1) % (loops[i].note_commands.size());
			if (command != 0) {
				if (command > 0) {
					int idx = (command - 1) % 12;
					int octave = (command - 1) / 12 - 4;
					float freq = freq_table[idx] * powf(2.0f, float(octave));
					loops[i].synth.is_on = true;
					loops[i].synth.play(freq);
					if (i == BASS_SYNTH) {
						target_note = idx;
						new_target = true;
					}
				}
				else {
					loops[i].synth.do_release = true;
				}
			}
		}
		loop_delay = 0.f;
	}

	// move spheres
	for (int i = 0; i < 5; i++) {
		float z = sphere_transforms[i]->position.z;
		if (direction == UP) {
			z = z - elapsed * 5.f;
			if (z < -6.f) z = 6.f;
			cylinder_transform->position.x = cylinder_position.x;
		}
		else {
			z = z + elapsed * 20.f;
			if (z > 6.0f) z = -6.0f;
			cylinder_transform->position.x = cylinder_position.x + mt()/float(mt.max()) - 0.5f;
		}
		sphere_transforms[i]->position.z = z;
	}
}

void GlitchMode::draw(glm::uvec2 const &drawable_size) {
	//update camera aspect ratio for drawable:
	camera->aspect = float(drawable_size.x) / float(drawable_size.y);

	//set up light type and position for lit_color_texture_program:
	// TODO: consider using the Light(s) in the scene to do this
	glUseProgram(lit_color_texture_program->program);
	glUniform1i(lit_color_texture_program->LIGHT_TYPE_int, 1);
	glUniform3fv(lit_color_texture_program->LIGHT_DIRECTION_vec3, 1, glm::value_ptr(glm::vec3(0.0f, 0.0f,-1.0f)));
	glUniform3fv(lit_color_texture_program->LIGHT_ENERGY_vec3, 1, glm::value_ptr(glm::vec3(1.0f, 1.0f, 0.95f)));
	glUseProgram(0);

	if (direction == UP) {
		glClearColor(0.5f, 0.5f, 0.7f, 1.0f);
	}
	else {
		glClearColor(0.7f, 0.5f, 0.7f, 1.0f);
	}
	glClearDepth(1.0f); //1.0 is actually the default value to clear the depth buffer to, but FYI you can change it.
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS); //this is the default depth comparison function, but FYI you can change it.

	scene.draw(*camera);
	GL_ERRORS();
}
