#define BOOST_TEST_MODULE netvid
#include <boost/test/included/unit_test.hpp>

#include "framebuffer.h"
#include "protocol.h"

#define BOOST_TEST_INFO_VAR(var) \
	BOOST_TEST_INFO("With parameter " #var " = " << (var))

BOOST_AUTO_TEST_CASE(palette_check_a1r5g5b5)
{
	auto get_color=[] (std::uint16_t col)
	{
		return to_float_srgb(fmt_a1r5g5b5, col);
	};

	typedef std::array<float, 3> s;

	BOOST_TEST(get_color(0b000000000000000)==s({ 0, 0, 0 }));
	BOOST_TEST(get_color(0b000000000011111)==s({ 0, 0, 1 }));
	BOOST_TEST(get_color(0b000001111100000)==s({ 0, 1, 0 }));
	BOOST_TEST(get_color(0b000001111111111)==s({ 0, 1, 1 }));
	BOOST_TEST(get_color(0b111110000000000)==s({ 1, 0, 0 }));
	BOOST_TEST(get_color(0b111110000011111)==s({ 1, 0, 1 }));
	BOOST_TEST(get_color(0b111111111100000)==s({ 1, 1, 0 }));
	BOOST_TEST(get_color(0b111111111111111)==s({ 1, 1, 1 }));
}

BOOST_AUTO_TEST_CASE(palette_check_r5g6b5)
{
	auto get_color=[] (std::uint16_t col)
	{
		return to_float_srgb(fmt_r5g6b5, col);
	};

	typedef std::array<float, 3> s;

	BOOST_TEST(get_color(0b0000000000000000)==s({ 0, 0, 0 }));
	BOOST_TEST(get_color(0b0000000000011111)==s({ 0, 0, 1 }));
	BOOST_TEST(get_color(0b0000011111100000)==s({ 0, 1, 0 }));
	BOOST_TEST(get_color(0b0000011111111111)==s({ 0, 1, 1 }));
	BOOST_TEST(get_color(0b1111100000000000)==s({ 1, 0, 0 }));
	BOOST_TEST(get_color(0b1111100000011111)==s({ 1, 0, 1 }));
	BOOST_TEST(get_color(0b1111111111100000)==s({ 1, 1, 0 }));
	BOOST_TEST(get_color(0b1111111111111111)==s({ 1, 1, 1 }));

	BOOST_TEST(from_float_srgb(fmt_r5g6b5, s({ 1, 1, 1 }))==0b1111111111111111);
	BOOST_TEST(from_float_srgb(fmt_r5g6b5, to_srgb(s({ 1, 1, 1 })))==0b1111111111111111);
}

BOOST_AUTO_TEST_CASE(srgb_linear)
{
	typedef std::array<float, 3> s;

	BOOST_TEST(to_srgb(s({ 1, 1, 1 }))>=s({ 1-1e-6, 1-1e-6, 1-1e-6 }));
	BOOST_TEST(to_srgb(s({ 1, 1, 1 }))<=s({ 1+1e-6, 1+1e-6, 1+1e-6 }));
	BOOST_TEST(to_linear(s({ 1, 1, 1 }))==s({ 1, 1, 1 }));

	BOOST_TEST(to_srgb(s({ 0, 0, 0 }))==s({ 0, 0, 0 }));
	BOOST_TEST(to_linear(s({ 0, 0, 0 }))==s({ 0, 0, 0 }));
}

BOOST_AUTO_TEST_CASE(frame_div)
{
	const int width=640;
	const int height=480;
	const int bpp=16;
	const int data_size=1400;
	const int pitch=calc_pitch(width, bpp);

	BOOST_TEST(pitch==1280);

	int w_div=0;
	int h_div=0;

	std::tie(w_div, h_div)=get_frame_divisions(width, height, bpp, data_size);

	BOOST_TEST(w_div>0);
	BOOST_TEST(w_div<=width);
	BOOST_TEST(h_div>0);
	BOOST_TEST(h_div<=height);
	
	for (int h=0; h<h_div; ++h)
	{
		BOOST_TEST_CONTEXT("With parameter h=" << h)
		{
			for (int w=0; w<w_div; ++w)
			{
				BOOST_TEST_CONTEXT("With parameter w=" << w)
				{
					int w_begin=0;
					int w_end=0;
					int h_begin=0;
					int h_end=0;

					std::tie(h_begin, w_begin, h_end, w_end)=get_chunk(width, height, w_div, h_div, h, w);

					BOOST_TEST(w_begin>=0);
					BOOST_TEST(w_end<=width);
					BOOST_TEST(w_end>=w_begin);
					BOOST_TEST(h_begin>=0);
					BOOST_TEST(h_end<=height);
					BOOST_TEST(h_end>=h_begin);

					BOOST_TEST_INFO_VAR(w_begin);
					BOOST_TEST_INFO_VAR(w_end);
					BOOST_TEST_INFO_VAR(h_begin);
					BOOST_TEST_INFO_VAR(h_end);
					BOOST_TEST_INFO_VAR(w_div);
					BOOST_TEST_INFO_VAR(h_div);
					BOOST_TEST(int_div_rup((w_end-w_begin)*bpp, 8)*(h_end-h_begin)<=data_size);
				}
			}
		}
	}
}
