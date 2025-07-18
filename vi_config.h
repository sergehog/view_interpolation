/* video_config.h - class for handling external parameters to the program
	@author Sergey Smirnov
	@date 28.06.2010
*/
#pragma once

#include <string>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>


#define CONFIG_PARAM(a,b,c)
#define CONFIG_PARAM_OPTIONAL(a,b,c,d)

#define CONFIG_PARAMS_LIST \
	CONFIG_PARAM(string, color1_file, "config.input.color1") \
	CONFIG_PARAM(string, color2_file, "config.input.color2") \
	CONFIG_PARAM(unsigned, width, "config.input.width") \
	CONFIG_PARAM(unsigned, height, "config.input.height") \
	CONFIG_PARAM_OPTIONAL(unsigned, frames, "config.input.frames", 1) \
	CONFIG_PARAM(string, camera_file, "config.input.camera_file") \
	CONFIG_PARAM(string, camera1_name, "config.input.camera1") \
	CONFIG_PARAM(string, camera2_name, "config.input.camera2") \
	CONFIG_PARAM_OPTIONAL(bool, y_axis_up, "config.input.y_axis_up", false) \
	CONFIG_PARAM_OPTIONAL(bool, x_axis_right, "config.input.x_axis_right", true) \
	CONFIG_PARAM(float, minZ, "config.input.minZ") \
	CONFIG_PARAM(float, maxZ, "config.input.maxZ") \
	CONFIG_PARAM_OPTIONAL(unsigned, output_width, "config.settings.output_width", width) \
	CONFIG_PARAM_OPTIONAL(unsigned, output_height, "config.settings.output_height", height) \
	CONFIG_PARAM(unsigned, layers, "config.settings.layers") \
	CONFIG_PARAM_OPTIONAL(unsigned, fsaa, "config.settings.fsaa", 4) \
	CONFIG_PARAM(float, speed, "config.settings.speed") \
	CONFIG_PARAM(float, mouseSpeed, "config.settings.mouseSpeed") \
	CONFIG_PARAM_OPTIONAL(float, param1, "config.settings.param1", 0.f) \
	CONFIG_PARAM_OPTIONAL(float, param2, "config.settings.param2", 0.f) \
	CONFIG_PARAM_OPTIONAL(string, desired_camera_file, "config.settings.desired_camera_file", camera_file) \
	CONFIG_PARAM(string, desired_camera_name, "config.settings.desired_camera") \
	CONFIG_PARAM(string, vertex_shader, "config.settings.vertex_shader") \
	CONFIG_PARAM(string, fragment_shader, "config.settings.fragment_shader") \
	CONFIG_PARAM_OPTIONAL(string, geometry_shader, "config.settings.geometry_shader", "") \
	CONFIG_PARAM(string, vertex_internal, "config.settings.vertex_internal") \
	CONFIG_PARAM(string, fragment_internal, "config.settings.fragment_internal") \
	CONFIG_PARAM_OPTIONAL(string, geometry_internal, "config.settings.geometry_internal", "") \
	CONFIG_PARAM_OPTIONAL(string, limits_file, "config.settings.limits_file", "")

using namespace std;
namespace config
{

	class vi_config
	{

#undef CONFIG_PARAM
#undef CONFIG_PARAM_OPTIONAL
#define CONFIG_PARAM(type, name, path)  const type name, 
#define CONFIG_PARAM_OPTIONAL(type, name, path, value) const type name,

	private:
	const int empty;
	//! private constructor , please use static load_config instead
	vi_config(
		CONFIG_PARAMS_LIST
		int empty
		) :
#undef CONFIG_PARAM
#undef CONFIG_PARAM_OPTIONAL
#define CONFIG_PARAM(type, name, path)  name(name), 
#define CONFIG_PARAM_OPTIONAL(type, name, path, value) name(name),
		CONFIG_PARAMS_LIST
		empty(0)	
		{
		
		};
	
public: 

#undef CONFIG_PARAM
#undef CONFIG_PARAM_OPTIONAL
#define CONFIG_PARAM(type, name, path)  const type name;
#define CONFIG_PARAM_OPTIONAL(type, name, path, value) const type name;

	CONFIG_PARAMS_LIST

	
	static vi_config load_config(const char* filename)
	{		
		
		boost::property_tree::ptree pt;	
		boost::property_tree::read_xml(std::string(filename), pt);
		if(pt.empty())
		{
			printf("Config file \"%s\" is not valuid XML file!\n", filename);
			throw std::runtime_error("Config file is not valuid XML file!");
		}

#undef CONFIG_PARAM
#undef CONFIG_PARAM_OPTIONAL
#define CONFIG_PARAM(type, name, path)  const type name = pt.get<type>(path);
#define CONFIG_PARAM_OPTIONAL(type, name, path, value) const type name = pt.get<type>(path, value);

		CONFIG_PARAMS_LIST

#undef CONFIG_PARAM
#undef CONFIG_PARAM_OPTIONAL
#define CONFIG_PARAM(type, name, path)  name,
#define CONFIG_PARAM_OPTIONAL(type, name, path, value) name,

			return vi_config(CONFIG_PARAMS_LIST 0);
	}

	
};


};
