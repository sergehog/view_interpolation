/*! view-interpolation OpenGL demo
* \author: Sergey Smirnov <sergei.smirnov@gmail.com>
* \date 24.02.2014
**/

#define GLM_FORCE_SSE2

#include <GL/glew.h>
#include "vi_config.h"
#include "common/camera_loader.h"
#include "common/yuv_reader.h"
#include "common/controls.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <memory>


using namespace glm;


unsigned prepare_buffers(config::vi_config config, GLuint &uvbuffer, GLuint &elementbuffer);
void setup_program(GLuint programID, config::vi_config config, int screen_width, int screen_height);
void read_rgb_frame(t3dtv::yuv_reader &color_reader, config::vi_config config, GLubyte *rgb_buffer);

// void checkGLError(char* name)
// {
// 	GLenum err = glGetError();
// 	if (err != GL_NO_ERROR)
// 	{
// 		char errTxt[400];
// 		sprintf(errTxt, "%s. OpenGL Error 0x%.4x!", name, err);
// 		throw std::runtime_error(errTxt);
// 	}
// }

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::cout << "CORRECT USAGE: " << argv[0] << " config-file.xml" << std::endl;
		return 1;
	}

	try
	{

		const static config::vi_config config = config::vi_config::load_config(argv[1]);
		const std::pair<const mat3, const mat4x3> input_camera1 = config::camera_loader::read_settings(config.camera_file, config.camera1_name);
		const std::pair<const mat3, const mat4x3> input_camera2 = config::camera_loader::read_settings(config.camera_file, config.camera2_name);
		const std::pair<const mat3, const mat4x3> output_camera = config::camera_loader::read_settings(config.desired_camera_file, config.desired_camera_name);

		const mat4 C1 = mat4(input_camera1.first * input_camera1.second);
		const mat4 C2 = mat4(input_camera2.first * input_camera2.second);
		const mat4 CV = mat4(output_camera.first * output_camera.second);

		// Initialise GLFW	
		if (!glfwInit())
		{
			throw std::runtime_error("Failed to initialize GLFW");
		}

		glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_SAMPLES, 0);
		
		t3dtv::yuv_reader color1_reader(config.color1_file.c_str(), int(config.width), int(config.height), true, true, 0);
		t3dtv::yuv_reader color2_reader(config.color2_file.c_str(), int(config.width), int(config.height), true, true, 0);

		if (color1_reader.is_bad() || color2_reader.is_bad())
		{
			throw std::runtime_error("Cannot read YUV file(s).");
		}

		std::unique_ptr<GLubyte> rgb1_buffer = std::unique_ptr<GLubyte>(new GLubyte[config.width * config.height * 3]);
		std::unique_ptr<GLubyte> rgb2_buffer = std::unique_ptr<GLubyte>(new GLubyte[config.width * config.height * 3]);

		GLFWwindow* window = glfwCreateWindow(config.output_width, config.output_height, "View-plus-depth OpenGL rendering demo", glfwGetPrimaryMonitor(), NULL);

		// Open a window and create its OpenGL context		
		if (!window)
		{
			glfwTerminate();
			throw std::runtime_error("Failed to open GLFW window.");
		}

		glfwMakeContextCurrent(window);

		int screen_width, screen_height;
		glfwGetWindowSize(window, &screen_width, &screen_height);
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
		glfwSwapInterval(-1);

		//const float range_multiplier = 1.3f;
		//const mat4 ProjectionMatrix = rgbd::image_tools::prepareProjectionMatrix(output_camera.first, screen_width, screen_height, config.minZ / range_multiplier, config.maxZ*range_multiplier);
		//const mat4 ProjectionMatrix = rgbd::image_tools::prepareProjectionMatrix2(output_camera.first, screen_width, screen_height, config.minZ, config.maxZ);

		// Initialize GLEW
		glewExperimental = true; // Needed for core profile
		if (glewInit() != GLEW_OK)
		{
			throw std::runtime_error("Failed to initialize GLEW.");
		}

		// Ensure we can capture the escape key being pressed below
		glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);

		// Dark blue background
		glClearColor(0.0f, 0.0f, 0.2f, 0.0f);

		// Enable depth test
		glEnable(GL_DEPTH);
		glEnable(GL_DEPTH_TEST);

		// Accept fragment if it closer to the camera than the former one
		glDepthFunc(GL_LESS);
		//glEnable(GL_BLEND);
		//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Cull triangles which normal is not towards the camera
		//glEnable(GL_CULL_FACE);

		glfwPollEvents();

		GLuint vao;
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);

		// Main program (renders to display)
		const GLuint main_programID = (config.geometry_shader.empty()) ? LoadShaders(config.vertex_shader.c_str(), config.fragment_shader.c_str()) : Load3Shaders(config.vertex_shader.c_str(), config.geometry_shader.c_str(), config.fragment_shader.c_str());
		const GLuint TextureID = glGetUniformLocation(main_programID, "Texture");
		setup_program(main_programID, config, screen_width, screen_height);

		// Secondary program (renders to texture)
		const GLuint internal_programID = (config.geometry_internal.empty()) ? LoadShaders(config.vertex_internal.c_str(), config.fragment_internal.c_str()) : Load3Shaders(config.vertex_internal.c_str(), config.geometry_internal.c_str(), config.fragment_internal.c_str());
		const GLuint tex1ID = glGetUniformLocation(internal_programID, "Texture1");
		const GLuint tex2ID = glGetUniformLocation(internal_programID, "Texture2");
		const GLuint Matrix1ID = glGetUniformLocation(internal_programID, "MVP1");
		const GLuint Matrix2ID = glGetUniformLocation(internal_programID, "MVP2");
		const GLuint LayerID = glGetUniformLocation(internal_programID, "layer");
		setup_program(internal_programID, config, screen_width, screen_height);

		glGetError();


		GLuint uvbuffer_internal, uvbuffer_main;
		const GLint uv_buffer_int[] = { 0, 0, config.width, 0, 0, config.height,
			config.width, 0, config.width, config.height, 0, config.height };
		const GLint uv_buffer_main[] = { 0, 0, screen_width, 0, 0, screen_height,
			screen_width, 0, screen_width, screen_height, 0, screen_height };
		glGenBuffers(1, &uvbuffer_internal);
		glBindBuffer(GL_ARRAY_BUFFER, uvbuffer_internal);
		glBufferData(GL_ARRAY_BUFFER, sizeof(uv_buffer_int), uv_buffer_int, GL_STATIC_DRAW);

		glGenBuffers(1, &uvbuffer_main);
		glBindBuffer(GL_ARRAY_BUFFER, uvbuffer_main);
		glBufferData(GL_ARRAY_BUFFER, sizeof(uv_buffer_main), uv_buffer_main, GL_STATIC_DRAW);

		///////////////////////////////////////////////////////////////////////////

		// The framebuffer, which regroups 0, 1, or more textures, and 0 or 1 depth buffer.
		GLuint FramebufferName = 0;
		glGenFramebuffers(1, &FramebufferName);
		glBindFramebuffer(GL_FRAMEBUFFER, FramebufferName);
		
		// The texture we're going to render to
		GLuint renderedTexture;
		glGenTextures(1, &renderedTexture);

		// "Bind" the newly created texture : all future texture functions will modify this texture
		glBindTexture(GL_TEXTURE_2D_ARRAY, renderedTexture);
		
		glTexStorage3D(GL_TEXTURE_2D_ARRAY, 10, GL_RGBA8, screen_width, screen_height, config.layers);

		// Give an empty image to OpenGL ( the last "0" means "empty" )
		//glTexImage2D(GL_TEXTURE_2D_ARRAY, 0, GL_RGB, config.width, config.height, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);

		// Poor filtering
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		
		// Set "renderedTexture" as our colour attachement #0
		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, renderedTexture, 0);

		GLenum DrawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
		glDrawBuffers(1, DrawBuffers); // "1" is the size of DrawBuffers

		
		// Always check that our framebuffer is ok
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			throw std::runtime_error("Framebuffer is not OK!");
		}
		

		GLuint texture1ID;
		glGenTextures(1, &texture1ID);
		glBindTexture(GL_TEXTURE_2D, texture1ID);
		// Give the image to OpenGL
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, config.width, config.height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		//glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // GL_MIRRORED_REPEAT
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // GL_CLAMP_TO_EDGE
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);


		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture1ID);
		glUniform1i(tex1ID, 0);

		GLuint texture2ID;
		glGenTextures(1, &texture2ID);
		glBindTexture(GL_TEXTURE_2D, texture2ID);
		// Give the image to OpenGL
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, config.width, config.height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		//glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // GL_MIRRORED_REPEAT
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // GL_CLAMP_TO_EDGE
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // GL_LINEAR
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, texture2ID);
		glUniform1i(tex2ID, 1);


				
		/////////////////////////////////////////////////////////////////////////

		bool first_loop = true;
		glfwSetCursorPos(window, screen_width / 2.0, screen_height / 2.0);
		//unsigned ft_triangles = 0;
		unsigned frame = 0;
		float curr_fps = 0;
		double lastTime = glfwGetTime();
		unsigned nbFrames = 0;

		
		//glBindFramebuffer(GL_FRAMEBUFFER, 0);
		//glViewport(0, 0, screen_width, screen_height);
		//const GLuint layerID = glGetUniformLocation(programID, "layer");
		//glUniform1ui(layerID, config.layer);

		while (!glfwWindowShouldClose(window) && glfwGetKey(window, GLFW_KEY_ESCAPE) != GLFW_PRESS)
		{
			// Measure speed
			double currentTime = glfwGetTime();
			nbFrames++;
			if (currentTime - lastTime >= 1.0)// If last prinf() was more than 1sec ago
			{
				curr_fps = nbFrames / (currentTime - lastTime);
				printf("%d: %3.1f FPS \t", frame, curr_fps);
				nbFrames = 0;
				lastTime = currentTime;
			}

			if (frame >= config.frames)
			{
				color1_reader.set_frame(0);
				color2_reader.set_frame(0);
				frame = 0;
				first_loop = false;
			}

			frame++;
			//printf("frame: %d\n", frame);
			glBindFramebuffer(GL_FRAMEBUFFER, FramebufferName);
			glViewport(0, 0, screen_width, screen_height); // Render on the whole framebuffer, complete from the lower left corner to the upper right
			//glViewport(0, 0, config.width, config.height);
			glClearColor(0.0f, 2.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			// Use our shader
			glUseProgram(internal_programID);

			read_rgb_frame(color1_reader, config, rgb1_buffer.get());
			read_rgb_frame(color2_reader, config, rgb2_buffer.get());

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, texture1ID);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, config.width, config.height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb1_buffer.get());
			glGenerateMipmap(GL_TEXTURE_2D);  //Generate mipmaps here.

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, texture2ID);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, config.width, config.height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb2_buffer.get());
			glGenerateMipmap(GL_TEXTURE_2D);  //Generate mipmaps here.


			const mat4 ViewMatrix = computeViewMatrix(config.speed, config.mouseSpeed, window);
			//const mat4 Cinv = glm::inverse(ViewMatrix*CV);
			const mat4 Cinv = ViewMatrix*glm::inverse(CV);
			const mat4 MVP1 = C1 * Cinv;
			const mat4 MVP2 = C2 * Cinv;

			// Send our transformation to the currently bound shader, in the "MVP" uniform
			glUniformMatrix4fv(Matrix1ID, 1, GL_FALSE, &MVP1[0][0]);
			glUniformMatrix4fv(Matrix2ID, 1, GL_FALSE, &MVP2[0][0]);

			glEnableVertexAttribArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, uvbuffer_internal);
			glVertexAttribIPointer(0, 2, GL_INT, 0, (void*)0);

			for (GLuint layer = 0; layer < config.layers; layer++)
			{
				glUniform1ui(LayerID, layer);

				glDrawArrays(GL_TRIANGLES, 0, 6);
			}

			glDisableVertexAttribArray(0);

			// Draw the scene
			//glDrawElements(GL_TRIANGLES, tr_indexes, GL_UNSIGNED_INT, (void*)0);
			//glDrawElementsInstanced(GL_TRIANGLES, tr_indexes, GL_UNSIGNED_INT, (void*)0, config.layers);

			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			glClearColor(0.0f, 0.0f, 0.2f, 0.0f);
			//glViewport(0, 0, screen_width, screen_height);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			// Use our shader
			glUseProgram(main_programID);

			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D_ARRAY, renderedTexture);
			glUniform1i(TextureID, 0);
			glGenerateMipmap(GL_TEXTURE_2D_ARRAY); 

			glEnableVertexAttribArray(0);
			glBindBuffer(GL_ARRAY_BUFFER, uvbuffer_main);
			glVertexAttribIPointer(0, 2, GL_INT, 0, (void*)0);			

			glDrawArrays(GL_TRIANGLES, 0, 6);

			glDisableVertexAttribArray(0);

			// Swap buffers
			glfwSwapBuffers(window);
			glfwPollEvents();
		}

		
		glDeleteBuffers(1, &uvbuffer_internal);
		glDeleteBuffers(1, &uvbuffer_main);
		glDeleteVertexArrays(1, &vao);
		glDeleteTextures(1, &texture1ID);
		glDeleteTextures(1, &texture2ID);
		glDeleteTextures(1, &renderedTexture);
		glDeleteProgram(internal_programID);
		glDeleteProgram(main_programID);
		glDeleteFramebuffers(1, &FramebufferName);
		
		// Close OpenGL window and terminate GLFW
		glfwTerminate();

	}
	catch (std::runtime_error& e)
	{
		std::cout << "Error occurred: " << e.what() << std::endl;
		glfwTerminate();
		return 2;
	}
	catch (...)
	{
		std::cout << "Unknown error occurred!" << std::endl;
		glfwTerminate();
		return 3;
	}

	return 0;
}


