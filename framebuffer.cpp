#include "framebuffer.h"

std::array<float, 3> to_linear(const std::array<float, 3> &color)
{
	std::array<float, 3> ret=color;
	static const float a=0.055f;
	static const float cutoff=0.04045f;

	for (auto i=ret.begin(); i!=ret.end(); ++i)
	{
		auto &intensity=*i;

		if (intensity>cutoff)
			intensity=std::pow((intensity+a)/(1+a), 2.4f);
		else
			intensity=intensity/12.92f;
	}

	return ret;
}

std::array<float, 3> to_srgb(const std::array<float, 3> &color)
{
	std::array<float, 3> ret=color;
	static const float a=0.055f;
	static const float cutoff=0.0031308f;

	for (auto i=ret.begin(); i!=ret.end(); ++i)
	{
		auto &intensity=*i;

		if (intensity>cutoff)
			intensity=(1+a)*std::pow(intensity, 1/2.4f)-a;
		else
			intensity=intensity*12.92f;
	}

	return ret;
}

frame_data_managed::~frame_data_managed()
{
	free(); // not strictly necessary
}

bool frame_data_managed::resize(int width, int height, int pitch, int bpp)
{
	auto realloc_needed=(pitch*height)!=bytes();

	frame_data_managed old;

	if (realloc_needed)
		old=std::move(*this);

	this->bpp=bpp;
	this->width=width;
	this->height=height;
	this->pitch=pitch;

	if (!realloc_needed)
		return false;

	if (bytes()==0)
	{
		free();

		return true;
	}

	data_store=std::make_unique<std::uint8_t[]>(bytes());
	data=data_store.get();

	if (old.data_store)
	{
		auto p=std::min(old.pitch, pitch);
		auto h=std::min(old.height, height);

		for (int y=0; y<h; ++y)
		{
			std::copy(old.pixel<std::uint8_t>(0, y), old.pixel<std::uint8_t>(0, y)+p, pixel<std::uint8_t>(0, y));
		}
	}

	return true;
}

bool frame_data_managed::resize(int width, int height, int bpp)
{
	int pitch=(width*bpp+7)/8;

	return resize(width, height, pitch, bpp);
}

void frame_data_managed::free()
{
	data_store.reset();
	data=nullptr;
	width=0;
	height=0;
	pitch=0;
	bpp=0;
}

void frame_data_managed::copy(const frame_data &other)
{
	resize(other.width, other.height, other.pitch, other.bpp);
	std::copy(other.data, other.data+other.bytes(), data_store.get());
	aspect_ratio=other.aspect_ratio;
}
