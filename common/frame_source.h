/*! Input frame sources: decoding (raw YUV / FFmpeg), lens undistortion and background buffering.
 *
 * All frames handed to the renderer are BGR24, interleaved, top row first, width x height of the
 * processing resolution. BGR because the shaders sample the input textures with a `.bgr` swizzle
 * (historically raw_frame::yuv2rgb emitted B,G,R planes).
 */
#pragma once

#include "camera_loader.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vi
{
	//! Sequential frame decoder producing BGR24 frames of a fixed size.
	class frame_decoder
	{
	public:
		virtual ~frame_decoder() = default;
		//! Decodes the next frame into dst (width*height*3 bytes). Returns false at end of stream.
		virtual bool decode_next(uint8_t *dst) = 0;
		//! Restarts decoding from the first frame.
		virtual void rewind() = 0;
		//! Number of frames, or 0 if unknown.
		virtual long frame_count() const = 0;
		//! Native frame rate, or 0 if unknown.
		virtual double fps() const = 0;
	};

	//! Opens a decoder (FFmpeg). *.yuv files are raw planar YUV420 of exactly width x height;
	//! anything else (videos, image files, image sequences like "img_%04d.png") is scaled to width x height.
	std::unique_ptr<frame_decoder> open_decoder(const std::string &file, int width, int height);

	//! Precomputed inverse lens-distortion lookup (OpenCV model), bilinear resampling.
	class undistort_map
	{
	public:
		//! cam must already be scaled to width x height.
		undistort_map(const config::camera_params &cam, int width, int height);
		void apply(const uint8_t *src, uint8_t *dst) const;

	private:
		int width, height;
		std::vector<int32_t> offset; // index of the top-left source pixel, -1 if outside the image
		std::vector<uint8_t> wx, wy; // bilinear weights in 1/256
	};

	//! A decoder running in its own thread, a few frames ahead of the renderer.
	//! Sources with a single frame (still images) are decoded once and need no thread.
	class buffered_source
	{
	public:
		//! loop_length > 0: rewind after that many frames (keeps several videos in sync); 0: rewind at end of stream.
		buffered_source(std::unique_ptr<frame_decoder> decoder, int width, int height,
		                std::unique_ptr<undistort_map> undistort, long loop_length, size_t capacity = 4);
		~buffered_source();

		buffered_source(const buffered_source &) = delete;
		buffered_source &operator=(const buffered_source &) = delete;

		//! Advances to the next frame (blocks until it is decoded). No-op for still images.
		void advance();
		//! Current frame (BGR24).
		const uint8_t *current() const { return current_frame.data(); }
		//! Increments whenever current() changes.
		unsigned long serial() const { return current_serial; }
		bool is_still() const { return still; }

	private:
		void run();
		bool decode_one(std::vector<uint8_t> &dst); // false if the stream has no frames at all

		std::unique_ptr<frame_decoder> decoder;
		std::unique_ptr<undistort_map> undistort;
		const size_t frame_bytes;
		const long loop_length;
		const size_t capacity;
		bool still = false;
		long decoded_in_loop = 0;
		bool warned = false;

		std::vector<uint8_t> scratch;       // decoder output before undistortion (worker thread only)
		std::vector<uint8_t> current_frame; // owned by the render thread
		unsigned long current_serial = 0;

		std::mutex mutex;
		std::condition_variable cv;
		std::deque<std::vector<uint8_t>> ready; // decoded frames, oldest first
		std::vector<std::vector<uint8_t>> spare; // recycled buffers
		std::exception_ptr error;
		bool stopping = false;
		std::thread worker;
	};
};