unsigned prepare_buffers(config::vi_config config, GLuint &uvbuffer, GLuint &elementbuffer)
{
	const unsigned tr_indexes = (config.width - 1) * (config.height - 1) * 2 * 3;
	const long HW = long(config.width)*config.height;

	std::unique_ptr<GLuint> indexes_unique = std::unique_ptr<GLuint>(new GLuint[tr_indexes]);
	std::unique_ptr<GLint> uv_unique = std::unique_ptr<GLint>(new GLint[HW * 2]);
	GLuint *indexes_buffer = indexes_unique.get();
	GLint *uv_buffer = uv_unique.get();

#pragma omp parallel sections
	{
#pragma omp section
		{
			// creates an index array
			long index = 0, trindex = 0;
			for (int y = 0; y < int(config.height); y++)
			{
				for (int x = 0; x<int(config.width); x++, index++)
				{
					if (x > 0 && y < int(config.height) - 1)
					{
						indexes_buffer[trindex * 3] = unsigned(index);
						indexes_buffer[trindex * 3 + 1] = unsigned(index - 1);
						indexes_buffer[trindex * 3 + 2] = unsigned(index + config.width - 1);
						trindex++;

						indexes_buffer[trindex * 3] = unsigned(index);
						indexes_buffer[trindex * 3 + 1] = unsigned(index + config.width - 1);
						indexes_buffer[trindex * 3 + 2] = unsigned(index + config.width);
						trindex++;
					}
				}
			}
		}
#pragma omp section
		{
		//creates vertex array
#pragma omp parallel for
		for (int y = 0; y < int(config.height); y++)
		{
			long index = y * long(config.width);
			int v = (config.y_axis_up) ? (config.height - y - 1) : y;

			for (int x = 0; x < int(config.width); x++, index++)
			{
				int u = (config.x_axis_right) ? x : (config.width - x - 1);
				uv_buffer[index * 2] = u;
				uv_buffer[index * 2 + 1] = v;
			}
		}
	}
	}

	// triangles are always the same, so can be initialized just one in advance				
	glGenBuffers(1, &elementbuffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementbuffer);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint)*tr_indexes, indexes_buffer, GL_STATIC_DRAW);

	glGenBuffers(1, &uvbuffer);
	glBindBuffer(GL_ARRAY_BUFFER, uvbuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(GLint)*HW * 2, uv_buffer, GL_STATIC_DRAW);

	return tr_indexes;
}

