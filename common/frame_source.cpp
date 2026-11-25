#include "frame_source.h"
#include "yuv_reader.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace vi
{
	namespace
	{
		std::string av_error(int code)
		{
			char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
			av_strerror(code, buf, sizeof(buf));
			return buf;
		}

		bool ends_with(const std::string &s, const std::string &suffix)
		{
			if (s.size() < suffix.size()) return false;
			return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
			                  [](char a, char b) { return std::tolower(a) == std::tolower(b); });
		}

		//! Raw planar YUV 4:2:0 file, no scaling (file resolution must equal the processing resolution).
		class yuv_decoder : public frame_decoder
		{
		public:
			yuv_decoder(const std::string &file, int w, int h)
				: reader(file, w, h, true, true, 0), width(w), height(h)
			{
				if (reader.is_bad())
				{
					throw std::runtime_error("Cannot read YUV file " + file);
				}
				std::ifstream f(file, std::ios::binary | std::ios::ate);
				count = long(f.tellg() / (std::streamoff(w) * h * 3 / 2));
				if (count < 1)
				{
					throw std::runtime_error("YUV file " + file + " is smaller than one frame");
				}
			}

			bool decode_next(uint8_t *dst) override
			{
				if (index >= count || !reader.read_frame()) return false;
				index++;
				// raw_frame::yuv2rgb stores the planes in B, G, R order
				const uint8_t *planes = reader.frame->frame;
				const long HW = long(width) * height;
				for (long i = 0; i < HW; i++)
				{
					dst[i * 3] = planes[i];
					dst[i * 3 + 1] = planes[i + HW];
					dst[i * 3 + 2] = planes[i + HW * 2];
				}
				return true;
			}

			void rewind() override
			{
				reader.set_frame(0);
				index = 0;
			}

			long frame_count() const override { return count; }
			double fps() const override { return 0; }

		private:
			t3dtv::yuv_reader reader;
			const int width, height;
			long count = 0, index = 0;
		};

		//! Anything libavformat can open: video files, single images, image sequences.
		class ffmpeg_decoder : public frame_decoder
		{
		public:
			ffmpeg_decoder(const std::string &file, int w, int h) : width(w), height(h), filename(file)
			{
				try
				{
					open();
				}
				catch (...)
				{
					close();
					throw;
				}
			}

			~ffmpeg_decoder() override { close(); }

			bool decode_next(uint8_t *dst) override
			{
				while (true)
				{
					int r = avcodec_receive_frame(dec, frame);
					if (r == 0)
					{
						convert(dst);
						av_frame_unref(frame);
						return true;
					}
					if (r == AVERROR_EOF) return false;
					if (r != AVERROR(EAGAIN)) fail("decoding error", r);

					if (flushing) return false;
					r = av_read_frame(fmt, pkt);
					if (r < 0)
					{
						// end of file: drain the frames still buffered in the decoder
						flushing = true;
						avcodec_send_packet(dec, nullptr);
						continue;
					}
					if (pkt->stream_index == stream)
					{
						r = avcodec_send_packet(dec, pkt);
						if (r < 0 && r != AVERROR_INVALIDDATA) fail("cannot send packet", r);
					}
					av_packet_unref(pkt);
				}
			}

			void rewind() override
			{
				const int r = av_seek_frame(fmt, stream, start_ts, AVSEEK_FLAG_BACKWARD);
				if (r < 0) fail("cannot seek to the beginning", r);
				avcodec_flush_buffers(dec);
				flushing = false;
			}

			long frame_count() const override { return count; }
			double fps() const override { return rate; }

		private:
			void open()
			{
				int r = avformat_open_input(&fmt, filename.c_str(), nullptr, nullptr);
				if (r < 0) fail("cannot open", r);
				r = avformat_find_stream_info(fmt, nullptr);
				if (r < 0) fail("cannot read stream info", r);

				const AVCodec *codec = nullptr;
				stream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
				if (stream < 0) fail("no video stream", stream);

				dec = avcodec_alloc_context3(codec);
				if (!dec) throw std::runtime_error("avcodec_alloc_context3 failed");
				avcodec_parameters_to_context(dec, fmt->streams[stream]->codecpar);
				dec->thread_count = 0; // let FFmpeg pick
				r = avcodec_open2(dec, codec, nullptr);
				if (r < 0) fail("cannot open decoder", r);

				frame = av_frame_alloc();
				pkt = av_packet_alloc();

				const AVStream *st = fmt->streams[stream];
				const AVRational fr = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
				rate = fr.den ? av_q2d(fr) : 0;
				count = long(st->nb_frames);
				if (count <= 0 && fmt->duration > 0 && rate > 0)
				{
					count = std::lround(double(fmt->duration) / AV_TIME_BASE * rate);
				}
				start_ts = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
			}

			void close()
			{
				if (sws) sws_freeContext(sws);
				if (pkt) av_packet_free(&pkt);
				if (frame) av_frame_free(&frame);
				if (dec) avcodec_free_context(&dec);
				if (fmt) avformat_close_input(&fmt);
				sws = nullptr;
			}

			void convert(uint8_t *dst)
			{
				sws = sws_getCachedContext(sws, frame->width, frame->height, AVPixelFormat(frame->format),
				                           width, height, AV_PIX_FMT_BGR24, SWS_BILINEAR, nullptr, nullptr, nullptr);
				if (!sws) throw std::runtime_error("sws_getCachedContext failed for " + filename);
				uint8_t *dst_planes[4] = {dst, nullptr, nullptr, nullptr};
				int dst_lines[4] = {width * 3, 0, 0, 0};
				sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst_planes, dst_lines);
			}

			[[noreturn]] void fail(const char *what, int code) const
			{
				throw std::runtime_error(filename + ": " + what + " (" + av_error(code) + ")");
			}

			const int width, height;
			const std::string filename;
			AVFormatContext *fmt = nullptr;
			AVCodecContext *dec = nullptr;
			AVFrame *frame = nullptr;
			AVPacket *pkt = nullptr;
			SwsContext *sws = nullptr;
			int stream = -1;
			int64_t start_ts = 0;
			bool flushing = false;
			long count = 0;
			double rate = 0;
		};
	}

	std::unique_ptr<frame_decoder> open_decoder(const std::string &file, int width, int height)
	{
		if (ends_with(file, ".yuv"))
		{
			return std::make_unique<yuv_decoder>(file, width, height);
		}
		return std::make_unique<ffmpeg_decoder>(file, width, height);
	}

	//////////////////////////////////////////////////////////////////////////

	undistort_map::undistort_map(const config::camera_params &cam, int w, int h)
		: width(w), height(h), offset(size_t(w) * h), wx(size_t(w) * h), wy(size_t(w) * h)
	{
		const float fx = cam.K[0][0], fy = cam.K[1][1], skew = cam.K[1][0];
		const float cx = cam.K[2][0], cy = cam.K[2][1];
		const float k1 = cam.dist[0], k2 = cam.dist[1], p1 = cam.dist[2], p2 = cam.dist[3], k3 = cam.dist[4];

		for (int y = 0; y < h; y++)
		{
			for (int x = 0; x < w; x++)
			{
				// undistorted pixel -> normalized coordinates -> distorted pixel (OpenCV model)
				const float yn = (y - cy) / fy;
				const float xn = (x - cx - skew * yn) / fx;
				const float r2 = xn * xn + yn * yn;
				const float radial = 1 + r2 * (k1 + r2 * (k2 + r2 * k3));
				const float xd = xn * radial + 2 * p1 * xn * yn + p2 * (r2 + 2 * xn * xn);
				const float yd = yn * radial + p1 * (r2 + 2 * yn * yn) + 2 * p2 * xn * yn;
				const float sx = fx * xd + skew * yd + cx;
				const float sy = fy * yd + cy;

				const size_t i = size_t(y) * w + x;
				if (sx < 0 || sy < 0 || sx > w - 1 || sy > h - 1)
				{
					offset[i] = -1;
					continue;
				}
				const int x0 = std::min(int(sx), w - 2);
				const int y0 = std::min(int(sy), h - 2);
				offset[i] = (y0 * w + x0) * 3;
				wx[i] = uint8_t(std::lround(std::min((sx - x0) * 256.f, 255.f)));
				wy[i] = uint8_t(std::lround(std::min((sy - y0) * 256.f, 255.f)));
			}
		}
	}

	void undistort_map::apply(const uint8_t *src, uint8_t *dst) const
	{
		const size_t n = size_t(width) * height;
		const int row = width * 3;
		for (size_t i = 0; i < n; i++, dst += 3)
		{
			const int32_t o = offset[i];
			if (o < 0)
			{
				dst[0] = dst[1] = dst[2] = 0;
				continue;
			}
			const uint32_t ax = wx[i], ay = wy[i];
			const uint8_t *p = src + o;
			for (int c = 0; c < 3; c++)
			{
				const uint32_t top = p[c] * (256 - ax) + p[c + 3] * ax;
				const uint32_t bottom = p[c + row] * (256 - ax) + p[c + row + 3] * ax;
				dst[c] = uint8_t((top * (256 - ay) + bottom * ay + 32768) >> 16);
			}
		}
	}

	//////////////////////////////////////////////////////////////////////////

	buffered_source::buffered_source(std::unique_ptr<frame_decoder> dec, int width, int height,
	                                 std::unique_ptr<undistort_map> undist, long loop, size_t cap)
		: decoder(std::move(dec)), undistort(std::move(undist)), frame_bytes(size_t(width) * height * 3),
		  loop_length(loop), capacity(std::max<size_t>(cap, 1))
	{
		scratch.resize(frame_bytes);
		current_frame.resize(frame_bytes);
		if (!decode_one(current_frame))
		{
			throw std::runtime_error("Input contains no frames");
		}
		current_serial = 1;

		// A source with a single frame (still image, 1-frame YUV) is decoded once, no thread needed.
		still = decoder->frame_count() == 1 || loop_length == 1;
		if (!still)
		{
			std::vector<uint8_t> second(frame_bytes);
			still = !decode_one(second);
			if (!still) ready.push_back(std::move(second));
		}
		if (still)
		{
			decoder.reset();
			return;
		}
		worker = std::thread(&buffered_source::run, this);
	}

	buffered_source::~buffered_source()
	{
		{
			std::lock_guard<std::mutex> lock(mutex);
			stopping = true;
		}
		cv.notify_all();
		if (worker.joinable()) worker.join();
	}

	bool buffered_source::decode_one(std::vector<uint8_t> &dst)
	{
		if (loop_length > 0 && decoded_in_loop >= loop_length)
		{
			decoder->rewind();
			decoded_in_loop = 0;
		}
		uint8_t *target = undistort ? scratch.data() : dst.data();
		if (!decoder->decode_next(target))
		{
			if (decoded_in_loop == 0) return false; // empty stream
			if (loop_length > 0 && !warned)
			{
				printf("Warning: stream ended after %ld frames, %ld expected - views may get out of sync\n",
				       decoded_in_loop, loop_length);
				warned = true;
			}
			decoder->rewind();
			decoded_in_loop = 0;
			if (!decoder->decode_next(target)) return false;
		}
		decoded_in_loop++;
		if (undistort) undistort->apply(scratch.data(), dst.data());
		return true;
	}

	void buffered_source::run()
	{
		try
		{
			while (true)
			{
				std::vector<uint8_t> buffer;
				{
					std::unique_lock<std::mutex> lock(mutex);
					cv.wait(lock, [this] { return stopping || ready.size() < capacity; });
					if (stopping) return;
					if (!spare.empty())
					{
						buffer = std::move(spare.back());
						spare.pop_back();
					}
				}
				buffer.resize(frame_bytes);
				if (!decode_one(buffer))
				{
					throw std::runtime_error("Decoding failed after rewind");
				}
				{
					std::lock_guard<std::mutex> lock(mutex);
					ready.push_back(std::move(buffer));
				}
				cv.notify_all();
			}
		}
		catch (...)
		{
			std::lock_guard<std::mutex> lock(mutex);
			error = std::current_exception();
			cv.notify_all();
		}
	}

	void buffered_source::advance()
	{
		if (still) return;
		std::unique_lock<std::mutex> lock(mutex);
		cv.wait(lock, [this] { return !ready.empty() || error; });
		if (ready.empty())
		{
			std::rethrow_exception(error);
		}
		spare.push_back(std::move(current_frame));
		current_frame = std::move(ready.front());
		ready.pop_front();
		lock.unlock();
		cv.notify_all();
		current_serial++;
	}
};
