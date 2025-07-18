#include <GL/glew.h>
//
// Include GLM
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace glm;

#include "controls.hpp"
#include <string>
#include <iostream>
#include <fstream>
#include <vector>

mat4 computeViewMatrix(const float speed, const float mouseSpeed, GLFWwindow *window)
{
	// Initial position : on +Z
	static glm::vec3 position = glm::vec3(0, 0, 0);
	// Initial horizontal angle : toward -Z
	static float horizontalAngle = 3.14f;
	// Initial vertical angle : none
	static float verticalAngle = 0.0f;

	// glfwGetTime is called only once, the first time this function is called
	static double lastTime = glfwGetTime();

	// Compute time difference between current and last frame
	double currentTime = glfwGetTime();
	float deltaTime = float(currentTime - lastTime);

	// Get mouse position
	int screen_width, screen_height;
	glfwGetWindowSize(window, &screen_width, &screen_height);
	double xpos, ypos;
	glfwGetCursorPos(window, &xpos, &ypos);
		
	// Reset mouse position for next frame
	glfwSetCursorPos(window, screen_width/2.0, screen_height/2.0);

	// Compute new orientation
	horizontalAngle += mouseSpeed * (screen_width/2.f - float(xpos));
	verticalAngle -= mouseSpeed * (screen_height / 2.f - float(ypos));

	// Direction : Spherical coordinates to Cartesian coordinates conversion
	glm::vec3 direction(
		cos(verticalAngle) * sin(horizontalAngle), 
		sin(verticalAngle),
		cos(verticalAngle) * cos(horizontalAngle)
	);
	
	// Right vector
	glm::vec3 right = glm::vec3(
		sin(horizontalAngle - 3.14f/2.0f), 
		0,
		cos(horizontalAngle - 3.14f/2.0f)
	);
	
	//// Up vector
	//glm::vec3 up = glm::vec3(
	//	0,
	//	cos(verticalAngle), 		
	//	-sin(verticalAngle)
	//);

	// Up vector
	glm::vec3 up = glm::cross( right, direction );

	// Move forward
	if (glfwGetKey(window, GLFW_KEY_UP ) == GLFW_PRESS || glfwGetKey(window, 'W' ) == GLFW_PRESS)
	{
		position += direction * deltaTime * speed;
	}
	// Move backward
	if (glfwGetKey(window, GLFW_KEY_DOWN ) == GLFW_PRESS || glfwGetKey(window, 'S' ) == GLFW_PRESS)
	{
		position -= direction * deltaTime * speed;
	}
	// Strafe right
	if (glfwGetKey(window, GLFW_KEY_RIGHT ) == GLFW_PRESS || glfwGetKey(window, 'D' ) == GLFW_PRESS)
	{
		position += right * deltaTime * speed;// * (config.x_axis_right ? 1.f : -1.f);
	}
	// Strafe left
	if (glfwGetKey(window, GLFW_KEY_LEFT ) == GLFW_PRESS || glfwGetKey(window, 'A' ) == GLFW_PRESS)
	{
		position -= right * deltaTime * speed;// * (config.x_axis_right ? 1.f : -1.f);
	}
	
	// Up
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT ) == GLFW_PRESS)
	{
		position -= up * deltaTime * speed;// * (config.y_axis_up ? -1.f : 1.f);
	}
	// Down
	if (glfwGetKey(window, GLFW_KEY_SPACE ) == GLFW_PRESS)
	{
		position += up * deltaTime * speed;// * (config.y_axis_up ? -1.f : 1.f);
	}

	//float FoV = initialFoV - 5 * glfwGetMouseWheel();

	// For the next frame, the "last time" will be "now"
	lastTime = currentTime;
	return glm::lookAt(
		position,           // Camera is here
		position + direction, // and looks here : at the same position, plus "direction"
		up                  // Head is up (set to 0,-1,0 to look upside-down)
		);
}


