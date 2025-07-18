#pragma once

#include <glm/glm.hpp>
#include <iostream>
#include <fstream>
#include <utility>

namespace config
{

	class camera_loader
	{
	public:

		//! returns intrinsic and extrinsic matrixes
		static const std::pair<const glm::mat3, const glm::mat4x3> read_settings(std::string camera_settings, std::string camera_name)
		{
			std::ifstream camfile(camera_settings);
			if (!camfile.is_open() || !camfile.good()) {
				throw std::runtime_error("Could not open camera file!");
			}
			std::string line{};
			while (std::getline(camfile, line))
			{
				std::cout << line << "\n";
				if (line.find(camera_name) != std::string::npos) {
					break;
				}
			}

			if (line.find(camera_name) == std::string::npos)
			{
				throw std::runtime_error("No specified camera parameters found!");
			}

			float a, b, c;
			float d, e, f;
			float g, h, i;
			camfile >> a >> b >> c;
			camfile >> d >> e >> f;
			camfile >> g >> h >> i;

			//! Column-major format here!!! THAT'S WRONG: //glm::mat3 intrinsics = glm::mat3 (a, b, c,  d, e, f,   g, h, i); 
			glm::mat3 intrinsics = glm::mat3(a, d, g, b, e, h, c, f, i);


			float b1, b2;
			camfile >> b1 >> b2;

			float j, k, l;
			camfile >> a >> b >> c >> d;
			camfile >> e >> f >> g >> h;
			camfile >> i >> j >> k >> l;

			glm::mat4x3 extrinsics = glm::mat4x3(a, e, i, b, f, j, c, g, k, d, h, l);

			camfile.close();

			return std::make_pair(intrinsics, extrinsics);
		}


	};

};