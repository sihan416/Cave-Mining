#include "load_save_png.hpp"
#include "GL.hpp"

#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <ctime>
#include <random>
#include <thread>
#include <unordered_map>

static GLuint compile_shader(GLenum type, std::string const &source);
static GLuint link_program(GLuint vertex_shader, GLuint fragment_shader);

int main(int argc, char **argv) {
	//Configuration:
	struct {
		std::string title = "Game1: Text/Tiles";
		glm::uvec2 size = glm::uvec2(960, 720);
	} config;

	//------------  initialization ------------

	//Initialize SDL library:
	SDL_Init(SDL_INIT_VIDEO);

	//Ask for an OpenGL context version 3.3, core profile, enable debug:
	SDL_GL_ResetAttributes();
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

	//create window:
	SDL_Window *window = SDL_CreateWindow(
		config.title.c_str(),
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		config.size.x, config.size.y,
		SDL_WINDOW_OPENGL /*| SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI*/
	);

	if (!window) {
		std::cerr << "Error creating SDL window: " << SDL_GetError() << std::endl;
		return 1;
	}

	//Create OpenGL context:
	SDL_GLContext context = SDL_GL_CreateContext(window);

	if (!context) {
		SDL_DestroyWindow(window);
		std::cerr << "Error creating OpenGL context: " << SDL_GetError() << std::endl;
		return 1;
	}

	#ifdef _WIN32
	//On windows, load OpenGL extensions:
	if (!init_gl_shims()) {
		std::cerr << "ERROR: failed to initialize shims." << std::endl;
		return 1;
	}
	#endif

	//Set VSYNC + Late Swap (prevents crazy FPS):
	if (SDL_GL_SetSwapInterval(-1) != 0) {
		std::cerr << "NOTE: couldn't set vsync + late swap tearing (" << SDL_GetError() << ")." << std::endl;
		if (SDL_GL_SetSwapInterval(1) != 0) {
			std::cerr << "NOTE: couldn't set vsync (" << SDL_GetError() << ")." << std::endl;
		}
	}

	//Hide mouse cursor (note: showing can be useful for debugging):
	SDL_ShowCursor(SDL_DISABLE);

	//------------ opengl objects / game assets ------------

	//texture:
	GLuint tex = 0;
	glm::uvec2 tex_size = glm::uvec2(0,0);

	{ //load texture 'tex':
		std::vector< uint32_t > data;
		if (!load_png("atlas.png", &tex_size.x, &tex_size.y, &data, LowerLeftOrigin)) {
			std::cerr << "Failed to load texture." << std::endl;
			exit(1);
		}
		//create a texture object:
		glGenTextures(1, &tex);
		//bind texture object to GL_TEXTURE_2D:
		glBindTexture(GL_TEXTURE_2D, tex);
		//upload texture data from data:
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_size.x, tex_size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, &data[0]);
		//set texture sampling parameters:
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}

	//shader program:
	GLuint program = 0;
	GLuint program_Position = 0;
	GLuint program_TexCoord = 0;
	GLuint program_Color = 0;
	GLuint program_mvp = 0;
	GLuint program_tex = 0;
	{ //compile shader program:
		GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER,
			"#version 330\n"
			"uniform mat4 mvp;\n"
			"in vec4 Position;\n"
			"in vec2 TexCoord;\n"
			"in vec4 Color;\n"
			"out vec2 texCoord;\n"
			"out vec4 color;\n"
			"void main() {\n"
			"	gl_Position = mvp * Position;\n"
			"	color = Color;\n"
			"	texCoord = TexCoord;\n"
			"}\n"
		);

		GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
			"#version 330\n"
			"uniform sampler2D tex;\n"
			"in vec4 color;\n"
			"in vec2 texCoord;\n"
			"out vec4 fragColor;\n"
			"void main() {\n"
			"	fragColor = texture(tex, texCoord) * color;\n"
			"}\n"
		);

		program = link_program(fragment_shader, vertex_shader);

		//look up attribute locations:
		program_Position = glGetAttribLocation(program, "Position");
		if (program_Position == -1U) throw std::runtime_error("no attribute named Position");
		program_TexCoord = glGetAttribLocation(program, "TexCoord");
		if (program_TexCoord == -1U) throw std::runtime_error("no attribute named TexCoord");
		program_Color = glGetAttribLocation(program, "Color");
		if (program_Color == -1U) throw std::runtime_error("no attribute named Color");

		//look up uniform locations:
		program_mvp = glGetUniformLocation(program, "mvp");
		if (program_mvp == -1U) throw std::runtime_error("no uniform named mvp");
		program_tex = glGetUniformLocation(program, "tex");
		if (program_tex == -1U) throw std::runtime_error("no uniform named tex");
	}

	//vertex buffer:
	GLuint buffer = 0;
	{ //create vertex buffer
		glGenBuffers(1, &buffer);
		glBindBuffer(GL_ARRAY_BUFFER, buffer);
	}

	struct Vertex {
		Vertex(glm::vec2 const &Position_, glm::vec2 const &TexCoord_, glm::u8vec4 const &Color_) :
			Position(Position_), TexCoord(TexCoord_), Color(Color_) { }
		glm::vec2 Position;
		glm::vec2 TexCoord;
		glm::u8vec4 Color;
	};
	static_assert(sizeof(Vertex) == 20, "Vertex is nicely packed.");

	//vertex array object:
	GLuint vao = 0;
	{ //create vao and set up binding:
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);
		glVertexAttribPointer(program_Position, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0);
		glVertexAttribPointer(program_TexCoord, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (GLbyte *)0 + sizeof(glm::vec2));
		glVertexAttribPointer(program_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (GLbyte *)0 + sizeof(glm::vec2) + sizeof(glm::vec2));
		glEnableVertexAttribArray(program_Position);
		glEnableVertexAttribArray(program_TexCoord);
