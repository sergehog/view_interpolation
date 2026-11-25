#include "frame_source.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>

namespace vi {
namespace {
std::string avError(int code) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
  av_strerror(code, buf, sizeof(buf));
  return buf;
}

bool endsWith(const std::string& s, const std::string& suffix) {
  if (s.size() < suffix.size()) return false;
  return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(),
                    [](char a, char b) { return std::tolower(a) == std::tolower(b); });
}

/// Anything libavformat can open: video files, single images, image sequences, and raw
/// planar YUV 4:2:0 files (*.yuv, read with the rawvideo demuxer at the processing resolution).
class FfmpegDecoder : public FrameDecoder {
 public:
  FfmpegDecoder(const std::string& file, int w, int h, bool raw_yuv)
      : width_(w), height_(h), filename_(file), raw_(raw_yuv) {
    try {
      open();
    } catch (...) {
      close();
      throw;
    }
  }

  ~FfmpegDecoder() override { close(); }

  bool decodeNext(uint8_t* dst) override {
    while (true) {
      int r = avcodec_receive_frame(dec_, frame_);
      if (r == 0) {
        convert(dst);
        av_frame_unref(frame_);
        return true;
      }
      if (r == AVERROR_EOF) return false;
      if (r != AVERROR(EAGAIN)) fail("decoding error", r);

      if (flushing_) return false;
      r = av_read_frame(fmt_, pkt_);
      if (r < 0) {
        // end of file: drain the frames still buffered in the decoder
        flushing_ = true;
        avcodec_send_packet(dec_, nullptr);
        continue;
      }
      if (pkt_->stream_index == stream_) {
        r = avcodec_send_packet(dec_, pkt_);
        if (r < 0 && r != AVERROR_INVALIDDATA) fail("cannot send packet", r);
      }
      av_packet_unref(pkt_);
    }
  }

  void rewind() override {
    const int r = av_seek_frame(fmt_, stream_, start_ts_, AVSEEK_FLAG_BACKWARD);
    if (r < 0) fail("cannot seek to the beginning", r);
    avcodec_flush_buffers(dec_);
    flushing_ = false;
  }

  long frameCount() const override { return count_; }
  double fps() const override { return rate_; }

 private:
  void open() {
    const AVInputFormat* input_format = nullptr;
    AVDictionary* options = nullptr;
    if (raw_) {
      // Headerless YUV: limited-range BT.601 like other YUV sources (verified on the Georgiev data
      // against the original renders: 39.9 dB, vs 26.5 dB for the old full-range raw_frame::yuv2rgb).
      input_format = av_find_input_format("rawvideo");
      const std::string size = std::to_string(width_) + "x" + std::to_string(height_);
      av_dict_set(&options, "video_size", size.c_str(), 0);
      av_dict_set(&options, "pixel_format", "yuv420p", 0);
    }
    int r = avformat_open_input(&fmt_, filename_.c_str(), input_format, &options);
    av_dict_free(&options);
    if (r < 0) fail("cannot open", r);
    r = avformat_find_stream_info(fmt_, nullptr);
    if (r < 0) fail("cannot read stream info", r);

    const AVCodec* codec = nullptr;
    stream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (stream_ < 0) fail("no video stream", stream_);

    dec_ = avcodec_alloc_context3(codec);
    if (!dec_) throw std::runtime_error("avcodec_alloc_context3 failed");
    avcodec_parameters_to_context(dec_, fmt_->streams[stream_]->codecpar);
    dec_->thread_count = 0;  // let FFmpeg pick
    r = avcodec_open2(dec_, codec, nullptr);
    if (r < 0) fail("cannot open decoder", r);

    frame_ = av_frame_alloc();
    pkt_ = av_packet_alloc();

    const AVStream* st = fmt_->streams[stream_];
    const AVRational fr = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
    rate_ = fr.den ? av_q2d(fr) : 0;
    count_ = long(st->nb_frames);
    if (count_ <= 0 && fmt_->duration > 0 && rate_ > 0) {
      count_ = std::lround(double(fmt_->duration) / AV_TIME_BASE * rate_);
    }
    start_ts_ = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
  }

  void close() {
    if (sws_) sws_freeContext(sws_);
    if (pkt_) av_packet_free(&pkt_);
    if (frame_) av_frame_free(&frame_);
    if (dec_) avcodec_free_context(&dec_);
    if (fmt_) avformat_close_input(&fmt_);
    sws_ = nullptr;
  }

  void convert(uint8_t* dst) {
    sws_ = sws_getCachedContext(sws_, frame_->width, frame_->height, AVPixelFormat(frame_->format), width_, height_,
                                AV_PIX_FMT_BGR24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) throw std::runtime_error("sws_getCachedContext failed for " + filename_);
    uint8_t* dst_planes[4] = {dst, nullptr, nullptr, nullptr};
    int dst_lines[4] = {width_ * 3, 0, 0, 0};
    sws_scale(sws_, frame_->data, frame_->linesize, 0, frame_->height, dst_planes, dst_lines);
  }

  [[noreturn]] void fail(const char* what, int code) const {
    throw std::runtime_error(filename_ + ": " + what + " (" + avError(code) + ")");
  }

  const int width_, height_;
  const std::string filename_;
  const bool raw_;
  AVFormatContext* fmt_ = nullptr;
  AVCodecContext* dec_ = nullptr;
  AVFrame* frame_ = nullptr;
  AVPacket* pkt_ = nullptr;
  SwsContext* sws_ = nullptr;
  int stream_ = -1;
  int64_t start_ts_ = 0;
  bool flushing_ = false;
  long count_ = 0;
  double rate_ = 0;
};
}  // namespace

