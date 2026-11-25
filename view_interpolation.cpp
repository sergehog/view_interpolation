/*! view-interpolation OpenGL demo
* \author: Sergey Smirnov <sergei.smirnov@gmail.com>
* \date 24.02.2014
**/

#define GLM_FORCE_SSE2

#include <GL/glew.h>
#include "vi_config.h"
#include "common/camera_loader.h"
#include "common/frame_source.h"
#include "common/controls.hpp"
#include "lodepng.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <memory>
#include <string>
#include <vector>


using namespace glm;


unsigned prepare_buffers(config::vi_config config, GLuint &uvbuffer, GLuint &elementbuffer);
void setup_program(GLuint programID, config::vi_config config, int screen_width, int screen_height);

//! One input view: calibrated camera (intrinsics scaled to the processing resolution) and its frame source.
struct input_view
{
	config::camera_params camera;
	mat4 projection;
	std::unique_ptr<vi::buffered_source> source;
};

std::vector<input_view> open_views(const config::vi_config &config, long &loop_length, double &fps);
std::pair<int, int> select_views(const std::vector<input_view> &views, const vec3 &center, const vec3 &direction,
                                 float max_angle_deg, std::pair<int, int> previous);

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
	// command line: config-file.xml [--snapshot out.png [--frame N]]
	// --snapshot renders the initial virtual view (of input frame N, default 0), saves it as PNG and exits.
	std::string snapshot_file;
	long snapshot_frame = 0;
	for (int i = 2; i < argc; i++)
	{
		if (!strcmp(argv[i], "--snapshot") && i + 1 < argc) snapshot_file = argv[++i];
		else if (!strcmp(argv[i], "--frame") && i + 1 < argc) snapshot_frame = std::stol(argv[++i]);
		else argc = 0; // unknown argument: print usage
	}
	if (argc < 2)
	{
		std::cout << "USAGE: view_interpolation config-file.xml [--snapshot out.png [--frame N]]" << std::endl;
		return 1;
	}
	const bool snapshot = !snapshot_file.empty();

	try
	{

		const static config::vi_config config = config::vi_config::load_config(argv[1]);
		const config::camera_params output_camera = config::camera_loader::read_settings(config.desired_camera_file, config.desired_camera_name)
			.scaled_to(int(config.width), int(config.height));
		// virtual camera projection CV = K4 * E; the free-fly motion is applied between the two, i.e. in the virtual camera's own frame
		const mat4 K4inv = glm::inverse(mat4(output_camera.K));
		const mat4 Einv = glm::inverse(mat4(output_camera.extrinsics()));
		const vec4 principal_ray(output_camera.K[2][0], output_camera.K[2][1], 1, 0); // (cx, cy, 1) at infinity

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
		
		long loop_length = 0;
		double fps = 0;
		std::vector<input_view> views = open_views(config, loop_length, fps);
		if (snapshot)
		{
			for (long f = 0; loop_length > 1 && f < snapshot_frame % loop_length; f++)
			{
				for (input_view &v : views) v.source->advance();
			}
		}

		GLFWwindow* window = glfwCreateWindow(config.output_width, config.output_height, "View-plus-depth OpenGL rendering demo", NULL, NULL); // windowed

		// Open a window and create its OpenGL context		
		if (!window)
		{
			glfwTerminate();
			throw std::runtime_error("Failed to open GLFW window.");
		}

		glfwMakeContextCurrent(window);

		int screen_width, screen_height;
		glfwGetFramebufferSize(window, &screen_width, &screen_height); // pixels, may exceed the window size on HiDPI displays
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



		GLuint uvbuffer_internal, uvbuffer_main;
		// Internal pass: two triangles covering the input image. This is exact for any camera
		// geometry because the shaders interpolate homogeneous coordinates and divide per fragment.
		const GLint uv_buffer_int[] = { 0, 0, GLint(config.width), 0, 0, GLint(config.height),
			GLint(config.width), 0, GLint(config.width), GLint(config.height), 0, GLint(config.height) };
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
		
		// full mip chain: the display pass aggregates costs by sampling mip levels 1-3
		const GLsizei mip_levels = GLsizei(std::floor(std::log2(double(std::max(screen_width, screen_height))))) + 1;
		glTexStorage3D(GL_TEXTURE_2D_ARRAY, mip_levels, GL_RGBA8, screen_width, screen_height, config.layers);

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


		glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // input rows are tightly packed BGR24

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

		float curr_fps = 0;
		double lastTime = glfwGetTime();
		unsigned nbFrames = 0;
		unsigned rendered_frames = 0;
		unsigned long total_frames = 0;

		// video clock: input frames advance at `fps`, independently of the rendering rate
		const double frame_period = 1.0 / fps;
		double next_frame_time = glfwGetTime() + frame_period;
		long video_frame = 0;

		// which views are bound to Texture1/Texture2, and which of their frames was uploaded last
		std::pair<int, int> pair(-1, -1);
		unsigned long uploaded_serial[2] = {0, 0};
		int uploaded_view[2] = {-1, -1};

		
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
				printf("frame %ld (views %s + %s): %3.1f FPS\n", video_frame, views[pair.first].camera.name.c_str(),
				       views[pair.second].camera.name.c_str(), curr_fps);
				nbFrames = 0;
				lastTime = currentTime;
			}

			if (!snapshot && loop_length > 1 && currentTime >= next_frame_time)
			{
				for (input_view &v : views)
				{
					v.source->advance();
				}
				video_frame = (video_frame + 1) % loop_length;
				next_frame_time += frame_period;
				if (currentTime > next_frame_time)
				{
					next_frame_time = currentTime + frame_period; // decoding can't keep up: slow down instead of skipping
				}
			}

			glBindFramebuffer(GL_FRAMEBUFFER, FramebufferName);
			glViewport(0, 0, screen_width, screen_height); // Render on the whole framebuffer, complete from the lower left corner to the upper right
			//glViewport(0, 0, config.width, config.height);
			glClearColor(0.0f, 2.0f, 0.0f, 0.0f);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			// Use our shader
			glUseProgram(internal_programID);

			const mat4 ViewMatrix = computeViewMatrix(config.speed, config.mouseSpeed, window);
			// (u*z, v*z, z, 1) of the moved virtual camera -> world. Moving in world coordinates
			// (ViewMatrix * inverse(CV)) is only right when the virtual camera frame is the world frame.
			const mat4 Cinv = Einv * ViewMatrix * K4inv;

			// pick the pair of input views that best brackets the (moved) virtual camera
			const vec3 virtual_center = vec3(Cinv * vec4(0, 0, 0, 1));
			const vec3 virtual_direction = glm::normalize(vec3(Cinv * principal_ray));
			const std::pair<int, int> new_pair = select_views(views, virtual_center, virtual_direction, config.max_view_angle, pair);
			if (new_pair != pair)
			{
				pair = new_pair;
				printf("Using views %s + %s\n", views[pair.first].camera.name.c_str(), views[pair.second].camera.name.c_str());
			}

			// upload input frames only when the bound view or its frame changed
			const int bound[2] = {pair.first, pair.second};
			const GLuint textures[2] = {texture1ID, texture2ID};
			for (int i = 0; i < 2; i++)
			{
				const vi::buffered_source &src = *views[bound[i]].source;
				if (uploaded_view[i] == bound[i] && uploaded_serial[i] == src.serial()) continue;
				glActiveTexture(GL_TEXTURE0 + i);
				glBindTexture(GL_TEXTURE_2D, textures[i]);
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, config.width, config.height, GL_RGB, GL_UNSIGNED_BYTE, src.current());
				glGenerateMipmap(GL_TEXTURE_2D);
				uploaded_view[i] = bound[i];
				uploaded_serial[i] = src.serial();
			}

			const mat4 MVP1 = views[pair.first].projection * Cinv;
			const mat4 MVP2 = views[pair.second].projection * Cinv;

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

			if (++total_frames == 1) // report OpenGL errors of the setup and the first frame once
			{
				for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError())
				{
					printf("OpenGL error 0x%04x\n", err);
				}
			}

			if (snapshot && ++rendered_frames >= 3) // a few warm-up frames, then save the back buffer and quit
			{
				std::vector<unsigned char> pixels(size_t(screen_width) * screen_height * 3), flipped(pixels.size());
				glReadBuffer(GL_BACK);
				glPixelStorei(GL_PACK_ALIGNMENT, 1);
				glReadPixels(0, 0, screen_width, screen_height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
				const size_t row = size_t(screen_width) * 3;
				for (int y = 0; y < screen_height; y++)
				{
					memcpy(&flipped[y * row], &pixels[(screen_height - 1 - y) * row], row);
				}
				const unsigned err = lodepng::encode(snapshot_file, flipped, screen_width, screen_height, LCT_RGB);
				printf(err ? "Cannot write snapshot %s\n" : "Snapshot saved to %s\n", snapshot_file.c_str());
				break;
			}

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


std::vector<input_view> open_views(const config::vi_config &config, long &loop_length, double &fps)
{
	const int w = int(config.width), h = int(config.height);

	// open all decoders first, to agree on a common loop length (keeps videos frame-synchronous)
	std::vector<std::unique_ptr<vi::frame_decoder>> decoders;
	loop_length = 0;
	fps = config.fps;
	for (const config::vi_config::view &v : config.views)
	{
		decoders.push_back(vi::open_decoder(v.file, w, h));
		const long count = decoders.back()->frame_count();
		printf("Input %s: camera %s, %ld frame(s), %.2f fps\n", v.file.c_str(), v.camera.c_str(), count, decoders.back()->fps());
		if (count > 1)
		{
			loop_length = (loop_length == 0) ? count : std::min(loop_length, count);
		}
		if (fps <= 0 && decoders.back()->fps() > 0)
		{
			fps = decoders.back()->fps();
		}
	}
	if (config.frames > 0 && (loop_length == 0 || long(config.frames) < loop_length))
	{
		loop_length = config.frames;
	}
	if (fps <= 0)
	{
		fps = 25;
	}
	if (loop_length > 1)
		printf("Playing %ld frames in a loop at %.2f fps\n", loop_length, fps);
	else
		printf("All inputs are still images\n");

	std::vector<input_view> views;
	for (size_t i = 0; i < config.views.size(); i++)
	{
		input_view view;
		view.camera = config::camera_loader::read_settings(config.camera_file, config.views[i].camera).scaled_to(w, h);
		view.projection = view.camera.projection();
		std::unique_ptr<vi::undistort_map> undistort;
		if (view.camera.has_distortion())
		{
			undistort = std::make_unique<vi::undistort_map>(view.camera, w, h);
		}
		view.source = std::make_unique<vi::buffered_source>(std::move(decoders[i]), w, h, std::move(undistort),
		                                                     loop_length, config.buffer_frames);
		views.push_back(std::move(view));
	}
	return views;
}

//! Chooses the two input views to interpolate from: the pair whose connecting segment passes closest
//! to the virtual camera centre (plus a small penalty for wide baselines), among views looking
//! roughly in the same direction as the virtual camera. Keeps the previous pair unless the new one is clearly better.
std::pair<int, int> select_views(const std::vector<input_view> &views, const vec3 &center, const vec3 &direction,
                                 const float max_angle_deg, const std::pair<int, int> previous)
{
	const int n = int(views.size());
	if (n == 2)
	{
		return {0, 1};
	}

	const float min_cos = std::cos(glm::radians(max_angle_deg));
	std::vector<int> candidates;
	for (int i = 0; i < n; i++)
	{
		if (glm::dot(views[i].camera.forward(), direction) >= min_cos) candidates.push_back(i);
	}
	if (candidates.size() < 2)
	{
		candidates.clear();
		for (int i = 0; i < n; i++) candidates.push_back(i);
	}

	auto score = [&](int a, int b) {
		const vec3 ca = views[a].camera.center(), cb = views[b].camera.center();
		const vec3 ab = cb - ca;
		const float s = glm::clamp(glm::dot(center - ca, ab) / glm::dot(ab, ab), 0.f, 1.f);
		return glm::length(center - (ca + s * ab)) + 0.25f * glm::length(ab);
	};

	std::pair<int, int> best(candidates[0], candidates[1]);
	float best_score = std::numeric_limits<float>::max();
	for (size_t i = 0; i < candidates.size(); i++)
	{
		for (size_t j = i + 1; j < candidates.size(); j++)
		{
			const float sc = score(candidates[i], candidates[j]);
			if (sc < best_score)
			{
				best_score = sc;
				best = {candidates[i], candidates[j]};
			}
		}
	}

	// hysteresis against flickering between two similar pairs
	if (previous.first >= 0 && previous != best && score(previous.first, previous.second) <= best_score * 1.05f)
	{
		return previous;
	}
	return best;
}