glEnableVertexAttribArray(program_Color);
	}

	//------------ sprite info ------------
	struct SpriteInfo {
		glm::vec2 min_uv = glm::vec2(0.0f);
		glm::vec2 max_uv = glm::vec2(1.0f);
		glm::vec2 rad = glm::vec2(0.5f);
	};

	//Hard coded sprite infos since pretty straight forward
	std::unordered_map<std::string, SpriteInfo> atlasInfos;
	atlasInfos["base"] = { glm::vec2(0.0f,0.9f),glm::vec2(0.1f,1.0f),glm::vec2(0.5f) };
	atlasInfos["rock"] = { glm::vec2(0.1f,0.9f),glm::vec2(0.2f,1.0f),glm::vec2(0.5f) };
	atlasInfos["char"] = { glm::vec2(0.3f,0.9f),glm::vec2(0.4f,1.0f),glm::vec2(0.5f) };
	atlasInfos["treasure"] = { glm::vec2(0.4f,0.9f),glm::vec2(0.5f,1.0f),glm::vec2(0.5f) };
	atlasInfos["prompt"] = { glm::vec2(0.0f,0.8f),glm::vec2(0.8f,0.9f),glm::vec2(4.0f,0.5f) };
	atlasInfos["end"] = { glm::vec2(0.0f,0.7f),glm::vec2(0.6f,0.8f),glm::vec2(3.0f,0.5f) };

	auto load_sprite = [&atlasInfos](std::string const &name) -> SpriteInfo {
		return atlasInfos[name];
	};


	//------------ game state ------------

	glm::vec2 mouse = glm::vec2(0.0f, 0.0f); //mouse position in [-1,1]x[-1,1] coordinates
	int curx = 5, cury = 5;
	int oldx = 5, oldy = 5;
	bool displayMine = false;
	bool displayEnd = false;
	bool won = false;
	struct tile {
		bool hasRock = false;
		bool hasTreasure = false;
		bool hasBase = true;
		bool canDel = true;
	};

	tile board[11][11];
	auto genBoard = [&board]() {
		std::mt19937 mt_rand((unsigned int)time(NULL));
		int treasureLoc = mt_rand() % 121;
		if (treasureLoc == 60)
			treasureLoc++;
		std::cout << treasureLoc << std::endl;
		for (int i = 0; i < 11; i++) {
			for (int j = 0; j < 11; j++) {
				if (i * 11 + j == treasureLoc) {
					board[i][j] = { true,true,true,false };
				}
				if (mt_rand() % 5 == 1)
					board[i][j] = { true,false,true,true };
				else
					board[i][j] = { false,false,true,true };
			}
		}
		int delStart = mt_rand() % 121;
		int xOffset = delStart / 11;
		int yOffset = delStart % 11;
		for (int i = 0; i < 11; i++) {
			for (int j = 0; j < 11; j++) {
				int x = (i + xOffset) % 11;
				int y = (j + yOffset) % 11;
				tile* temp = &board[x][y];
				if (temp->canDel && !(x == 5 && y == 5) && mt_rand() % 2 == 1) {
					temp->hasBase = false;
					for (int k = -1; k < 2; k++) {
						for (int l = -1; l < 2; l++) {
							if (x + k >= 0 && x + k <= 10 && y + l >= 0 && y + l <= 10)
								board[x + k][y + l].canDel = false;
						}
					}

				}
			}
		}
		board[treasureLoc/11][treasureLoc%11] = { true,true,true,false };
		std::cout << treasureLoc / 11 << std::endl;
		std::cout << treasureLoc % 11 << std::endl;
		board[5][5] = { false,false,true,false };
	};

	genBoard();

	struct {
		glm::vec2 at = glm::vec2(0.0f, 0.0f);
		glm::vec2 radius = glm::vec2(10.0f, 10.0f);
	} camera;
	//correct radius for aspect ratio:
	camera.radius.x = camera.radius.y * (float(config.size.x) / float(config.size.y));

	//------------ game loop ------------

	bool should_quit = false;
	while (true) {
		static SDL_Event evt;
		while (SDL_PollEvent(&evt) == 1) {
			//handle input:
			if (evt.type == SDL_MOUSEMOTION) {
				mouse.x = (evt.motion.x + 0.5f) / float(config.size.x) * 2.0f - 1.0f;
				mouse.y = (evt.motion.y + 0.5f) / float(config.size.y) *-2.0f + 1.0f;
			} else if (evt.type == SDL_MOUSEBUTTONDOWN) {
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_SPACE) {
				if (board[curx][cury].hasRock){
					board[curx][cury].hasRock = false;
					displayMine = false;
					if (board[curx][cury].hasTreasure) {
						displayEnd = true;
					}
				}
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_w) {
				if (cury != 10 && board[curx][cury + 1].hasBase) {
					oldy = cury;
					cury = oldy + 1;
				}
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_a) {
				if (curx != 0 && board[curx - 1][cury].hasBase) {
					oldx = curx;
					curx = oldx - 1;
				}
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_s) {
				if (cury != 0 && board[curx][cury - 1].hasBase) {
					oldy = cury;
					cury = oldy - 1;
				}
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_d) {
				if (curx != 10 && board[curx + 1][cury].hasBase) {
					oldx = curx;
					curx = oldx + 1;
				}
			} else if (evt.type == SDL_KEYDOWN && evt.key.keysym.sym == SDLK_ESCAPE) {
				should_quit = true;
			} else if (evt.type == SDL_QUIT) {
				should_quit = true;
				break;
			}
		}
		if (should_quit) {
			if(won)
				std::this_thread::sleep_for(std::chrono::seconds(3));
			break;
		}

		auto current_time = std::chrono::high_resolution_clock::now();
		static auto previous_time = current_time;
		float elapsed = std::chrono::duration< float >(current_time - previous_time).count();
		previous_time = current_time;

		{ //update game state:
			(void)elapsed;
			if (board[curx][cury].hasRock)
				displayMine = true;
			else
				displayMine = false;
		}

		//draw output:
		glClearColor(0.0, 0.0, 0.0, 0.0);
		glClear(GL_COLOR_BUFFER_BIT);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);


		{ //draw game state:
			std::vector< Vertex > verts;

			//helper: add rectangle to verts:
			auto rect = [&verts](glm::vec2 const &at, glm::vec2 const &rad, glm::u8vec4 const &tint) {
				verts.emplace_back(at + glm::vec2(-rad.x,-rad.y), glm::vec2(0.0f, 0.0f), tint);
				verts.emplace_back(verts.back());
				verts.emplace_back(at + glm::vec2(-rad.x, rad.y), glm::vec2(0.0f, 1.0f), tint);
				verts.emplace_back(at + glm::vec2( rad.x,-rad.y), glm::vec2(1.0f, 0.0f), tint);
				verts.emplace_back(at + glm::vec2( rad.x, rad.y), glm::vec2(1.0f, 1.0f), tint);
				verts.emplace_back(verts.back());
			};

			auto draw_sprite = [&verts](SpriteInfo const &sprite, glm::vec2 const &at) {
				glm::vec2 min_uv = sprite.min_uv;
				glm::vec2 max_uv = sprite.max_uv;
				glm::vec2 rad = sprite.rad;
				glm::u8vec4 tint = glm::u8vec4(0xff, 0xff, 0xff, 0xff);

				verts.emplace_back(at + glm::vec2(-rad.x,-rad.y), glm::vec2(min_uv.x, min_uv.y), tint);
				verts.emplace_back(verts.back());
				verts.emplace_back(at + glm::vec2(-rad.x, rad.y), glm::vec2(min_uv.x, max_uv.y), tint);
				verts.emplace_back(at + glm::vec2( rad.x,-rad.y), glm::vec2(max_uv.x, min_uv.y), tint);
				verts.emplace_back(at + glm::vec2( rad.x, rad.y), glm::vec2(max_uv.x, max_uv.y), tint);
				verts.emplace_back(verts.back());
			};


			//Draw a sprite "player" at position (5.0, 2.0):
			static SpriteInfo base = load_sprite("base");
			static SpriteInfo rock = load_sprite("rock");
			static SpriteInfo charBase = load_sprite("char");
			static SpriteInfo treasure = load_sprite("treasure");
			static SpriteInfo minePrompt = load_sprite("prompt");
			static SpriteInfo endPrompt = load_sprite("end");
			for (int i = 0; i < 11; i++) {
				for (int j = 0; j < 11; j++) {
					if (board[i][j].hasBase &&
						((i >= curx - 1 && i <= curx + 1 && cury == j) ||
						(j >= cury - 1 && j <= cury + 1 && curx == i))) {
						if (board[i][j].hasRock)
								draw_sprite(rock, glm::vec2(-5.0f + i, -5.0f + j));
						else 
							draw_sprite(base, glm::vec2(-5.0f + i, -5.0f + j));
					}
				}
			}
			if(displayMine)
				draw_sprite(minePrompt, glm::vec2(0.0f, -8.0f));
			if (displayEnd) {
				draw_sprite(endPrompt, glm::vec2(0.0f, -8.0f));
				draw_sprite(treasure, glm::vec2(-5.0f + curx, -5.0f + cury));
				should_quit = true;
				won = true;
			}
			draw_sprite(charBase, glm::vec2(-5.0f + curx, -5.0f + cury));


			glBindBuffer(GL_ARRAY_BUFFER, buffer);
			glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * verts.size(), &verts[0], GL_STREAM_DRAW);

			glUseProgram(program);
			glUniform1i(program_tex, 0);
			glm::vec2 scale = 1.0f / camera.radius;
			glm::vec2 offset = scale * -camera.at;
			glm::mat4 mvp = glm::mat4(
				glm::vec4(scale.x, 0.0f, 0.0f, 0.0f),
				glm::vec4(0.0f, scale.y, 0.0f, 0.0f),
				glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
				glm::vec4(offset.x, offset.y, 0.0f, 1.0f)
			);
			glUniformMatrix4fv(program_mvp, 1, GL_FALSE, glm::value_ptr(mvp));

			glBindTexture(GL_TEXTURE_2D, tex);
			glBindVertexArray(vao);

			glDrawArrays(GL_TRIANGLE_STRIP, 0, verts.size());
		}


		SDL_GL_SwapWindow(window);
	}


	//------------  teardown ------------

	SDL_GL_DeleteContext(context);
	context = 0;

	SDL_DestroyWindow(window);
	window = NULL;

	return 0;
}



static GLuint compile_shader(GLenum type, std::string const &source) {
	GLuint shader = glCreateShader(type);
	GLchar const *str = source.c_str();
	GLint length = source.size();
	glShaderSource(shader, 1, &str, &length);
	glCompileShader(shader);
	GLint compile_status = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
	if (compile_status != GL_TRUE) {
		std::cerr << "Failed to compile shader." << std::endl;
		GLint info_log_length = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetShaderInfoLog(shader, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		glDeleteShader(shader);
		throw std::runtime_error("Failed to compile shader.");
	}
	return shader;
}

static GLuint link_program(GLuint fragment_shader, GLuint vertex_shader) {
	GLuint program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);
	GLint link_status = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);
	if (link_status != GL_TRUE) {
		std::cerr << "Failed to link shader program." << std::endl;
		GLint info_log_length = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &info_log_length);
		std::vector< GLchar > info_log(info_log_length, 0);
		GLsizei length = 0;
		glGetProgramInfoLog(program, info_log.size(), &length, &info_log[0]);
		std::cerr << "Info log: " << std::string(info_log.begin(), info_log.begin() + length);
		throw std::runtime_error("Failed to link program");
	}
	return program;
}
