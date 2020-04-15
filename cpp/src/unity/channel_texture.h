#pragma once

#include <memory>
#include <d3d11.h>
#include "kh_yuv.h"

namespace kh
{
// A class for a single channel of Kinect color pixels.
// Three (Y, U, and V) ChannelTextures are required to represent the color pixels.
class ChannelTexture
{
public:
	ChannelTexture(ID3D11Device* device, int width, int height);
	~ChannelTexture();
	int width() { return width_; }
	int height() { return height_; }
	ID3D11ShaderResourceView* getTextureView(ID3D11Device* device);
	void updatePixels(ID3D11Device* device,
					  ID3D11DeviceContext* device_context,
					  int width,
				 	  int height,
					  uint8_t* frame_data,
					  int frame_linesize);

private:
	int width_;
	int height_;
	ID3D11Texture2D* texture_;
};
}