GLuint Load3Shaders(const char * vertex_file_path, const char * geometry_file_path, const char * fragment_file_path)//, std::vector <std::pair<std::string, std::string>> replaces)
{
	// Create the shaders
	GLuint VertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	GLuint GeometryShaderID = glCreateShader(GL_GEOMETRY_SHADER);
	GLuint FragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

	// Read the Vertex Shader code from the file
	std::string VertexShaderCode;
	std::ifstream VertexShaderStream(vertex_file_path, std::ios::in);
	if(VertexShaderStream.is_open())
	{
		std::string Line = "";
		while(getline(VertexShaderStream, Line))
			VertexShaderCode += "\n" + Line;
		VertexShaderStream.close();
	}
	else
	{
		char errTxt[400];
		sprintf(errTxt, "Impossible to open file %s. \n", vertex_file_path);
		throw std::runtime_error(errTxt);
	}

	// Read the Geometry Shader code from the file
	std::string GeometryShaderCode;
	std::ifstream GeometryShaderStream(geometry_file_path, std::ios::in);
	if(GeometryShaderStream.is_open())
	{
		std::string Line = "";
		while(getline(GeometryShaderStream, Line))
			GeometryShaderCode += "\n" + Line;
		GeometryShaderStream.close();
	}
	else
	{
		char errTxt[400];
		sprintf(errTxt, "Impossible to open file %s. \n", geometry_file_path);
		throw std::runtime_error(errTxt);
	}


	// Read the Fragment Shader code from the file
	std::string FragmentShaderCode;
	std::ifstream FragmentShaderStream(fragment_file_path, std::ios::in);
	if(FragmentShaderStream.is_open())
	{
		std::string Line = "";
		while(getline(FragmentShaderStream, Line))
			FragmentShaderCode += "\n" + Line;
		FragmentShaderStream.close();
	}
	else
	{
		char errTxt[400];
		sprintf(errTxt, "Impossible to open file %s. \n", fragment_file_path);
		throw std::runtime_error(errTxt);
	}


	GLint Result = GL_FALSE;
	int InfoLogLength;


	// Compile Vertex Shader
	printf("Compiling Vertex shader : %s\n", vertex_file_path);
	char const * VertexSourcePointer = VertexShaderCode.c_str();
	glShaderSource(VertexShaderID, 1, &VertexSourcePointer , NULL);
	glCompileShader(VertexShaderID);

	// Check Vertex Shader
	glGetShaderiv(VertexShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(VertexShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if ( InfoLogLength > 1 )
	{
		std::vector<char> VertexShaderErrorMessage(InfoLogLength+1);
		glGetShaderInfoLog(VertexShaderID, InfoLogLength, NULL, &VertexShaderErrorMessage[0]);
		throw std::runtime_error(&VertexShaderErrorMessage[0]);
	}

	
	// Compile Geometry Shader
	printf("Compiling Geometry shader : %s\n", geometry_file_path);
	char const * GeometrySourcePointer = GeometryShaderCode.c_str();
	glShaderSource(GeometryShaderID, 1, &GeometrySourcePointer , NULL);
	glCompileShader(GeometryShaderID);

	// Check Geometry Shader
	glGetShaderiv(GeometryShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(GeometryShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if ( InfoLogLength > 1 )
	{
		std::vector<char> GeometryShaderErrorMessage(InfoLogLength+1);
		glGetShaderInfoLog(GeometryShaderID, InfoLogLength, NULL, &GeometryShaderErrorMessage[0]);
		throw std::runtime_error(&GeometryShaderErrorMessage[0]);
	}

	// Compile Fragment Shader
	printf("Compiling fragment shader : %s\n", fragment_file_path);
	char const * FragmentSourcePointer = FragmentShaderCode.c_str();
	glShaderSource(FragmentShaderID, 1, &FragmentSourcePointer , NULL);
	glCompileShader(FragmentShaderID);

	// Check Fragment Shader
	glGetShaderiv(FragmentShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(FragmentShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if ( InfoLogLength > 1 )
	{
		std::vector<char> FragmentShaderErrorMessage(InfoLogLength+1);
		glGetShaderInfoLog(FragmentShaderID, InfoLogLength, NULL, &FragmentShaderErrorMessage[0]);
		throw std::runtime_error(&FragmentShaderErrorMessage[0]);
	}



	// Link the program
	printf("Linking program\n");
	GLuint ProgramID = glCreateProgram();
	glAttachShader(ProgramID, VertexShaderID);
	glAttachShader(ProgramID, FragmentShaderID);
	glAttachShader(ProgramID, GeometryShaderID);
	glLinkProgram(ProgramID);

	// Check the program
	glGetProgramiv(ProgramID, GL_LINK_STATUS, &Result);
	glGetProgramiv(ProgramID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if ( InfoLogLength > 1 )
	{
		std::vector<char> ProgramErrorMessage(InfoLogLength+1);
		glGetProgramInfoLog(ProgramID, InfoLogLength, NULL, &ProgramErrorMessage[0]);
		throw std::runtime_error(&ProgramErrorMessage[0]);
	}

	glDeleteShader(VertexShaderID);
	glDeleteShader(FragmentShaderID);
	glDeleteShader(GeometryShaderID);

	return ProgramID;
}

GLuint LoadShaders(const char * vertex_file_path,const char * fragment_file_path)
{

	// Create the shaders
	GLuint VertexShaderID = glCreateShader(GL_VERTEX_SHADER);
	GLuint FragmentShaderID = glCreateShader(GL_FRAGMENT_SHADER);

	// Read the Vertex Shader code from the file
	std::string VertexShaderCode;
	std::ifstream VertexShaderStream(vertex_file_path, std::ios::in);
	if(VertexShaderStream.is_open())
	{
		std::string Line = "";
		while(getline(VertexShaderStream, Line))
			VertexShaderCode += "\n" + Line;
		VertexShaderStream.close();
	}
	else
	{
		char errTxt[400];
		sprintf(errTxt, "Impossible to open %s. \n", vertex_file_path);
		throw std::runtime_error(errTxt);
		return 0;
	}

	// Read the Fragment Shader code from the file
	std::string FragmentShaderCode;
	std::ifstream FragmentShaderStream(fragment_file_path, std::ios::in);
	if(FragmentShaderStream.is_open())
	{
		std::string Line = "";
		while(getline(FragmentShaderStream, Line))
			FragmentShaderCode += "\n" + Line;
		FragmentShaderStream.close();
	}
	else
	{
		char errTxt[400];
		sprintf(errTxt, "Impossible to open %s. \n", fragment_file_path);
		throw std::runtime_error(errTxt);
		return 0;
	}


	GLint Result = GL_FALSE;
	int InfoLogLength;


	// Compile Vertex Shader
	printf("Compiling vertex shader : %s\n", vertex_file_path);
	char const * VertexSourcePointer = VertexShaderCode.c_str();
	glShaderSource(VertexShaderID, 1, &VertexSourcePointer , NULL);
	glCompileShader(VertexShaderID);

	// Check Vertex Shader
	glGetShaderiv(VertexShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(VertexShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if ( InfoLogLength > 1 )
	{
		std::vector<char> VertexShaderErrorMessage(InfoLogLength+1);
		glGetShaderInfoLog(VertexShaderID, InfoLogLength, NULL, &VertexShaderErrorMessage[0]);
		throw std::runtime_error(&VertexShaderErrorMessage[0]);
	}



	// Compile Fragment Shader
	printf("Compiling fragment shader : %s\n", fragment_file_path);
	char const * FragmentSourcePointer = FragmentShaderCode.c_str();
	glShaderSource(FragmentShaderID, 1, &FragmentSourcePointer , NULL);
	glCompileShader(FragmentShaderID);

	// Check Fragment Shader
	glGetShaderiv(FragmentShaderID, GL_COMPILE_STATUS, &Result);
	glGetShaderiv(FragmentShaderID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if ( InfoLogLength > 1 )
	{
		std::vector<char> FragmentShaderErrorMessage(InfoLogLength+1);
		glGetShaderInfoLog(FragmentShaderID, InfoLogLength, NULL, &FragmentShaderErrorMessage[0]);
		throw std::runtime_error(&FragmentShaderErrorMessage[0]);
	}

	// Link the program
	printf("Linking program\n");
	GLuint ProgramID = glCreateProgram();
	glAttachShader(ProgramID, VertexShaderID);
	glAttachShader(ProgramID, FragmentShaderID);
	glLinkProgram(ProgramID);

	// Check the program
	glGetProgramiv(ProgramID, GL_LINK_STATUS, &Result);
	glGetProgramiv(ProgramID, GL_INFO_LOG_LENGTH, &InfoLogLength);
	if ( InfoLogLength > 1 )
	{
		std::vector<char> ProgramErrorMessage(InfoLogLength+1);
		glGetProgramInfoLog(ProgramID, InfoLogLength, NULL, &ProgramErrorMessage[0]);
		throw std::runtime_error(&ProgramErrorMessage[0]);
	}

	glDeleteShader(VertexShaderID);
	glDeleteShader(FragmentShaderID);

	return ProgramID;
}


