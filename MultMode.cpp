#include "MultMode.hpp"

//for the GL_ERRORS() macro:
#include "gl_errors.hpp"

//for glm::value_ptr() :
#include <glm/gtc/type_ptr.hpp>

#include <random>

#define HEX_TO_U8VEC4( HX ) (glm::u8vec4( (HX >> 24) & 0xff, (HX >> 16) & 0xff, (HX >> 8) & 0xff, (HX) & 0xff ))

MultMode::MultMode() 
    : starting_paddle(glm::vec2(-court_radius.x + 0.5f, 0.0f), glm::vec2(0.2f, 0.5f), HEX_TO_U8VEC4(0xf2d2b6ff), 0),
      right_paddle(glm::vec2( court_radius.x - 0.5f, 0.0f), glm::vec2(0.2f, 1.0f), HEX_TO_U8VEC4(0xf2d2b6ff), 100)
    {

	//set up trail as if ball has been here for 'forever':
	ball_trail.clear();
	ball_trail.emplace_back(ball, trail_length);
	ball_trail.emplace_back(ball, 0.0f);

    proj_trail.clear();
    collision_trail.clear();

	//----- allocate OpenGL resources -----
	{ //vertex buffer:
		glGenBuffers(1, &vertex_buffer);
		//for now, buffer will be un-filled.

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	{ //vertex array mapping buffer for color_texture_program:
		//ask OpenGL to fill vertex_buffer_for_color_texture_program with the name of an unused vertex array object:
		glGenVertexArrays(1, &vertex_buffer_for_color_texture_program);

		//set vertex_buffer_for_color_texture_program as the current vertex array object:
		glBindVertexArray(vertex_buffer_for_color_texture_program);

		//set vertex_buffer as the source of glVertexAttribPointer() commands:
		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

		//set up the vertex array object to describe arrays of MultMode::Vertex:
		glVertexAttribPointer(
			color_texture_program.Position_vec4, //attribute
			3, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 0 //offset
		);
		glEnableVertexAttribArray(color_texture_program.Position_vec4);
		//[Note that it is okay to bind a vec3 input to a vec4 attribute -- the w component will be filled with 1.0 automatically]

		glVertexAttribPointer(
			color_texture_program.Color_vec4, //attribute
			4, //size
			GL_UNSIGNED_BYTE, //type
			GL_TRUE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 //offset
		);
		glEnableVertexAttribArray(color_texture_program.Color_vec4);

		glVertexAttribPointer(
			color_texture_program.TexCoord_vec2, //attribute
			2, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 + 4*1 //offset
		);
		glEnableVertexAttribArray(color_texture_program.TexCoord_vec2);

		//done referring to vertex_buffer, so unbind it:
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		//done setting up vertex array object, so unbind it:
		glBindVertexArray(0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	{ //solid white texture:
		//ask OpenGL to fill white_tex with the name of an unused texture object:
		glGenTextures(1, &white_tex);

		//bind that texture object as a GL_TEXTURE_2D-type texture:
		glBindTexture(GL_TEXTURE_2D, white_tex);

		//upload a 1x1 image of solid white to the texture:
		glm::uvec2 size = glm::uvec2(1,1);
		std::vector< glm::u8vec4 > data(size.x*size.y, glm::u8vec4(0xff, 0xff, 0xff, 0xff));
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());

		//set filtering and wrapping parameters:
		//(it's a bit silly to mipmap a 1x1 texture, but I'm doing it because you may want to use this code to load different sizes of texture)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		//since texture uses a mipmap and we haven't uploaded one, instruct opengl to make one for us:
		glGenerateMipmap(GL_TEXTURE_2D);

		//Okay, texture uploaded, can unbind it:
		glBindTexture(GL_TEXTURE_2D, 0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}
}

MultMode::~MultMode() {

	//----- free OpenGL resources -----
	glDeleteBuffers(1, &vertex_buffer);
	vertex_buffer = 0;

	glDeleteVertexArrays(1, &vertex_buffer_for_color_texture_program);
	vertex_buffer_for_color_texture_program = 0;

	glDeleteTextures(1, &white_tex);
	white_tex = 0;
}

bool MultMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
    switch(evt.type) {
        case SDL_MOUSEMOTION: {
            if (selected_paddle == nullptr) break;
            //convert mouse from window pixels (top-left origin, +y is down) to clip space ([-1,1]x[-1,1], +y is up):
            glm::vec2 clip_mouse = glm::vec2(
                (evt.motion.x + 0.5f) / window_size.x * 2.0f - 1.0f,
                (evt.motion.y + 0.5f) / window_size.y *-2.0f + 1.0f
            );
            
            selected_paddle->position.y = (clip_to_court * glm::vec3(clip_mouse, 1.0f)).y;
            break;
        }
        case SDL_MOUSEBUTTONDOWN: {
            if (selected_paddle != nullptr) break;
            //check which paddle user clicked on
            //convert mouse from window pixel to clip space
            glm::vec2 clip_mouse = glm::vec2(
                (evt.button.x + 0.5f) / window_size.x * 2.0f - 1.0f,
                (evt.button.y + 0.5f) / window_size.y *-2.0f + 1.0f
            );

            glm::vec2 mouse_pos = clip_to_court * glm::vec3(clip_mouse, 1.0f);

            for (Paddle *paddle : paddles) {
                glm::vec2 corner1 = paddle->position - paddle->radius;
                glm::vec2 corner2 = paddle->position + paddle->radius;

                if (mouse_pos.x >= corner1.x && mouse_pos.x <= corner2.x &&
                    mouse_pos.y >= corner1.y && mouse_pos.y <= corner2.y &&
                                                        paddle->state == Ready) {
                    selected_paddle = paddle;
                    
                    //change paddle state to Active
                    selected_paddle->state = Active;
                    selected_paddle->state_changed = true;
                    selected_paddle->active_timer = 0.0f;
                    selected_paddle->regen_timer = 0.0f;
                    break;
                }
            }

            break;
        }
        case SDL_KEYDOWN: {
            switch (evt.key.keysym.sym) {
                case SDLK_f: {
                    if (inventory == nullptr || active_powerup != nullptr) break;
                        active_powerup = inventory;
                        inventory = nullptr;
                        if (active_powerup->type == Spray) {
                            if (active_powerup->spray.size() > 0) {
                                printf("Error spray size should be 0\n");
                            }
                            //add balls to spray vector
                            float angle = 0.349066f; // 20 degrees
                            float new_vel_x1 = ball_velocity.x * cosf(angle) - ball_velocity.y*sinf(angle);
                            float new_vel_y1 = ball_velocity.x * sinf(angle) + ball_velocity.y*cosf(angle);
                            glm::vec4 new_ball1 = glm::vec4(ball.x, ball.y, new_vel_x1, new_vel_y1);
                            active_powerup->spray.push_back(new_ball1);

                            float new_vel_x2 = ball_velocity.x * cosf(-angle) - ball_velocity.y*sinf(-angle);
                            float new_vel_y2 = ball_velocity.x * sinf(-angle) + ball_velocity.y*cosf(-angle);
                            glm::vec4 new_ball2 = glm::vec4(ball.x, ball.y, new_vel_x2, new_vel_y2);
                            active_powerup->spray.push_back(new_ball2);
                        }
                    break;
                }
                case SDLK_SPACE: {
                    if (selected_paddle == nullptr) break;
                    selected_paddle->color = HEX_TO_U8VEC4(0xf2d2b6ff);
                    //change paddle state to Regen
                    selected_paddle->state = Regen;
                    selected_paddle->state_changed = true;
                    selected_paddle->active_timer = 0.0f;
                    selected_paddle->regen_timer = 0.0f;
                    
                    selected_paddle = nullptr;
                    break;
                }
            }
            break;
        }
    }

	return false;
}

void MultMode::update(float elapsed) {

	static std::mt19937 mt; //mersenne twister pseudo-random number generator

	//----- paddle update -----

    if (active_powerup == nullptr || active_powerup->type != Freeze) {
        { //right player ai:
            ai_offset_update -= elapsed;
            if (ai_offset_update < elapsed) {
                //update again in [0.5,1.0) seconds:
                ai_offset_update = (mt() / float(mt.max())) * 0.5f + 0.5f;
                ai_offset = (mt() / float(mt.max())) * 2.5f - 1.25f;
            }
            if (right_paddle.position.y < ball.y + ai_offset) {
                right_paddle.position.y = std::min(ball.y + ai_offset, right_paddle.position.y + 2.0f * elapsed);
            } else {
                right_paddle.position.y = std::max(ball.y + ai_offset, right_paddle.position.y - 2.0f * elapsed);
            }
        }
    }

    //clamp paddles against paddles:
    if (selected_paddle != nullptr) {
        int i = selected_paddle->index;
        if (i != 0) {
            paddles[i]->position.y = std::min(paddles[i]->position.y, paddles[i-1]->position.y - 2*paddles[i]->radius.y);
        }

        if (i != paddles.size()-1) {
            paddles[i]->position.y = std::max(paddles[i]->position.y, paddles[i+1]->position.y + 2*paddles[i]->radius.y);
        }
    }

	//clamp paddles to court:
    auto clamp_paddle = [this](Paddle &paddle) {
        paddle.position.y = std::max(paddle.position.y, -court_radius.y + paddle.radius.y);
	    paddle.position.y = std::min(paddle.position.y,  court_radius.y - paddle.radius.y);
    };

    clamp_paddle(right_paddle);
    if (selected_paddle != nullptr) {
        clamp_paddle(*selected_paddle);
    }

    //update timer state of paddles:
    for (Paddle *paddle : paddles) {
        if (paddle->state_changed) {
            paddle->state_changed = false;
            continue;
        }

        switch (paddle->state) {
            case Active: {
                paddle->active_timer += elapsed;
                if (paddle->active_timer > active_time) {
                    paddle->state = Regen;
                    paddle->active_timer = 0.0f;
                    paddle->regen_timer = 0.0f;
                    selected_paddle = nullptr;
                }
                break;
            }
            case Regen: {
                paddle->regen_timer += elapsed;
                if (paddle->regen_timer > regen_time) {
                    paddle->state = Ready;
                    paddle->active_timer = 0.0f;
                    paddle->regen_timer = 0.0f;
                }
                break;
            }
            case Ready: {
                break;
            }
        }
    }

    //update timer of powerups
    if (powerup_on_court.size() < 3) {
        powerup_spawn_timer += elapsed;
        if (powerup_spawn_timer >= powerup_spawn_time) {
            powerup_spawn_timer = 0.0f;
            //randomly spawn a powerup
            float x = (-court_radius.x + powerup_radius.x) + static_cast <float> (rand()) /( static_cast <float> (RAND_MAX/(2*(court_radius.x - powerup_radius.x))));
            float y = (-court_radius.y + powerup_radius.y) + static_cast <float> (rand()) /( static_cast <float> (RAND_MAX/(2*(court_radius.y - powerup_radius.y))));
            PowerUps rand_powerup = (PowerUps)(rand() % 4 + 1);
            PowerUp *new_powerup = new PowerUp(glm::vec2(x, y), rand_powerup);
            powerup_on_court.push_back(new_powerup);
        }
    }

    if (active_powerup != nullptr) {
        switch (active_powerup->type) {
            case Projection: {
                active_powerup->active_timer += elapsed;
                if (active_powerup->active_timer >= projection_time) {
                    delete active_powerup;
                    active_powerup = nullptr;
                }
                break;
            }
            case Spray: {
                if (active_powerup->spray.size() == 0) {
                    delete active_powerup;
                    active_powerup = nullptr;
                }
                break;
            }
            case Freeze: {
                active_powerup->active_timer += elapsed;
                right_paddle.color = HEX_TO_U8VEC4(0xd6ecefff);
                if (active_powerup->active_timer >= freeze_time) {
                    right_paddle.color = HEX_TO_U8VEC4(0xf2d2b6ff);
                    delete active_powerup;
                    active_powerup = nullptr;
                }
                break;
            }
            case Shrink: {
                active_powerup->active_timer += elapsed;
                right_paddle.radius = glm::vec2(0.2f, 0.5f);
                if (active_powerup->active_timer >= shrink_time) {
                    right_paddle.radius = glm::vec2(0.2f, 1.0f);
                    delete active_powerup;
                    active_powerup = nullptr;
                }
                break;
            }
        }
    }

	//----- ball update -----

	//speed of ball doubles every four points:
	float speed_multiplier = 4.0f * std::pow(2.0f, (left_score + right_score) / 4.0f);

	//velocity cap, though (otherwise ball can pass through paddles):
	speed_multiplier = std::min(speed_multiplier, 10.0f);

	ball += elapsed * speed_multiplier * ball_velocity;

    //---- spray update -----

    if (active_powerup != nullptr && active_powerup->type == Spray) {
        for (glm::vec4 &b : active_powerup->spray) {
            b[0] += elapsed * speed_multiplier * b[2];
            b[1] += elapsed * speed_multiplier * b[3];
        }
    }

	//---- collision handling ----

	//paddles:
	auto paddle_vs_ball = [this](Paddle const &paddle) {
		//compute area of overlap:
		glm::vec2 min = glm::max(paddle.position - paddle.radius, ball - ball_radius);
		glm::vec2 max = glm::min(paddle.position + paddle.radius, ball + ball_radius);

		//if no overlap, no collision:
		if (min.x > max.x || min.y > max.y) return;

		if (max.x - min.x > max.y - min.y) {
			//wider overlap in x => bounce in y direction:
			if (ball.y > paddle.position.y) {
				ball.y = paddle.position.y + paddle.radius.y + ball_radius.y;
				ball_velocity.y = std::abs(ball_velocity.y);
			} else {
				ball.y = paddle.position.y - paddle.radius.y - ball_radius.y;
				ball_velocity.y = -std::abs(ball_velocity.y);
			}
		} else {
			//wider overlap in y => bounce in x direction:
			if (ball.x > paddle.position.x) {
				ball.x = paddle.position.x + paddle.radius.x + ball_radius.x;
				ball_velocity.x = std::abs(ball_velocity.x);
			} else {
				ball.x = paddle.position.x - paddle.radius.x - ball_radius.x;
				ball_velocity.x = -std::abs(ball_velocity.x);
			}
			//warp y velocity based on offset from paddle center:
			float vel = (ball.y - paddle.position.y) / (paddle.radius.y + ball_radius.y);
			ball_velocity.y = glm::mix(ball_velocity.y, vel, 0.75f);
		}

        proj_trail.clear();
        collision_trail.push_back(ball);
	};
	
    for (Paddle *paddle : paddles) {
        paddle_vs_ball(*paddle);
    }
	paddle_vs_ball(right_paddle);

    //powerups:
    for (auto it = powerup_on_court.begin(); it != powerup_on_court.end(); it++) {
        auto powerup = (*it);
        glm::vec2 min = glm::max(powerup->position - powerup->radius, ball - ball_radius);
		glm::vec2 max = glm::min(powerup->position + powerup->radius, ball + ball_radius);
        if (min.x > max.x || min.y > max.y) continue;
        //collided with powerup
        if (inventory != nullptr) {
            delete inventory;
            inventory = nullptr;
        }

        inventory = powerup;
        powerup_on_court.erase(it--);
    }

    { //spray collide with surface => remove it
        if (active_powerup != nullptr && active_powerup->type == Spray) {
            for (auto it = active_powerup->spray.begin(); it != active_powerup->spray.end(); it++) {
                auto b = (*it);
                glm::vec2 b_pos = glm::vec2(b[0], b[1]);
                glm::vec2 b_vel = glm::vec2(b[2], b[3]);

                // collision against wall
                if ((b_pos.y > court_radius.y - spray_radius.y)
                    || (b_pos.y < -court_radius.y + spray_radius.y)) {
                    // no points scored
                    active_powerup->spray.erase(it--);
                    continue;
                } else if (b_pos.x > court_radius.x - spray_radius.x) {
                    // player scored
                    if (b_vel.x > 0.0f) {
                        left_score += 1;
                    }
                    active_powerup->spray.erase(it--);
                    continue;
                } else if (b_pos.x < -court_radius.x + spray_radius.x) {
                    // ai scored
                    if (b_vel.x < 0.0f) {
                        right_score += 1;
                    }
                    active_powerup->spray.erase(it--);
                    continue;
                }

                // collision against player paddles
                for (Paddle *paddle : paddles) {
                    glm::vec2 min = glm::max(paddle->position - paddle->radius, b_pos - spray_radius);
                    glm::vec2 max = glm::min(paddle->position + paddle->radius, b_pos + spray_radius);
                    if (!(min.x > max.x || min.y > max.y)) {
                        active_powerup->spray.erase(it--);
                        continue;
                    }
                }

                // collision against ai paddle
                glm::vec2 min = glm::max(right_paddle.position - right_paddle.radius, b_pos - spray_radius);
                glm::vec2 max = glm::min(right_paddle.position + right_paddle.radius, b_pos + spray_radius);
                if (!(min.x > max.x || min.y > max.y)) {
                    active_powerup->spray.erase(it--);
                }
            }
        }
    }

    bool wall_collision = false;

    //collide with any of the walls
    if ((ball.y > court_radius.y - ball_radius.y)
        || (ball.y < -court_radius.y + ball_radius.y)
        || (ball.x > court_radius.x - ball_radius.x)
        || (ball.x < -court_radius.x + ball_radius.x)) {
        wall_collision = true;
    }

	//court walls:
	if (ball.y > court_radius.y - ball_radius.y) {
		ball.y = court_radius.y - ball_radius.y;
		if (ball_velocity.y > 0.0f) {
			ball_velocity.y = -ball_velocity.y;
		}
	}
	if (ball.y < -court_radius.y + ball_radius.y) {
		ball.y = -court_radius.y + ball_radius.y;
		if (ball_velocity.y < 0.0f) {
			ball_velocity.y = -ball_velocity.y;
		}
	}

    //player scored
	if (ball.x > court_radius.x - ball_radius.x) {
		ball.x = court_radius.x - ball_radius.x;
		if (ball_velocity.x > 0.0f) {
			ball_velocity.x = -ball_velocity.x;
			left_score += 1;

            //give player another paddle (max 3) 
            if (paddles.size() < 3) {
                Paddle *new_paddle = new Paddle(glm::vec2(-court_radius.x + 0.5f, 0.0f), glm::vec2(0.2f, 0.5f), HEX_TO_U8VEC4(0xf2d2b6ff), paddles.size());
                paddles.push_back(new_paddle);
                
                // reset and space out the paddle positions
                double full_length = court_radius.y * 2;
                double chunk = full_length / (paddles.size() + 1);
                double position = court_radius.y;
                position -= chunk;
                for (Paddle *paddle : paddles) {
                    paddle->position.y = position;
                    position -= chunk;
                }
            }
		}
	}
    // AI scored
	if (ball.x < -court_radius.x + ball_radius.x) {
		ball.x = -court_radius.x + ball_radius.x;
		if (ball_velocity.x < 0.0f) {
			ball_velocity.x = -ball_velocity.x;
			right_score += 1;
		}
	}

    if (wall_collision) {
        proj_trail.clear();
        collision_trail.push_back(ball);
    }

	//----- gradient trails -----

	//age up all locations in ball trail:
	for (auto &t : ball_trail) {
		t.z += elapsed;
	}
	//store fresh location at back of ball trail:
	ball_trail.emplace_back(ball, 0.0f);

	//trim any too-old locations from back of trail:
	//NOTE: since trail drawing interpolates between points, only removes back element if second-to-back element is too old:
	while (ball_trail.size() >= 2 && ball_trail[1].z > trail_length) {
		ball_trail.pop_front();
	}

    // check if any of the collision trail needs to be removed
    {
        while (proj_trail.size() > 0) {
            //compute area of overlap:
            glm::vec2 min = glm::max(proj_trail[0] - proj_radius, ball - ball_radius);
            glm::vec2 max = glm::min(proj_trail[0] + proj_radius, ball + ball_radius);

            //if no overlap, no collision:
            if (min.x > max.x || min.y > max.y) break;
            proj_trail.erase(proj_trail.begin());
        }
    }

}

void MultMode::draw(glm::uvec2 const &drawable_size) {
	//some nice colors from the course web page:
	const glm::u8vec4 bg_color = HEX_TO_U8VEC4(0x193b59ff);
	const glm::u8vec4 fg_color = HEX_TO_U8VEC4(0xf2d2b6ff);
	const glm::u8vec4 shadow_color = HEX_TO_U8VEC4(0xf2ad94ff);
	const std::vector< glm::u8vec4 > trail_colors = {
		HEX_TO_U8VEC4(0xf2ad9488),
		HEX_TO_U8VEC4(0xf2897288),
		HEX_TO_U8VEC4(0xbacac088),
	};

	//other useful drawing constants:
	const float wall_radius = 0.05f;
	const float shadow_offset = 0.07f;
	const float padding = 0.14f; //padding between outside of walls and edge of window

	//---- compute vertices to draw ----

	//vertices will be accumulated into this list and then uploaded+drawn at the end of this function:
	std::vector< Vertex > vertices;

	//inline helper function for rectangle drawing:
	auto draw_rectangle = [&vertices](glm::vec2 const &center, glm::vec2 const &radius, glm::u8vec4 const &color) {
		//draw rectangle as two CCW-oriented triangles:
		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));

		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
	};

    //inline helper function for powerup drawing:
    auto draw_powerup = [&draw_rectangle](const PowerUps type, glm::vec2 const &position, glm::vec2 const &radius) {
		switch (type) {
            case Projection: {
                draw_rectangle(position, radius, HEX_TO_U8VEC4(0xadd8e6f0)); // base
                // draw glasses
                draw_rectangle(glm::vec2(position.x, position.y + 0.025f), glm::vec2(0.05f, 0.025f), HEX_TO_U8VEC4(0x000000ff)); // bridge
                draw_rectangle(glm::vec2(position.x - 0.1f, position.y + 0.025f), glm::vec2(0.05f, 0.075f), HEX_TO_U8VEC4(0x000000ff)); // left
                draw_rectangle(glm::vec2(position.x + 0.1f, position.y + 0.025f), glm::vec2(0.05f, 0.075f), HEX_TO_U8VEC4(0x000000ff)); // right
                break;
            }
            case Spray: {
                draw_rectangle(position, radius, HEX_TO_U8VEC4(0xffc0cbff)); // base
                glm::vec2 r = glm::vec2(0.025f, 0.025f);
                // draw spray
                draw_rectangle(glm::vec2(position.x + 0.125f, position.y + 0.075f), r, HEX_TO_U8VEC4(0x000000ff)); // top
                draw_rectangle(glm::vec2(position.x + 0.075f, position.y - 0.075f), r, HEX_TO_U8VEC4(0x000000ff)); // center
                draw_rectangle(glm::vec2(position.x - 0.075f, position.y - 0.125f), r, HEX_TO_U8VEC4(0x000000ff)); // bottom
                break;
            }
            case Freeze: {
                draw_rectangle(position, radius, HEX_TO_U8VEC4(0x060d33ff)); // base
                // draw ice
                draw_rectangle(glm::vec2(position.x, position.y + 0.15f), glm::vec2(0.2f, 0.05f), HEX_TO_U8VEC4(0xb0f5ecff)); // 1
                draw_rectangle(glm::vec2(position.x - 0.125f, position.y + 0.025f), glm::vec2(0.025f, 0.075f), HEX_TO_U8VEC4(0xb0f5ecff)); // 2
                draw_rectangle(glm::vec2(position.x - 0.025f, position.y + 0.075f), glm::vec2(0.025f, 0.025f), HEX_TO_U8VEC4(0xb0f5ecff)); // 3
                draw_rectangle(glm::vec2(position.x + 0.125f, position.y + 0.05f), glm::vec2(0.025f, 0.05f), HEX_TO_U8VEC4(0xb0f5ecff)); // 4
                draw_rectangle(glm::vec2(position.x + 0.175f, position.y), glm::vec2(0.025f, 0.1f), HEX_TO_U8VEC4(0xb0f5ecff)); // 5
                break;
            }
            case Shrink: {
                draw_rectangle(position, radius, HEX_TO_U8VEC4(0x1d0429ff)); // base
                // draw big and small paddle
                draw_rectangle(glm::vec2(position.x - 0.075f, position.y), glm::vec2(0.025f, 0.15f), HEX_TO_U8VEC4(0xf2d2b6ff)); // big
                draw_rectangle(glm::vec2(position.x + 0.075f, position.y), glm::vec2(0.025f, 0.05f), HEX_TO_U8VEC4(0xf2d2b6ff)); // small
                break;
            }
        }
	};

	//shadows for everything (except the trail):

	glm::vec2 s = glm::vec2(0.0f,-shadow_offset);

	draw_rectangle(glm::vec2(-court_radius.x-wall_radius, 0.0f)+s, glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), shadow_color);
	draw_rectangle(glm::vec2( court_radius.x+wall_radius, 0.0f)+s, glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), shadow_color);
	draw_rectangle(glm::vec2( 0.0f,-court_radius.y-wall_radius)+s, glm::vec2(court_radius.x, wall_radius), shadow_color);
	draw_rectangle(glm::vec2( 0.0f, court_radius.y+wall_radius)+s, glm::vec2(court_radius.x, wall_radius), shadow_color);
	
	draw_rectangle(ball+s, ball_radius, shadow_color);

	//ball's trail:
	if (ball_trail.size() >= 2) {
		//start ti at second element so there is always something before it to interpolate from:
		std::deque< glm::vec3 >::iterator ti = ball_trail.begin() + 1;
		//draw trail from oldest-to-newest:
		constexpr uint32_t STEPS = 20;
		//draw from [STEPS, ..., 1]:
		for (uint32_t step = STEPS; step > 0; --step) {
			//time at which to draw the trail element:
			float t = step / float(STEPS) * trail_length;
			//advance ti until 'just before' t:
			while (ti != ball_trail.end() && ti->z > t) ++ti;
			//if we ran out of recorded tail, stop drawing:
			if (ti == ball_trail.end()) break;
			//interpolate between previous and current trail point to the correct time:
			glm::vec3 a = *(ti-1);
			glm::vec3 b = *(ti);
			glm::vec2 at = (t - a.z) / (b.z - a.z) * (glm::vec2(b) - glm::vec2(a)) + glm::vec2(a);

			//look up color using linear interpolation:
			//compute (continuous) index:
			float c = (step-1) / float(STEPS-1) * trail_colors.size();
			//split into an integer and fractional portion:
			int32_t ci = int32_t(std::floor(c));
			float cf = c - ci;
			//clamp to allowable range (shouldn't ever be needed but good to think about for general interpolation):
			if (ci < 0) {
				ci = 0;
				cf = 0.0f;
			}
			if (ci > int32_t(trail_colors.size())-2) {
				ci = int32_t(trail_colors.size())-2;
				cf = 1.0f;
			}
			//do the interpolation (casting to floating point vectors because glm::mix doesn't have an overload for u8 vectors):
			glm::u8vec4 color = glm::u8vec4(
				glm::mix(glm::vec4(trail_colors[ci]), glm::vec4(trail_colors[ci+1]), cf)
			);

			//draw:
			draw_rectangle(at, ball_radius, color);
		}

        if (collision_trail.size() > 0 && collision_trail[0] != ball) {
            //ball's projection 
            constexpr uint32_t PROJ_STEPS = 25;
            auto current_ball = ball_trail.end() - 1;
            auto prev_ball = ball_trail.end() - 2;
            float rise = (current_ball->y - prev_ball->y) * 10;
            float run = (current_ball->x - prev_ball->x) * 10;
            for (uint32_t step = 1; step <= PROJ_STEPS; ++step) {
                glm::vec2 proj_pos = glm::vec2((*current_ball).x + run*step, (*current_ball).y + rise*step);
                proj_trail.push_back(proj_pos);
            }
            collision_trail.clear();
        }
	}

    // draw active powerup animations
    if (active_powerup != nullptr) {
        switch (active_powerup->type) {
            case Projection: {
                for (auto &proj_pos : proj_trail) {
                    draw_rectangle(proj_pos, proj_radius, HEX_TO_U8VEC4(0xffffffff));
                }
                break;
            }
            case Spray: {
                for (auto &b: active_powerup->spray) {
                    draw_rectangle(glm::vec2(b[0], b[1]), spray_radius, HEX_TO_U8VEC4(0xffc0cbff));
                }
                break;
            }
            case Freeze: {
                break;
            }
            case Shrink: {
                break;
            }
        }
    }

    //powerups:
    for (PowerUp *powerup : powerup_on_court) {
        draw_powerup(powerup->type, powerup->position, powerup->radius);
    }

	//solid objects:

	//walls:
	draw_rectangle(glm::vec2(-court_radius.x-wall_radius, 0.0f), glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), fg_color);
	draw_rectangle(glm::vec2( court_radius.x+wall_radius, 0.0f), glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), fg_color);
	draw_rectangle(glm::vec2( 0.0f,-court_radius.y-wall_radius), glm::vec2(court_radius.x, wall_radius), fg_color);
	draw_rectangle(glm::vec2( 0.0f, court_radius.y+wall_radius), glm::vec2(court_radius.x, wall_radius), fg_color);

	//paddles:
    for (Paddle *paddle : paddles) {
        switch (paddle->state) {
            case Ready: {
                draw_rectangle(paddle->position, paddle->radius, HEX_TO_U8VEC4(0xf2d2b6ff));
                break;
            }
            case Active: {
                //red - time spent active
                glm::vec2 time_spent_rad = glm::vec2(paddle->radius.x/2, paddle->radius.y * (paddle->active_timer/active_time));
                glm::vec2 time_spent_pos = glm::vec2(paddle->position.x - 0.35f, paddle->position.y + paddle->radius.y - time_spent_rad.y);
                draw_rectangle(time_spent_pos, time_spent_rad, HEX_TO_U8VEC4(0xff0000ff));
                //green - time left active
                glm::vec2 time_left_rad = glm::vec2(paddle->radius.x/2, paddle->radius.y * (1.0f - paddle->active_timer/active_time));
                glm::vec2 time_left_pos = glm::vec2(paddle->position.x - 0.35f, paddle->position.y - paddle->radius.y + time_left_rad.y);
                draw_rectangle(time_left_pos, time_left_rad, HEX_TO_U8VEC4(0x00ff00ff));

                //draw paddle
                draw_rectangle(paddle->position, paddle->radius, HEX_TO_U8VEC4(0x90ee90ff));
                break;
            }
            case Regen: {
                //green - time spent in cool down
                glm::vec2 time_spent_rad = glm::vec2(paddle->radius.x, paddle->radius.y * (paddle->regen_timer/regen_time));
                glm::vec2 time_spent_pos = glm::vec2(paddle->position.x, paddle->position.y + paddle->radius.y - time_spent_rad.y);
                draw_rectangle(time_spent_pos, time_spent_rad, HEX_TO_U8VEC4(0xf2d2b6ff));
                //red - time left in cool down
                glm::vec2 time_left_rad = glm::vec2(paddle->radius.x, paddle->radius.y * (1.0f - paddle->regen_timer/regen_time));
                glm::vec2 time_left_pos = glm::vec2(paddle->position.x, paddle->position.y - paddle->radius.y + time_left_rad.y);
                draw_rectangle(time_left_pos, time_left_rad, HEX_TO_U8VEC4(0xff0000ff));
                break;
            }
        }
    }
	draw_rectangle(right_paddle.position, right_paddle.radius, right_paddle.color);
	

	//ball:
	draw_rectangle(ball, ball_radius, fg_color);

	//scores:
	glm::vec2 score_radius = glm::vec2(0.1f, 0.1f);
	for (uint32_t i = 0; i < right_score; ++i) { //ai score
		draw_rectangle(glm::vec2( court_radius.x - (2.0f + 3.0f * i) * score_radius.x, court_radius.y + 2.0f * wall_radius + 2.0f * score_radius.y), score_radius, HEX_TO_U8VEC4(0xb53737ff));
	}
    for (uint32_t i = 0; i < left_score; ++i) { //player score
		draw_rectangle(glm::vec2( -court_radius.x + (2.0f + 3.0f * i) * score_radius.x, court_radius.y + 2.0f * wall_radius + 2.0f * score_radius.y), score_radius, HEX_TO_U8VEC4(0x3895d3ff));
	}

    //inventory:
    glm::vec2 top_left_corner = glm::vec2(-court_radius.x + (powerup_radius.x - 0.1f), court_radius.y - (powerup_radius.y - 0.6f));
    draw_rectangle( top_left_corner, glm::vec2(powerup_radius.y + 0.05f, powerup_radius.y + 0.05f), HEX_TO_U8VEC4(0x000000ff));
    if (inventory != nullptr) {
        draw_powerup(inventory->type, top_left_corner, inventory->radius);
    }

	//------ compute court-to-window transform ------

	//compute area that should be visible:
	glm::vec2 scene_min = glm::vec2(
		-court_radius.x - 2.0f * wall_radius - padding,
		-court_radius.y - 2.0f * wall_radius - padding
	);
	glm::vec2 scene_max = glm::vec2(
		court_radius.x + 2.0f * wall_radius + padding,
		court_radius.y + 2.0f * wall_radius + 3.0f * score_radius.y + padding
	);

	//compute window aspect ratio:
	float aspect = drawable_size.x / float(drawable_size.y);
	//we'll scale the x coordinate by 1.0 / aspect to make sure things stay square.

	//compute scale factor for court given that...
	float scale = std::min(
		(2.0f * aspect) / (scene_max.x - scene_min.x), //... x must fit in [-aspect,aspect] ...
		(2.0f) / (scene_max.y - scene_min.y) //... y must fit in [-1,1].
	);

	glm::vec2 center = 0.5f * (scene_max + scene_min);

	//build matrix that scales and translates appropriately:
	glm::mat4 court_to_clip = glm::mat4(
		glm::vec4(scale / aspect, 0.0f, 0.0f, 0.0f),
		glm::vec4(0.0f, scale, 0.0f, 0.0f),
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
		glm::vec4(-center.x * (scale / aspect), -center.y * scale, 0.0f, 1.0f)
	);
	//NOTE: glm matrices are specified in *Column-Major* order,
	// so each line above is specifying a *column* of the matrix(!)

	//also build the matrix that takes clip coordinates to court coordinates (used for mouse handling):
	clip_to_court = glm::mat3x2(
		glm::vec2(aspect / scale, 0.0f),
		glm::vec2(0.0f, 1.0f / scale),
		glm::vec2(center.x, center.y)
	);

	//---- actual drawing ----

	//clear the color buffer:
	glClearColor(bg_color.r / 255.0f, bg_color.g / 255.0f, bg_color.b / 255.0f, bg_color.a / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	//use alpha blending:
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//don't use the depth test:
	glDisable(GL_DEPTH_TEST);

	//upload vertices to vertex_buffer:
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); //set vertex_buffer as current
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertices[0]), vertices.data(), GL_STREAM_DRAW); //upload vertices array
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//set color_texture_program as current program:
	glUseProgram(color_texture_program.program);

	//upload OBJECT_TO_CLIP to the proper uniform location:
	glUniformMatrix4fv(color_texture_program.OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(court_to_clip));

	//use the mapping vertex_buffer_for_color_texture_program to fetch vertex data:
	glBindVertexArray(vertex_buffer_for_color_texture_program);

	//bind the solid white texture to location zero so things will be drawn just with their colors:
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, white_tex);

	//run the OpenGL pipeline:
	glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size()));

	//unbind the solid white texture:
	glBindTexture(GL_TEXTURE_2D, 0);

	//reset vertex array to none:
	glBindVertexArray(0);

	//reset current program to none:
	glUseProgram(0);
	

	GL_ERRORS(); //PARANOIA: print errors just in case we did something wrong.

}
