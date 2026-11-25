#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace config
{
	//! Pinhole camera with optional lens distortion.
	//! World -> camera: x_cam = R * X + t; pixel ~ K * x_cam (y axis pointing down, origin at top-left pixel).
	struct camera_params
	{
		std::string name;
		glm::mat3 K{1.f};               // intrinsics (GLM column-major)
		glm::mat3 R{1.f};               // world -> camera rotation
		glm::vec3 t{0.f};               // world -> camera translation
		float dist[5] = {0, 0, 0, 0, 0}; // OpenCV order: k1, k2, p1, p2, k3 (normalized coordinates)
		int width = 0, height = 0;      // resolution the intrinsics refer to; 0 = unknown (assume processing resolution)

		bool has_distortion() const
		{
			for (float d : dist)
				if (d != 0.f) return true;
			return false;
		}

		glm::mat4x3 extrinsics() const { return glm::mat4x3(R[0], R[1], R[2], t); }
		//! 4x4 [K*R | K*t ; 0 0 0 1], maps homogeneous world points to (u*z, v*z, z, 1)
		glm::mat4 projection() const { return glm::mat4(K * extrinsics()); }
		glm::vec3 center() const { return -(glm::transpose(R) * t); }
		glm::vec3 forward() const { return glm::row(R, 2); } // optical axis in world coordinates

		//! Returns a copy whose intrinsics refer to an image of size w x h (pixel-centre aware scaling).
		camera_params scaled_to(int w, int h) const
		{
			camera_params c = *this;
			if (width <= 0 || height <= 0 || (width == w && height == h))
			{
				c.width = w;
				c.height = h;
				return c;
			}
			const float sx = float(w) / width, sy = float(h) / height;
			c.K[0][0] *= sx;                         // fx
			c.K[1][0] *= sx;                         // skew
			c.K[1][1] *= sy;                         // fy
			c.K[2][0] = (K[2][0] + 0.5f) * sx - 0.5f; // cx
			c.K[2][1] = (K[2][1] + 0.5f) * sy - 0.5f; // cy
			c.width = w;
			c.height = h;
			return c;
		}
	};

	//! Loads camera calibration files. Two formats are supported:
	//!  * *.json - CMU Panoptic calibration ({"cameras": [{"name", "resolution", "K", "distCoef", "R", "t"}, ...]})
	//!  * anything else - plain text, a sequence of blocks:
	//!        <name>
	//!        3x3 K (row-major)
	//!        k1 k2           (radial distortion)
	//!        3x4 [R|t] (row-major, world -> camera)
	class camera_loader
	{
	public:
		static camera_params read_settings(const std::string &camera_file, const std::string &camera_name)
		{
			const std::map<std::string, camera_params> &cams = load_all(camera_file);
			const auto it = cams.find(camera_name);
			if (it == cams.end())
			{
				throw std::runtime_error("Camera \"" + camera_name + "\" not found in " + camera_file);
			}
			return it->second;
		}

		//! Parses (and caches) all cameras of a file.
		static const std::map<std::string, camera_params> &load_all(const std::string &camera_file)
		{
			static std::map<std::string, std::map<std::string, camera_params>> cache;
			auto it = cache.find(camera_file);
			if (it == cache.end())
			{
				it = cache.emplace(camera_file, is_json(camera_file) ? parse_json(camera_file) : parse_text(camera_file)).first;
			}
			return it->second;
		}

	private:
		static bool is_json(const std::string &f)
		{
			return f.size() >= 5 && f.compare(f.size() - 5, 5, ".json") == 0;
		}

		static bool is_number(const std::string &s)
		{
			char *end = nullptr;
			std::strtod(s.c_str(), &end);
			return end != s.c_str() && *end == '\0';
		}

		static std::map<std::string, camera_params> parse_text(const std::string &camera_file)
		{
			std::ifstream camfile(camera_file);
			if (!camfile.is_open() || !camfile.good())
			{
				throw std::runtime_error("Could not open camera file " + camera_file);
			}

			std::map<std::string, camera_params> cams;
			std::string token;
			while (camfile >> token)
			{
				if (is_number(token))
				{
					throw std::runtime_error("Unexpected number \"" + token + "\" in camera file " + camera_file);
				}
				float v[23];
				for (float &x : v)
				{
					if (!(camfile >> x))
					{
						throw std::runtime_error("Incomplete parameters of camera \"" + token + "\" in " + camera_file);
					}
				}
				camera_params c;
				c.name = token;
				// v[0..8]: K row-major; GLM constructors take columns
				c.K = glm::mat3(v[0], v[3], v[6], v[1], v[4], v[7], v[2], v[5], v[8]);
				c.dist[0] = v[9];
				c.dist[1] = v[10];
				// v[11..22]: [R|t] row-major
				const float *e = v + 11;
				c.R = glm::mat3(e[0], e[4], e[8], e[1], e[5], e[9], e[2], e[6], e[10]);
				c.t = glm::vec3(e[3], e[7], e[11]);
				cams[c.name] = c;
			}
			return cams;
		}

		static std::vector<float> flat_numbers(const boost::property_tree::ptree &node)
		{
			// JSON arrays are ptree children with empty keys; nested arrays are flattened row by row
			std::vector<float> out;
			if (node.empty())
			{
				out.push_back(node.get_value<float>());
				return out;
			}
			for (const auto &child : node)
			{
				const std::vector<float> sub = flat_numbers(child.second);
				out.insert(out.end(), sub.begin(), sub.end());
			}
			return out;
		}

		static std::map<std::string, camera_params> parse_json(const std::string &camera_file)
		{
			boost::property_tree::ptree pt;
			boost::property_tree::read_json(camera_file, pt);

			std::map<std::string, camera_params> cams;
			for (const auto &item : pt.get_child("cameras"))
			{
				const boost::property_tree::ptree &cam = item.second;
				camera_params c;
				c.name = cam.get<std::string>("name");

				const std::vector<float> k = flat_numbers(cam.get_child("K"));
				const std::vector<float> r = flat_numbers(cam.get_child("R"));
				const std::vector<float> t = flat_numbers(cam.get_child("t"));
				if (k.size() != 9 || r.size() != 9 || t.size() != 3)
				{
					throw std::runtime_error("Malformed calibration of camera \"" + c.name + "\" in " + camera_file);
				}
				c.K = glm::mat3(k[0], k[3], k[6], k[1], k[4], k[7], k[2], k[5], k[8]);
				c.R = glm::mat3(r[0], r[3], r[6], r[1], r[4], r[7], r[2], r[5], r[8]);
				c.t = glm::vec3(t[0], t[1], t[2]);

				if (const auto d = cam.get_child_optional("distCoef"))
				{
					const std::vector<float> dc = flat_numbers(*d);
					for (size_t i = 0; i < dc.size() && i < 5; i++)
						c.dist[i] = dc[i];
				}
				if (const auto res = cam.get_child_optional("resolution"))
				{
					const std::vector<float> wh = flat_numbers(*res);
					if (wh.size() == 2)
					{
						c.width = int(wh[0]);
						c.height = int(wh[1]);
					}
				}
				cams[c.name] = c;
			}
			return cams;
		}
	};
};
