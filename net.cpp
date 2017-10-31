#include "net.h"

#include <chrono>
#include <iostream>

#include <boost/lexical_cast.hpp>
#include <boost/optional/optional_io.hpp>

using namespace boost;
using namespace boost::asio;
using namespace boost::asio::ip;
using namespace netvid;

socket_wrapper::socket_wrapper(boost::asio::io_service &service)
	: socket(service), strand(service)
{
	socket.open(boost::asio::ip::udp::v4());
}


void socket_wrapper::bind(const boost::asio::ip::udp::endpoint &endpoint)
{
	socket.bind(endpoint);
}


void socket_wrapper::bind(const std::string &endpoint)
{
	bind(string_to_endpoint(endpoint));
}

boost::asio::ip::udp::endpoint socket_wrapper::string_to_endpoint(const std::string &remote_endpoint_str)
{
	std::smatch m;
	std::regex re(R"(^([^:]*)(:(\d+))?$)");

	if (!std::regex_match(remote_endpoint_str, m, re))
		throw std::invalid_argument("Could not parse "+remote_endpoint_str);

	auto host=m[1].str();
	std::string port="12382";

	if (m.size()>2)
		port=m[3].str();

	io_service ios;
	udp::resolver resolver(ios);
	udp::resolver::query query(host, port);
	auto i=resolver.resolve(query);
	boost::asio::ip::udp::endpoint endpoint;

	for (; i!=decltype(resolver)::iterator(); ++i)
	{
		endpoint=*i;
		std::cerr << "Trying " << endpoint << std::endl;
		break;
	}

	if (i==decltype(resolver)::iterator())
		throw std::invalid_argument("Could not resolve "+remote_endpoint_str);

	return endpoint;
}

template<class sender_impl>
sender<sender_impl>::sender(socket_wrapper &sw)
	: sender_impl(sw)
{
}

template<class sender_impl>
void sender<sender_impl>::send(const frame_data &f, std::promise<void> &pr)
{
	if (!sender_impl::sw.socket.is_open())
		return;

	remote_mode_header rmh;

	rmh.width=f.width;
	rmh.height=f.height;
	rmh.bpp=f.bpp;
	rmh.pitch=f.pitch;
	rmh.aspect_ratio=f.aspect_ratio;
	rmh.seq_id=++seq_id;

	++frame_id;

	current_chunk.reset();

	std::tie(current_chunk.w_div, current_chunk.h_div)=get_frame_divisions(rmh.width, rmh.height, rmh.bpp);

	sender_impl::send([this, &f, &pr] (const boost::system::error_code &error, std::size_t bytes_transferred)
	{
		if (error)
			std::cerr << "send failed: " << error.message() << std::endl;

		send_next_chunk(f, pr);
	}, rmh);
}

template<class sender_impl>
void sender<sender_impl>::send(const frame_data_managed &f, std::promise<void> &pr)
{
	send(static_cast<const frame_data &>(f), pr);
}

template<class sender_impl>
void sender<sender_impl>::send_next_chunk(const frame_data &f, std::promise<void> &pr)
{
	int &x=current_chunk.x;
	int &y=current_chunk.y;
	int &w_div=current_chunk.w_div;
	int &h_div=current_chunk.h_div;
	auto &chunk_id=current_chunk.chunk_id;
	int top;
	int left;
	int bottom;
	int right;

	if (x>=w_div)
	{
		x=0;
		++y;
	}

	if (y>=h_div || current_chunk.abort)
	{
		pr.set_value();

		return;
	}

	std::tie(top, left, bottom, right)=get_chunk(f.width, f.height, w_div, h_div, y, x);

	++x;

	send_chunk(f, frame_id, ++chunk_id, w_div*h_div, top, left, bottom, right, [this, &f, &pr] (const boost::system::error_code &error, std::size_t bytes_transferred) { send_next_chunk(f, pr); });
}

