#ifndef PROTOCOL_H
#define PROTOCOL_H

#pragma pack(push)
#pragma pack(1)
struct remote_header
{
	std::uint32_t pkt_id=~0;
	std::uint32_t seq_id=0;
};

struct remote_mode_header : remote_header
{
	remote_mode_header()
	{
		pkt_id=0;
	}

	std::uint32_t width=0;
	std::uint32_t height=0;
	std::uint32_t pitch=0;
	std::uint32_t bpp=0;
	double aspect_ratio=4/3.;

	bool operator==(const remote_mode_header &right) const
	{
		return
			pkt_id==right.pkt_id &&
			width==right.width &&
			height==right.height &&
			pitch==right.pitch &&
			bpp==right.bpp &&
			aspect_ratio==right.aspect_ratio;
	}

	bool operator!=(const remote_mode_header &right) const
	{
		return !operator==(right);
	}
};

struct remote_chunk_header : remote_header
{
	remote_chunk_header()
	{
		pkt_id=1;
	}

	std::uint32_t frame_id=0;
	std::uint32_t frame_chunks=0;
	std::uint32_t chunk_id=0;
	std::uint32_t x=0;
	std::uint32_t y=0;
	std::uint32_t width=0;
	std::uint32_t height=0;
	std::uint32_t pitch=0;
	std::uint32_t bpp=0;
};

struct remote_vsync_header : remote_header
{
	remote_vsync_header()
	{
		pkt_id=2;
	}
};
#pragma pack(pop)

inline int calc_pitch(int width, int bpp)
{
	return (width*bpp+7)/8;
}

inline std::tuple<int, int, int, int> get_chunk(int width, int height, int w_div, int h_div, int row, int col)
{
	return std::make_tuple(
			(height*row)/h_div, (width*col)/w_div,
			(height*(row+1))/h_div, (width*(col+1))/w_div);
}

inline int int_div_rup(int num, int div)
{
	return (num+div-1)/div;
}

inline std::tuple<int, int> get_frame_divisions(int width, int height, int bpp, int max_bytes=1400)
{
	auto pitch=calc_pitch(width, bpp);
	auto total_bytes=pitch*height;
	auto max_pixels=(max_bytes*8)/bpp;
	auto min_packets_needed=int_div_rup(total_bytes, max_bytes);
	auto divs=static_cast<int>(ceil(sqrt(min_packets_needed)));
	auto w_divs=divs;
	auto h_divs=divs;
	
	w_divs=int_div_rup(width*int_div_rup(height, h_divs), max_pixels);

	return std::make_tuple(w_divs, h_divs);
}

#endif /* PROTOCOL_H */
