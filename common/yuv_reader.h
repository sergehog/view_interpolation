/** header for yuv_reader
*	@file yuv_reader.h
*	@author Sergey Smirnov <sergey.smirnov@tut.fi>
*	@date 29.10.2010
*	@copyright Computational Imaging / Transform and Spectral Techniques Group, 
*	Departament of Signal Processing, 
*	Tampere University of Technology, 2010-2011
*	http://sp.cs.tut.fi/groups/trans/
*/

#pragma once 

#ifndef _yuv_reader_h_
#define _yuv_reader_h_

#include <string>
#include <iostream>
#include <fstream>
#include "raw_frame.h"
//#include <complex>

//using namespace std;

namespace t3dtv
{
//! yuv_reader: frame-per-frame reader of yuv files
//! It is not thread-safe! 
//! You cannot use the same instance of class to read in multiple threads. 
class yuv_reader
{
private:
	bool const self_allocated;
	//! frame420 is allocated when convertation to 444 format is required
	//! Otherwise, it's simply an alias to 'frame'
	raw_frame *frame420; 
	
	//! frameYUV is allocated when convertation to RGB is required
	raw_frame *frameYUV;
	bool const convert444;
	bool const convertRGB;
	std::ifstream stream;

public:

	//! main interfacing frame class
	raw_frame * const frame;

	//! Public constructor which allocates memory itself
	// @param convert_in444 set to true: automatically converts to 444 format, false - keeps in 420 format
	// @param convert_inRGB false: keep in YUV color space, true: converts to RGB 
	// @param start_frame	
	yuv_reader (std::string filename, int width, int height, bool convert_to444 = true, bool convert_toRGB = false, int start_frame = 0)
		: convert444(convert_to444), convertRGB(convert_toRGB&&convert_to444)
		, frame(new raw_frame(width, height, !convert_to444))
		, self_allocated(true)
	{
		//frame = new raw_frame(width, height, is420);
		frame420 = (convert444) ? new raw_frame(width, height, true) : (raw_frame *)frame;
		frameYUV = (convertRGB) ? new raw_frame(width, height, false) : (raw_frame *)frame;

		stream.open(filename.c_str(), std::ios::in | std::ios::binary);
		if(start_frame > 0)
		{
			//stream.seekg(start_frame*(frame420->framesize));
			set_frame(start_frame);
		}
	};

	//! Public constructor with use of pre-allocated memory
	// @param convert_in444 set to true: automatically converts to 444 format, false - keeps in 420 format
	// @param convert_inRGB false: keep in YUV color space, true: converts to RGB 
	// @param start_frame	
	yuv_reader (std::string filename, raw_frame* in_frame, bool convert_toRGB = false, int start_frame = 0)
		: convert444(!in_frame->is420), convertRGB(convert_toRGB && (!in_frame->is420))
		, frame(in_frame)
		, self_allocated(false)
	{
		
	}

	bool is_bad()
	{
		return stream.eof() || stream.bad() || stream.fail();
	}

	//! reads single consequetive frame and returns status
	bool const read_frame()
	{
		bool noErr = frame420->read_frame(stream);
		if(convert444 && noErr)
		{
			noErr = frame420->convert_444(frameYUV);
		}
		if(convertRGB && noErr)
		{
			noErr = frameYUV->yuv2rgb((raw_frame*)frame);
		}

		return noErr;
	};

	//! reads single consequetive frame (to an external specified memory)
	//! An experimental thread-safe reading, but not completely thread safe :(
	void read_frame(raw_frame* frame_ext)
	{
		raw_frame *frame420_ext, *frameYUV_ext;

		frame420_ext = frame_ext->is420 ? frame_ext : frame420;
		frameYUV_ext = (!frame_ext->is420) && convertRGB ? frameYUV : frame_ext;
		
		bool noErr = frame420_ext->read_frame(stream);
		if(!frame_ext->is420 && noErr)
		{
			noErr = frame420_ext->convert_444(frameYUV_ext);
		}
		if(!frame_ext->is420 && convertRGB && noErr)
		{
			noErr = frameYUV_ext->yuv2rgb(frame_ext);
		}
		if(!noErr)
		{
			throw std::runtime_error("void yuv_reader::read_frame(raw_frame* frame_ext) Error!");
		}
		//return noErr;
	};
	
	void set_frame(int frame)
	{
		stream.seekg(frame*(frame420->framesize));
	}

	~yuv_reader()
	{
		if(stream.is_open())
		{
			stream.close();
		}
		if(self_allocated && frame)
		{
			delete frame;
		}
		if(convert444)
		{
			delete frame420;
			if(convertRGB)
				delete frameYUV;
		}
	};

};

};

#endif