std::unique_ptr<FrameDecoder> openDecoder(const std::string& file, int width, int height) {
  av_log_set_level(AV_LOG_ERROR);  // FFmpeg's informational messages are noise here
  return std::make_unique<FfmpegDecoder>(file, width, height, endsWith(file, ".yuv"));
}

//////////////////////////////////////////////////////////////////////////

UndistortMap::UndistortMap(const Camera& cam, int w, int h)
    : width_(w), height_(h), offset_(size_t(w) * h), wx_(size_t(w) * h), wy_(size_t(w) * h) {
  const float fx = cam.intrinsics[0][0], fy = cam.intrinsics[1][1], skew = cam.intrinsics[1][0];
  const float cx = cam.intrinsics[2][0], cy = cam.intrinsics[2][1];
  const float k1 = cam.distortion[0], k2 = cam.distortion[1], p1 = cam.distortion[2], p2 = cam.distortion[3],
              k3 = cam.distortion[4];

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
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
      if (sx < 0 || sy < 0 || sx > w - 1 || sy > h - 1) {
        offset_[i] = -1;
        continue;
      }
      const int x0 = std::min(int(sx), w - 2);
      const int y0 = std::min(int(sy), h - 2);
      offset_[i] = (y0 * w + x0) * 3;
      wx_[i] = uint8_t(std::lround(std::min((sx - x0) * 256.f, 255.f)));
      wy_[i] = uint8_t(std::lround(std::min((sy - y0) * 256.f, 255.f)));
    }
  }
}

void UndistortMap::apply(const uint8_t* src, uint8_t* dst) const {
  const size_t n = size_t(width_) * height_;
  const int row = width_ * 3;
  for (size_t i = 0; i < n; i++, dst += 3) {
    const int32_t o = offset_[i];
    if (o < 0) {
      dst[0] = dst[1] = dst[2] = 0;
      continue;
    }
    const uint32_t ax = wx_[i], ay = wy_[i];
    const uint8_t* p = src + o;
    for (int c = 0; c < 3; c++) {
      const uint32_t top = p[c] * (256 - ax) + p[c + 3] * ax;
      const uint32_t bottom = p[c + row] * (256 - ax) + p[c + row + 3] * ax;
      dst[c] = uint8_t((top * (256 - ay) + bottom * ay + 32768) >> 16);
    }
  }
}

//////////////////////////////////////////////////////////////////////////

BufferedSource::BufferedSource(std::unique_ptr<FrameDecoder> dec, int width, int height,
                               std::unique_ptr<UndistortMap> undist, long loop, size_t cap)
    : decoder_(std::move(dec)),
      undistort_(std::move(undist)),
      frame_bytes_(size_t(width) * height * 3),
      loop_length_(loop),
      capacity_(std::max<size_t>(cap, 1)) {
  scratch_.resize(frame_bytes_);
  current_frame_.resize(frame_bytes_);
  if (!decodeOne(current_frame_)) {
    throw std::runtime_error("Input contains no frames");
  }
  current_serial_ = 1;

  // A source with a single frame (still image, 1-frame YUV) is decoded once, no thread needed.
  still_ = decoder_->frameCount() == 1 || loop_length_ == 1;
  if (!still_) {
    std::vector<uint8_t> second(frame_bytes_);
    still_ = !decodeOne(second);
    if (!still_) ready_.push_back(std::move(second));
  }
  if (still_) {
    decoder_.reset();
    return;
  }
  worker_ = std::thread(&BufferedSource::run, this);
}

BufferedSource::~BufferedSource() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

bool BufferedSource::decodeOne(std::vector<uint8_t>& dst) {
  if (loop_length_ > 0 && decoded_in_loop_ >= loop_length_) {
    decoder_->rewind();
    decoded_in_loop_ = 0;
  }
  uint8_t* target = undistort_ ? scratch_.data() : dst.data();
  if (!decoder_->decodeNext(target)) {
    if (decoded_in_loop_ == 0) return false;  // empty stream
    if (loop_length_ > 0 && !warned_) {
      printf("Warning: stream ended after %ld frames, %ld expected - views may get out of sync\n", decoded_in_loop_,
             loop_length_);
      warned_ = true;
    }
    decoder_->rewind();
    decoded_in_loop_ = 0;
    if (!decoder_->decodeNext(target)) return false;
  }
  decoded_in_loop_++;
  if (undistort_) undistort_->apply(scratch_.data(), dst.data());
  return true;
}

void BufferedSource::run() {
  try {
    while (true) {
      std::vector<uint8_t> buffer;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return stopping_ || ready_.size() < capacity_; });
        if (stopping_) return;
        if (!spare_.empty()) {
          buffer = std::move(spare_.back());
          spare_.pop_back();
        }
      }
      buffer.resize(frame_bytes_);
      if (!decodeOne(buffer)) {
        throw std::runtime_error("Decoding failed after rewind");
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        ready_.push_back(std::move(buffer));
      }
      cv_.notify_all();
    }
  } catch (...) {
    std::lock_guard<std::mutex> lock(mutex_);
    error_ = std::current_exception();
    cv_.notify_all();
  }
}

void BufferedSource::advance() {
  if (still_) return;
  std::unique_lock<std::mutex> lock(mutex_);
  cv_.wait(lock, [this] { return !ready_.empty() || error_; });
  if (ready_.empty()) {
    std::rethrow_exception(error_);
  }
  spare_.push_back(std::move(current_frame_));
  current_frame_ = std::move(ready_.front());
  ready_.pop_front();
  lock.unlock();
  cv_.notify_all();
  current_serial_++;
}

}  // namespace vi
