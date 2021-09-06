#include "ColorTextureProgram.hpp"

#include "Mode.hpp"
#include "GL.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

enum PaddleState {Ready, Active, Regen};
enum PowerUps {Projection = 1, Spray, Freeze, Shrink};

/*
 * MultMode is a game mode that implements a single-player game of Mult.
 */

struct MultMode : Mode {
	MultMode();
	virtual ~MultMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	glm::vec2 court_radius = glm::vec2(7.0f, 5.0f);
	glm::vec2 paddle_radius = glm::vec2(0.2f, 1.0f);
	glm::vec2 ball_radius = glm::vec2(0.2f, 0.2f);

    float active_time = 1.0f;
    float regen_time = 2.0f;

    struct Paddle {
        Paddle(glm::vec2 const &position_, glm::vec2 const &radius_, glm::u8vec4 const &color_, const int index_) :
			position(position_), radius(radius_), color(color_), index(index_) {
            state = Ready;
            state_changed = false;
            active_timer = 0.0f;
            regen_timer = 0.0f;
        }
        glm::vec2 position;
        glm::vec2 radius;
        glm::u8vec4 color;
        int index;

        PaddleState state;
        bool state_changed;
        float active_timer;
        float regen_timer;
    };

    Paddle starting_paddle;
    std::vector< Paddle * > paddles{&starting_paddle};

    Paddle *selected_paddle = nullptr;

    Paddle right_paddle;

	glm::vec2 ball = glm::vec2(0.0f, 0.0f);
	glm::vec2 ball_velocity = glm::vec2(-1.0f, 0.0f);

	uint32_t left_score = 0;
	uint32_t right_score = 0;

	float ai_offset = 0.0f;
	float ai_offset_update = 0.0f;

    //----- powerups -----

    float powerup_spawn_timer = 0.0f;
    float powerup_spawn_time = 10.0f;

    glm::vec2 powerup_radius = glm::vec2(0.2f, 0.2f);
    struct PowerUp {
       PowerUp(glm::vec2 const &position_, const PowerUps type_) :
			position(position_), type(type_) {
            radius = glm::vec2(0.2f, 0.2f);
            active_timer = 0.0f;
        } 
        glm::vec2 position;
        glm::vec2 radius;
        PowerUps type;
        float active_timer;
        std::vector< glm::vec4 > spray; // (pos_x, pos_y, vel_x, vel_y)
    };

    float projection_time = 5.0f;
    float freeze_time = 3.0f;
    float shrink_time = 5.0f;
    glm::vec2 spray_radius = glm::vec2(0.1f, 0.1f);

    std::vector< PowerUp * > powerup_on_court;
    PowerUp *inventory = nullptr;
    PowerUp *active_powerup = nullptr;

	//----- pretty gradient trails -----

	float trail_length = 1.3f;
	std::deque< glm::vec3 > ball_trail; //stores (x,y,age), oldest elements first

    float proj_length = 1.0f;
    std::vector< glm::vec2 > proj_trail;
    std::vector< glm::vec2 > collision_trail;
    glm::vec2 proj_radius = glm::vec2(0.05f, 0.05f);

	//----- opengl assets / helpers ------

	//draw functions will work on vectors of vertices, defined as follows:
	struct Vertex {
		Vertex(glm::vec3 const &Position_, glm::u8vec4 const &Color_, glm::vec2 const &TexCoord_) :
			Position(Position_), Color(Color_), TexCoord(TexCoord_) { }
		glm::vec3 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};
	static_assert(sizeof(Vertex) == 4*3 + 1*4 + 4*2, "MultMode::Vertex should be packed");

	//Shader program that draws transformed, vertices tinted with vertex colors:
	ColorTextureProgram color_texture_program;

	//Buffer used to hold vertex data during drawing:
	GLuint vertex_buffer = 0;

	//Vertex Array Object that maps buffer locations to color_texture_program attribute locations:
	GLuint vertex_buffer_for_color_texture_program = 0;

	//Solid white texture:
	GLuint white_tex = 0;

	//matrix that maps from clip coordinates to court-space coordinates:
	glm::mat3x2 clip_to_court = glm::mat3x2(1.0f);
	// computed in draw() as the inverse of OBJECT_TO_CLIP
	// (stored here so that the mouse handling code can use it to position the paddle)

};