template<class sender_impl>
template<class handler_type>
void sender<sender_impl>::send_chunk(const frame_data &f, int frame_id, int chunk_id, int total_chunks, int top, int left, int bottom, int right, handler_type sent_handler)
{
	frame_data_managed &chunk=current_chunk.buffer;
	remote_chunk_header &rch=current_chunk.rch;

	rch.x=left;
	rch.y=top;
	rch.width=right-left;
	rch.bpp=f.bpp;
	rch.pitch=(rch.width*f.bpp+7)/8;
	rch.height=bottom-top;
	rch.chunk_id=chunk_id;
	rch.frame_chunks=total_chunks;
	rch.frame_id=frame_id;
	rch.seq_id=++seq_id;

	chunk.resize(rch.width, rch.height, rch.pitch, rch.bpp);

	for (std::uint32_t y=0; y<rch.height; ++y)
	{
		std::copy(f.pixel<std::uint8_t>(left, top+y), f.pixel<std::uint8_t>(right, top+y), chunk.pixel<std::uint8_t>(0, y));
	}

	sender_impl::send(sent_handler, rch, chunk.buffer());
}

template<class sender_impl>
void sender<sender_impl>::set_remote_endpoint(const std::string &remote_endpoint_str)
{
	set_remote_endpoint(socket_wrapper::string_to_endpoint(remote_endpoint_str));
}

template<class sender_impl>
void sender<sender_impl>::set_remote_endpoint(const boost::asio::ip::udp::endpoint &endpoint)
{
	sender_impl::remote_endpoint=endpoint;
}

template<class sender_impl>
void sender<sender_impl>::restart()
{
	current_chunk.x=0;
	current_chunk.y=0;
	current_chunk.chunk_id=0;
}

template<class sender_impl>
void sender<sender_impl>::chunk_progress::reset()
{
	rch=remote_chunk_header();
	x=0;
	y=0;
	w_div=1;
	h_div=1;
	chunk_id=~0;
	abort=false;
}

template
struct netvid::sender<unlimited_sender>;

template
struct netvid::sender<rate_limited_sender>;

receiver::receiver(socket_wrapper &sw)
	: sw(sw)
{
	recv_buffer.resize(max_pkt_size);
}

receiver::~receiver()
{
}

void receiver::start()
{
	boost::asio::socket_base::receive_buffer_size option(1024*1024);

	sw.socket.set_option(option);
	sw.socket.get_option(option);

	std::cerr << "Receive buffer size: " << option.value() << std::endl;
	std::cerr << "Started, listening on " << boost::lexical_cast<std::string>(sw.socket.local_endpoint()) << std::endl;

	recv_next_packet();
}

void receiver::recv_next_packet()
{
	sw.socket.async_receive_from(boost::asio::buffer(recv_buffer), threaded_endpoint, [this] (const boost::system::error_code &error, std::size_t bytes_received)
	{ recv_handler(error, bytes_received); });
}

void receiver::recv_handler(const boost::system::error_code &error, std::size_t bytes_transferred)
{
	if (error && error != boost::asio::error::message_size)
	{
		std::cerr << "recv failed: " << error.message() << std::endl;
		//throw boost::system::system_error(error);
	}
	else
		internal_packet_handler(recv_buffer.data(), recv_buffer.data()+bytes_transferred, threaded_endpoint);

	recv_next_packet();
}

void receiver::packet_handler(const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint)
{
}

void receiver::internal_packet_handler(const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint)
{
	packet_handler(data_begin, data_end, remote_endpoint);

	if (on_live_packet)
		on_live_packet(data_begin, data_end, threaded_endpoint);
}

batched_receiver::batched_receiver(socket_wrapper &sw)
	: receiver(sw)
{

}

batched_receiver::~batched_receiver()
{

}


void batched_receiver::packet_handler(const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint)
{
	int start_offset=threaded_packets.internal_buffer.size();
	std::size_t bytes_transferred=data_end-data_begin;

	threaded_packets.internal_buffer.resize(start_offset+bytes_transferred);
	std::copy(data_begin, data_end, &threaded_packets.internal_buffer[start_offset]);
	threaded_packets.packets.push_back({ start_offset, (int)threaded_packets.internal_buffer.size(), remote_endpoint });
}

std::future<void> batched_receiver::flip_buffer_packets(std::promise<void> &promise)
{
	auto future=promise.get_future();
	auto handler=[this, &promise]()
	{
		std::swap(buffered_packets, threaded_packets);
		threaded_packets.clear();
		
		if (on_flip_packet_buffer)
			on_flip_packet_buffer();
		
		promise.set_value();
	};

	sw.socket.get_io_service().post(handler);

	return future;
}

