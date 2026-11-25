// Input frame sources: FFmpeg decoding, lens undistortion and background buffering.
//
// All frames handed to the renderer are BGR24, interleaved, top row first, at the processing resolution
// (BGR because the shaders sample the input textures with a `.bgr` swizzle).
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "camera.h"

namespace vi {

/// Sequential frame decoder producing BGR24 frames of a fixed size.
class FrameDecoder {
 public:
  virtual ~FrameDecoder() = default;
  /// Decodes the next frame into dst (width*height*3 bytes). Returns false at end of stream.
  virtual bool decodeNext(uint8_t* dst) = 0;
  /// Restarts decoding from the first frame.
  virtual void rewind() = 0;
  /// Number of frames, or 0 if unknown.
  virtual long frameCount() const = 0;
  /// Native frame rate, or 0 if unknown.
  virtual double fps() const = 0;
};

/// Opens a decoder (FFmpeg). *.yuv files are raw planar YUV420 of exactly width x height;
/// anything else (videos, image files, image sequences like "img_%04d.png") is scaled to width x height.
std::unique_ptr<FrameDecoder> openDecoder(const std::string& file, int width, int height);

/// Precomputed inverse lens-distortion lookup (OpenCV model), bilinear resampling.
class UndistortMap {
 public:
  /// cam must already be scaled to width x height.
  UndistortMap(const Camera& cam, int width, int height);
  void apply(const uint8_t* src, uint8_t* dst) const;

 private:
  int width_, height_;
  std::vector<int32_t> offset_;   // index of the top-left source pixel, -1 if outside the image
  std::vector<uint8_t> wx_, wy_;  // bilinear weights in 1/256
};

/// A decoder running in its own thread, a few frames ahead of the renderer.
/// Sources with a single frame (still images) are decoded once and need no thread.
class BufferedSource {
 public:
  /// loop_length > 0: rewind after that many frames (keeps several videos in sync); 0: rewind at end of stream.
  BufferedSource(std::unique_ptr<FrameDecoder> decoder, int width, int height, std::unique_ptr<UndistortMap> undistort,
                 long loop_length, size_t capacity = 4);
  ~BufferedSource();

  BufferedSource(const BufferedSource&) = delete;
  BufferedSource& operator=(const BufferedSource&) = delete;

  /// Advances to the next frame (blocks until it is decoded). No-op for still images.
  void advance();
  /// Current frame (BGR24).
  const uint8_t* current() const { return current_frame_.data(); }
  /// Increments whenever current() changes.
  unsigned long serial() const { return current_serial_; }
  bool isStill() const { return still_; }

 private:
  void run();
  bool decodeOne(std::vector<uint8_t>& dst);  // false if the stream has no frames at all

  std::unique_ptr<FrameDecoder> decoder_;
  std::unique_ptr<UndistortMap> undistort_;
  const size_t frame_bytes_;
  const long loop_length_;
  const size_t capacity_;
  bool still_ = false;
  long decoded_in_loop_ = 0;
  bool warned_ = false;

  std::vector<uint8_t> scratch_;        // decoder output before undistortion (worker thread only)
  std::vector<uint8_t> current_frame_;  // owned by the render thread
  unsigned long current_serial_ = 0;

  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::vector<uint8_t>> ready_;   // decoded frames, oldest first
  std::vector<std::vector<uint8_t>> spare_;  // recycled buffers
  std::exception_ptr error_;
  bool stopping_ = false;
  std::thread worker_;
};

}  // namespace vi
