/** RAW image frame class - header 
 * @file raw_frame.h
 * @author Sergey Smirnov <sergey.smirnov@tut.fi>
 * @date 29.10.2010
 * @copyright Tampere University of Technology, 2010
*/
#pragma once 
#include <string.h>
#include <iostream>
#include <fstream>

namespace t3dtv
{
//#define byte unsigned char
typedef unsigned char byte;

#define INDEX(x,y,width) ((x)+(y)*(width))

enum frame_format
{
	f400 = 0,
	f420 = 1,
	f422x = 2,
	f422y = 3,
	f444 = 4
};

class raw_frame
{
public:	

	// all pointers are made to be const!
	// They work as get-only properties
	union
	{
		// 2 aliases on frame data
		struct
		{
			byte* R;
			byte* G;
			byte* B;
		};
		struct
		{
			byte* Y;
			byte* U;
			byte* V;
		};
	};
	
	const int lumasize;
	const int chromasize;
	const int framesize;
	const int height;
	const int width;	
	const bool is420;
	byte * const frame;
	

	//! public constructor (By default - 420 format)
	// ToDo: to be removed to .cpp file!
	inline raw_frame(int w, int h, bool format420 = true) 
		: width(w), height(h), is420(format420), lumasize(h*w)
		, chromasize(get_chromasize(w,h,format420))
		, framesize(lumasize+2*chromasize)
		, frame(new byte[framesize])
		//, R(frame), G(R + lumasize), B(G + chromasize)
		//, Y(frame), U(R + lumasize), V(G + chromasize)
	{
		//int chromaheight = (format420) ? h/2 : h;
		//int chromawidth = (format420) ? w/2 : w;
		//chromasize = chromaheight*chromawidth;
		//wholesize = lumasize + 2*chromasize;
		////frame = new byte[wholesize];
		R = frame;
		G = R + lumasize;
		B = G + chromasize;
	};


	int static get_chromasize(int w, int h, bool format420)
	{
		int chromaheight = (format420) ? h/2 : h;
		int chromawidth = (format420) ? w/2 : w;
		return chromaheight*chromawidth;		
	}

	inline ~raw_frame()
	{
		delete[] frame;
	};


	inline int index(int x, int y)
	{
		return x + y*width;
	}

	bool convert_444(raw_frame *frm)
	{
		if(frm->is420)
			return false;

		if(!is420)
		{
			memcpy(frm->frame, frame, framesize);
		}
		else
		{
			memcpy(frm->frame, frame, lumasize); // copy luma channel
			byte* LU = (byte*)frm->U;
			byte* LV = (byte*)frm->V;
			for(int y=0, indexL = 0, indexC = 0; y<height; y+=2, indexL+=width)
			{
				for(int x=0; x<width; x+=2, indexL+=2, indexC++)
				{
					LU[indexL] = LU[indexL+1] = LU[indexL+width] = LU[indexL+1+width] = U[indexC];
					LV[indexL] = LV[indexL+1] = LV[indexL+width] = LV[indexL+1+width] = V[indexC];
				}
			}
		}

		return true;
	};

	bool yuv2rgb(raw_frame *frm)
	{
		if(frm->is420 || is420 || framesize != frm->framesize)
			return false;
		#pragma omp parallel for
		for(int i=0; i<lumasize; i++)
		{
			frm->R[i] = static_cast<byte>((int)Y[i] + 1.772*((int)U[i]-127) + 0.5);
			frm->G[i] = static_cast<byte>(Y[i] - 0.344*((int)U[i]-127) - 0.714*((int)V[i]-127) + 0.5);
			frm->B[i] = static_cast<byte>(Y[i] + 1.402*((int)V[i]-127) + 0.5);
		}		

		return true;
	};

	bool read_frame(std::ifstream& stream)
	{
		if(stream.bad() || stream.eof())
			return false;
		
		stream.read((char*)frame, framesize);
		
		return !stream.bad();
	}

	bool write_frame(std::ofstream& stream)
	{
		if(stream.bad())
			return false;
		
		stream.write((char*)frame, framesize);
		stream.flush();

		return !stream.bad();
	}


};


class raw_frame_utility
{
public:
	int const static factor = 2;

	void static downsample(raw_frame *in, raw_frame* out)
	{
		//#pragma omp parallel for
		for(int x=0; x<out->width; x++)		
		{
			for(int y=0; y<out->height; y++)			
			{
				//cycle over square in original image
				double tmpR = 0;
				double tmpG = 0;
				double tmpB = 0;
				for(int xx=x*factor; xx<(x+1)*factor; xx++)
				{
					for(int yy=y*factor; yy<(y+1)*factor; yy++)	
					{
						tmpR += in->R[in->index(xx,yy)];
						tmpG += in->G[in->index(xx,yy)];
						tmpB += in->B[in->index(xx,yy)];
					}
				}
				tmpR /= factor*factor;
				tmpG /= factor*factor;
				tmpB /= factor*factor;

				out->R[out->index(x,y)] = floor(0.5 + tmpR);
				out->G[out->index(x,y)] = floor(0.5 + tmpG);
				out->B[out->index(x,y)] = floor(0.5 + tmpB);
			}
		}

		
	}

};

};