void setup_program(const GLuint programID, config::vi_config config, const int screen_width, const int screen_height)
{
	const GLuint param1ID = glGetUniformLocation(programID, "param1");
	const GLuint param2ID = glGetUniformLocation(programID, "param2");
	const GLuint minZID = glGetUniformLocation(programID, "minZ");
	const GLuint maxZID = glGetUniformLocation(programID, "maxZ");
	const GLuint minNZID = glGetUniformLocation(programID, "minNZ"); // prepared for disparity-to-depth convertion 
	const GLuint maxNZID = glGetUniformLocation(programID, "maxNZ");
	const GLuint nanID = glGetUniformLocation(programID, "nan");
	const GLuint widthID = glGetUniformLocation(programID, "width");
	const GLuint heightID = glGetUniformLocation(programID, "height");
	const GLuint swidthID = glGetUniformLocation(programID, "screen_width");
	const GLuint sheightID = glGetUniformLocation(programID, "screen_height");
	const GLuint layersID = glGetUniformLocation(programID, "layers");
	
	glUseProgram(programID);
	glUniform1f(param1ID, config.param1);
	glUniform1f(param2ID, config.param2);
	glUniform1f(minZID, config.minZ);
	glUniform1f(maxZID, config.maxZ);
	glUniform1f(minNZID, (1.0 / config.minZ - 1.0 / config.maxZ));
	glUniform1f(maxNZID, 1.0 / config.maxZ);
	glUniform1f(nanID, sqrt(-1.f));
	glUniform1ui(widthID, config.width);
	glUniform1ui(heightID, config.height);
	glUniform1ui(swidthID, screen_width);
	glUniform1ui(sheightID, screen_height);
	glUniform1ui(layersID, config.layers);
}


void read_rgb_frame(t3dtv::yuv_reader &color_reader, config::vi_config config, GLubyte *const rgb_buffer)
{
	color_reader.read_frame();

	const GLubyte * const frame = color_reader.frame->frame;
	const long HW = long(config.width) * config.height;

#pragma omp parallel for
	for (int yi = 0; yi < int(config.height); yi++)
	{
		long index = yi * long(config.width);

		for (int xi = 0; xi < int(config.width); xi++, index++)
		{
			rgb_buffer[index * 3] = frame[index];
			rgb_buffer[index * 3 + 1] = frame[index + HW];
			rgb_buffer[index * 3 + 2] = frame[index + HW * 2];
		}
	}

}