void batched_receiver::process_packets()
{
	std::promise<void> promise;

	flip_buffer_packets(promise).get();

	for (const auto &pkt : buffered_packets.packets)
	{
		auto data_begin=buffered_packets.internal_buffer.data()+pkt.begin;
		auto data_end=buffered_packets.internal_buffer.data()+pkt.end;

		on_packet(data_begin, data_end, pkt.remote_endpoint);
	}

	if (on_batch_complete)
		on_batch_complete();
}

bool chunk_validator::process(const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint)
{
	auto &rch=*reinterpret_cast<const remote_chunk_header *>(data_begin);

	if (rch.pkt_id!=remote_chunk_header().pkt_id)
		return false;

	auto now=std::chrono::steady_clock::now();

	if (!frame_id ||
		frame_id_assign_time+std::chrono::seconds(3)<now ||
		(rch.frame_id-*frame_id>0 && rch.frame_id-*frame_id<60))
	{
		if (frame_id && frame_completed)
			frame_completed(*frame_id);

		if (frame_id && rch.frame_id>*frame_id+1)
		{
			std::cerr << "Missed frame(s) " << *frame_id+1;

			if (rch.frame_id>*frame_id+2)
				std::cerr << "-" << rch.frame_id-1;

			std::cerr << std::endl;
		}

		frame_id=rch.frame_id;
		frame_id_assign_time=now;
		chunks_received.clear();
	}

	if (frame_id!=rch.frame_id)
		return false;

	if (chunks_received.size()!=rch.frame_chunks)
		chunks_received.assign(rch.frame_chunks, false);

	chunks_received[rch.chunk_id]=true;

	if (on_chunk)
		on_chunk(rch, data_begin+sizeof(rch), data_end-(data_begin+sizeof(rch)));

	bool rcvd_full_frame=std::find(chunks_received.begin(), chunks_received.end(), false)==chunks_received.end();

	if (!rcvd_full_frame)
		return true;

	if (frame_completed)
		frame_completed(*frame_id);

	frame_id=boost::none;
	chunks_received.clear();

	return true;
}

bool chunk_validator::complete() const
{
	return std::find(chunks_received.begin(), chunks_received.end(), false)==chunks_received.end();
}

void chunk_validator::trace_missing_chunks()
{
	if (complete())
		return;

	std::cerr << "Missing chunks in frame " << frame_id << ": ";

	bool first=true;
	int missing=0;
	auto begin=chunks_received.begin();

	for (auto i=begin; i<chunks_received.end();)
	{
		if (!*i)
		{
			auto j=std::find(std::next(i), chunks_received.end(), true);
			auto dist=std::distance(i, j);

			missing+=dist;

			if (!first)
				std::cerr << ", ";
			else
				first=false;

			std::cerr << std::distance(begin, i);

			if (dist>1)
				std::cerr << "-" << std::distance(begin, std::prev(j));

			i=j;

			continue;
		}

		++i;
	}

	std::cerr << " (" << missing << ")";
	std::cerr << std::endl;
}

