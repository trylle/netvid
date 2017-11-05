#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <array>
#include <cmath>
#include <memory>

#include <boost/asio/buffer.hpp>
#include <boost/functional/hash.hpp>

struct channel_bits
{
	int start_bit=0;
	int bits=0;

	std::uint32_t mask() const
	{
		return (1 << bits)-1;
	}
};

template<class input_type_t>
struct pixel_format
{
	typedef input_type_t input_type;
	std::array<channel_bits, 3> channels;

	int bits() const
	{
		return sizeof(input_type)*8;
	}

	int visible_bits() const
	{
		int b=0;
 
		for (const auto &c : channels)
			b+=c.bits;
 
		return b;
	}
};

static constexpr pixel_format<std::uint16_t> fmt_a1r5g5b5={ { { { 10, 5 },{ 5, 5 },{ 0, 5 } } } };
static constexpr pixel_format<std::uint16_t> fmt_r5g6b5={ { { { 11, 5 },{ 5, 6 },{ 0, 5 } } } };
static constexpr pixel_format<std::uint32_t> fmt_a8r8g8b8={ { { { 16, 8 },{ 8, 8 },{ 0, 8 } } } };

template<typename fmt_t>
std::array<float, 3> to_float_srgb(const fmt_t &fmt, typename fmt_t::input_type color)
{
	std::array<float, 3> ret;

	for (int i=0; i<3; ++i)
	{
		const auto &c=fmt.channels[i];
		auto mask=c.mask();

		ret[i]=((color >> c.start_bit) & mask)/float(mask);
	}

	return ret;
}

template<typename fmt_t>
typename fmt_t::input_type from_float_srgb(const fmt_t &fmt, const std::array<float, 3> &srgb_color)
{
	typedef typename fmt_t::input_type output_type;
	output_type ret=0;

	for (int i=0; i<3; ++i)
	{
		const auto &c=fmt.channels[i];

		ret|=static_cast<output_type>(srgb_color[i]*c.mask()+.5f) << c.start_bit;
	}

	return ret;
}

// based on https://en.wikipedia.org/wiki/SRGB#The_reverse_transformation
extern std::array<float, 3> to_linear(const std::array<float, 3> &color);

// based on https://en.wikipedia.org/wiki/SRGB#The_forward_transformation_.28CIE_XYZ_to_sRGB.29
extern std::array<float, 3> to_srgb(const std::array<float, 3> &color);

template<class T>
struct pixel_accessor
{
	T *ptr;
	int offset;
	int bits;

	T get() const
	{
		return (*ptr >> offset) & ((1 << bits)-1);
	}

	operator T() const
	{
		return get();
	}

	pixel_accessor &operator=(T val)
	{
		*ptr&=~(((1 << bits)-1) << offset);
		*ptr|=val << offset;
		
		return *this;
	}
};

struct frame_data
{
	std::uint8_t *data=nullptr;
	int width=0;
	int height=0;
	int pitch=0;
	int bpp=0;
	double aspect_ratio=4/3.;

	int bytes() const
	{
		return pitch*height;
	}

	std::uint8_t *end()
	{
		return data+bytes();
	}

	const std::uint8_t *end() const
	{
		return data+bytes();
	}

	boost::asio::const_buffer buffer() const
	{
		return boost::asio::const_buffer(data, bytes());
	}

	template<class T>
	T *pixel(int x, int y)
	{
		return reinterpret_cast<T *>(data+(x*bpp/8)+y*pitch);
	}

	template<class T>
	const T *pixel(int x, int y) const
	{
		return reinterpret_cast<const T *>(data+(x*bpp/8)+y*pitch);
	}

	template<class T>
	pixel_accessor<T> pixel_unaligned(int x, int y)
	{
		int want_bit=x*bpp;
		int actual_bit=want_bit & ~7;

		pixel_accessor<T> pa;

		pa.offset=want_bit-actual_bit;
		pa.bits=bpp;
		pa.ptr=reinterpret_cast<T *>(data+(actual_bit/8)+y*pitch);

		return pa;
	}

	template<class T>
	const pixel_accessor<T> pixel_unaligned(int x, int y) const
	{
		return const_cast<frame_data *>(this)->pixel_unaligned<T>(x, y);
	}

	void clear()
	{
		std::fill(data, end(), 0);
	}

	operator bool() const
	{
		return data!=nullptr;
	}

	bool operator!() const
	{
		return !operator bool();
	}
};

namespace std
{
	template<>
	struct hash<frame_data>
	{
		size_t operator()(const frame_data &fd) const
		{
			size_t seed=0;

			boost::hash_range(seed, static_cast<const std::uint8_t *>(fd.data), fd.end());

			boost::hash_combine(seed, fd.width);
			boost::hash_combine(seed, fd.height);
			boost::hash_combine(seed, fd.pitch);
			boost::hash_combine(seed, fd.bpp);
			boost::hash_combine(seed, fd.aspect_ratio);

			return seed;
		}
	};
}

struct frame_data_managed : frame_data
{
	std::unique_ptr<std::uint8_t[]> data_store;

	frame_data_managed()=default;
	frame_data_managed(frame_data_managed &&)=default;
	~frame_data_managed();

	frame_data_managed &operator=(frame_data_managed &&)=default;

	void free();
	void copy(const frame_data &other);

	bool resize(int width, int height, int pitch, int bpp);
	bool resize(int width, int height, int bpp);
};

static float distance(const std::array<float, 3> &left, const std::array<float, 3> &right)
{
	float sqr_distance=0;

	for (int i=0; i<3; ++i)
	{
		auto d=left[i]-right[i];

		sqr_distance+=d*d;
	}

	return sqrt(sqr_distance);
}

template<bool negate_right=false, std::size_t d>
void add_ref(std::array<float, d> &left, const std::array<float, d> &right)
{
	for (std::size_t i=0; i<d; ++i)
		left[i]+=right[i]*(negate_right ? -1 : 1);
}

template<bool negate_right=false, std::size_t d>
std::array<float, d> add(const std::array<float, d> &left, const std::array<float, d> &right)
{
	std::array<float, d> ret=left;

	add_ref<negate_right>(ret, right);

	return ret;
}

template<std::size_t d>
static inline std::array<float, d> sub(const std::array<float, d> &left, const std::array<float, d> &right)
{
	return add<true, d>(left, right);
}

template<std::size_t d>
static inline void clamp_ref(std::array<float, d> &left)
{
	for (auto &i : left)
		i=std::max(0.f, std::min(1.f, i));
}

template<std::size_t d>
static inline std::array<float, d> clamp(const std::array<float, d> &left)
{
	std::array<float, d> ret=left;

	clamp_ref(ret);

	return ret;
}

template<std::size_t d>
static inline void mul_ref(std::array<float, d> &left, float x)
{
	for (auto &i : left)
		i*=x;
}

template<std::size_t d>
static inline std::array<float, d> mul(const std::array<float, d> &left, float x)
{
	std::array<float, d> ret=left;

	mul_ref(ret, x);

	return ret;
}

template<std::size_t d>
static inline std::array<float, d> lerp(const std::array<float, d> &left, const std::array<float, d> &right, float x)
{
	return add(left, mul(sub(right, left), x));
}

#endif /* FRAMEBUFFER_H */