frame_receiver::frame_receiver(socket_wrapper &sw)
	: batched_receiver(sw)
{
	on_flip_packet_buffer=[this]
	{
		std::unique_lock<std::mutex> l(m);

		if (!frame_pending_processing)
			return;
		
		frame_pending_processing=false;
	};

	processed_chunk_validator.frame_completed=[this] (auto)
	{
		processed_chunk_validator.trace_missing_chunks();

		frame_pending=false;
		this->flip_buffers();
	};

	processed_chunk_validator.on_chunk=[this] (const remote_chunk_header &header, const std::uint8_t *data, int length)
	{
		frame_pending=true;
		on_chunk(header, data, length);
	};

	on_packet=[this] (const std::uint8_t *data_begin, const std::uint8_t *data_end, const udp::endpoint &remote_endpoint)
	{
		auto &rh=*reinterpret_cast<const remote_header *>(data_begin);

		/*if (current_seq_id && rh.seq_id-*current_seq_id>1)
		{
		std::cout << "missed packets " << *current_seq_id+1 << "-" << rh.seq_id-1 << std::endl;
		}*/

		current_seq_id=rh.seq_id;

		//expire(last_seq_id);
		expire(last_mode_set);
		//expire(last_frame_id);

		switch (rh.pkt_id)
		{
		case 0:
			if (check_new(last_mode_set, rh.seq_id))
			{
				auto &rmh=*reinterpret_cast<const remote_mode_header *>(&rh);

				if (on_mode_set)
					on_mode_set(rmh);
			}
			break;
		}

		processed_chunk_validator.process(data_begin, data_end, remote_endpoint);
	};

	on_batch_complete=[this] ()
	{
		if (buffers_flipped)
		{
			buffers_flipped=false;

			if (on_frame)
				on_frame();
		}
	};

	on_mode_set=[this] (const remote_mode_header &rmh)
	{
		back_buffer.resize(rmh.width, rmh.height, rmh.pitch, rmh.bpp);
	};

	on_chunk=[this] (const remote_chunk_header &header, const std::uint8_t *data, int length)
	{
		auto w=std::max<int>(back_buffer.width, header.width+header.x);

		back_buffer.resize(
			w,
			std::max<int>(back_buffer.height, header.height+header.y),
			std::max<int>(back_buffer.pitch, (w*header.bpp+7)/8),
			header.bpp);

		for (std::uint32_t y=0; y<header.height; ++y)
		{
			std::copy(data+header.pitch*y, data+header.pitch*y+(header.width*header.bpp+7)/8, back_buffer.pixel<std::uint8_t>(header.x, header.y+y));
		}
	};

	live_chunk_validator.frame_completed=[this] (auto)
	{
		{
			std::unique_lock<std::mutex> l(m);

			frame_pending_processing=true;
		}

		cv.notify_one();
	};
}

void frame_receiver::packet_handler(const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint)
{
	batched_receiver::packet_handler(data_begin, data_end, remote_endpoint);

	live_chunk_validator.process(data_begin, data_end, remote_endpoint);
/*
	auto &rh=*reinterpret_cast<const remote_header *>(data_begin);
	bool should_process_chunk=live_chunk_validator.process(data_begin, data_end, remote_endpoint);

	if (rh.pkt_id==remote_chunk_header().pkt_id && !should_process_chunk)
		return;*/
}

void frame_receiver::flip_buffers()
{
	std::unique_lock<std::mutex> lock(front_buffer_mutex);

	std::swap(front_buffer, back_buffer);
	back_buffer.resize(front_buffer.width, front_buffer.height, front_buffer.pitch, front_buffer.bpp);
	std::copy(front_buffer.data, front_buffer.end(), back_buffer.data);
	buffers_flipped=true;
}

void frame_receiver::expire(boost::optional<std::uint32_t> &seq_id)
{
	if (!seq_id)
		return;

	if (!current_seq_id || (*current_seq_id-*seq_id)>=seq_diff_out_of_range)
		seq_id=boost::none;
}

bool frame_receiver::check_new(boost::optional<std::uint32_t> &stored_seq_id, std::uint32_t new_seq_id)
{
	if (!stored_seq_id || ((new_seq_id-*stored_seq_id)<seq_diff_out_of_range && (new_seq_id-*stored_seq_id)>0))
	{
		stored_seq_id=new_seq_id;

		return true;
	}

	return false;
}

std::unique_lock<std::mutex> frame_receiver::lock_front_buffer()
{
	return std::unique_lock<std::mutex>(front_buffer_mutex);
}

boost::optional<std::uint32_t> frame_receiver::get_front_frame_id() const
{
	return processed_chunk_validator.frame_id;
}

void netvid::frame_receiver::wait_for_frame()
{
	std::unique_lock<std::mutex> l(m);

	if (frame_pending_processing)
		return;

	// don't bother checking for spurious wake-ups, is of no consequence
	cv.wait(l);
}

io_service_wrapper::io_service_wrapper()
{
	work.emplace(io_service);
}

io_service_wrapper::~io_service_wrapper()
{
	stop();
}

void io_service_wrapper::run()
{
	thread=std::thread([this] { io_service.run(); });
}

void io_service_wrapper::stop()
{
	work.reset();

	if (thread.joinable())
		thread.join();
}

void packets::clear()
{
	internal_buffer.clear();
	packets.clear();
}

