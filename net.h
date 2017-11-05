#ifndef NET_H
#define NET_H

#include <memory>
#include <regex>
#include <thread>
#include <mutex>
#include <future>

#include <boost/asio.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/high_resolution_timer.hpp>
#include <boost/thread.hpp>

#include "framebuffer.h"
#include "protocol.h"

namespace netvid
{
	struct io_service_wrapper
	{
		boost::asio::io_service io_service;
		std::thread thread;
		boost::optional<boost::asio::io_service::work> work;

		io_service_wrapper();
		~io_service_wrapper();

		void run();
		void stop();
	};

	struct socket_wrapper
	{
		boost::asio::ip::udp::socket socket;
		boost::asio::io_service::strand strand;

		socket_wrapper(boost::asio::io_service &service);

		void bind(const boost::asio::ip::udp::endpoint &endpoint);
		void bind(const std::string &endpoint);

		static boost::asio::ip::udp::endpoint string_to_endpoint(const std::string &str);
	};

	template<class header_type>
	static auto prepare_packet(const header_type &header, const boost::asio::const_buffer &payload={})
	{
		std::array<boost::asio::const_buffer, 2> packet;

		packet[0]=boost::asio::buffer(&header, sizeof(header));
		packet[1]=payload;

		return packet;
	}

	struct unlimited_sender
	{
		socket_wrapper &sw;
		boost::asio::ip::udp::endpoint remote_endpoint;

		unlimited_sender(socket_wrapper &sw)
			: sw(sw)
		{

		}

		template<class... args_type, class handler_type>
		void send(handler_type sent_handler, args_type && ...args)
		{
			return sw.socket.async_send_to(prepare_packet(std::forward<args_type>(args)...), remote_endpoint, sent_handler);
		}
	};

	struct rate_limited_sender
	{
		socket_wrapper &sw;
		boost::asio::ip::udp::endpoint remote_endpoint;
		int max_rate_bytes=90*1024*1024/8; // default limit 90 mbps
		boost::asio::high_resolution_timer timer;
		bool sent=false;
		bool expired=false;

		rate_limited_sender(socket_wrapper &sw)
			: sw(sw), timer(sw.socket.get_io_service())
		{

		}

		std::chrono::microseconds bytes_to_us(int bytes_sent) const
		{
			return std::chrono::microseconds((bytes_sent*1000*1000)/max_rate_bytes);
		}

		template<class handler_type>
		void delay(int bytes_sent, handler_type handler)
		{
			timer.expires_from_now(bytes_to_us(bytes_sent));
			timer.async_wait(handler);
		}

		template<class... args_type, class handler_type>
		void send(handler_type sent_handler, args_type && ...args)
		{
			sent=false;
			expired=false;

			std::size_t bytes_to_send=0;
			auto packet=prepare_packet(std::forward<args_type>(args)...);

			for (const auto &i : packet)
				bytes_to_send+=boost::asio::buffer_size(i);

			timer.expires_from_now(bytes_to_us(bytes_to_send));

			sw.socket.async_send_to(packet, remote_endpoint, [this, sent_handler] (const boost::system::error_code &error, std::size_t bytes_transferred)
			{
				transfer_complete(sent_handler, error, bytes_transferred);
			});

			timer.async_wait([this, sent_handler] (const boost::system::error_code &error)
			{
				wait_complete(sent_handler, error);
			});
		}

		template<class handler_type>
		void transfer_complete(handler_type handler, const boost::system::error_code &error, std::size_t bytes_transferred)
		{
			if (error)
				return handler(error, bytes_transferred);

			sent=true;

			check_ready_to_write(handler, bytes_transferred);
		}

		template<class handler_type>
		void wait_complete(handler_type handler, const boost::system::error_code &error)
		{
			if (error)
				return handler(error, 0);

			expired=true;

			check_ready_to_write(handler, 0);
		}

		template<class handler_type>
		void check_ready_to_write(handler_type handler, std::size_t bytes_transferred)
		{
			if (!sent || !expired)
				return;

			handler(boost::system::errc::make_error_code(boost::system::errc::success), bytes_transferred);
		}
	};

	template<class sender_impl=unlimited_sender>
	struct sender : sender_impl
	{
		std::uint32_t seq_id=~0;
		std::uint32_t frame_id=~0;

		sender(socket_wrapper &sw);

		void set_remote_endpoint(const std::string &remote_endpoint_str);
		void set_remote_endpoint(const boost::asio::ip::udp::endpoint &endpoint={});

		struct chunk_progress
		{
			frame_data_managed buffer;
			remote_chunk_header rch;
			int x=0;
			int y=0;
			int w_div;
			int h_div;
			std::uint32_t chunk_id=~0;
			bool abort=false;

			void reset();
		} current_chunk;

		using sender_impl::send;

		void send(const frame_data &f, std::promise<void> &pr);
		void send(const frame_data_managed &f, std::promise<void> &pr);

		void restart();


	private:
		void send_next_chunk(const frame_data &f, std::promise<void> &pr);

		template<class handler_type>
		void send_chunk(const frame_data &f, int frame_id, int chunk_id, int total_chunks, int top, int left, int bottom, int right, handler_type sent_handler);

		//std::function<void()> sent_handler;
	};

	struct packet
	{
		int begin=0;
		int end=0;
		boost::asio::ip::udp::endpoint remote_endpoint;
	};

	struct packets
	{
		std::vector<std::uint8_t> internal_buffer;
		std::vector<packet> packets;

		void clear();
	};

	struct receiver
	{
		socket_wrapper &sw;
		boost::asio::ip::udp::endpoint remote_endpoint;

		static const std::size_t max_pkt_size=64*1024;

		receiver(socket_wrapper &sw);
		~receiver();

		std::function<void(const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint)> on_live_packet;

		void start();

	protected:
		virtual void packet_handler(const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint);
		
	private:
		boost::asio::ip::udp::endpoint threaded_endpoint;
		std::vector<std::uint8_t> recv_buffer;

		void recv_next_packet();
		void recv_handler(const boost::system::error_code &error, std::size_t bytes_transferred);
		void internal_packet_handler(const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint);
	};

	struct batched_receiver : receiver
	{
		packets buffered_packets;

		static const std::size_t max_pkt_size=64*1024;

		batched_receiver(socket_wrapper &sw);
		~batched_receiver();

		std::function<void(const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint)> on_packet;
		std::function<void()> on_batch_complete;

		void process_packets();

		std::future<void> flip_buffer_packets(std::promise<void> &promise);

	protected:
		void packet_handler(const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint) override;

		std::function<void()> on_flip_packet_buffer;
		
	private:
		packets threaded_packets;
	};

	struct chunk_validator
	{
		std::chrono::steady_clock::time_point frame_id_assign_time;
		boost::optional<std::uint32_t> frame_id;
		std::vector<bool> chunks_received;
		std::function<void (const remote_chunk_header &header, const std::uint8_t *data, int length)> on_chunk;
		std::function<void (std::uint32_t frame_id)> frame_completed;

		bool process(const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint);
		bool complete() const;
		void trace_missing_chunks();
	};

	struct frame_receiver : batched_receiver
	{
		frame_data_managed back_buffer;
		frame_data_managed front_buffer;
		std::mutex front_buffer_mutex;
		std::function<void(const remote_mode_header &header)> on_mode_set;
		std::function<void(const remote_chunk_header &header, const std::uint8_t *data, int length)> on_chunk;
		std::function<void()> on_frame;
		boost::optional<std::uint32_t> last_mode_set;
		boost::optional<std::uint32_t> current_seq_id;
		bool frame_pending=false;
		bool buffers_flipped=false;
		bool frame_pending_processing=false;

		static const auto seq_diff_out_of_range=std::numeric_limits<std::uint32_t>::max()/2;

		frame_receiver(socket_wrapper &sw);

		void wait_for_frame();
		std::unique_lock<std::mutex> lock_front_buffer();
		boost::optional<std::uint32_t> get_front_frame_id() const;

	protected:
		void packet_handler(const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint) override;

	private:
		void init();

		void flip_buffers();

		void expire(boost::optional<std::uint32_t> &seq_id);
		bool check_new(boost::optional<std::uint32_t> &stored_seq_id, std::uint32_t new_seq_id);

		std::condition_variable cv;
		std::mutex m;
		chunk_validator live_chunk_validator;
		chunk_validator processed_chunk_validator;
	};
}

#endif /* NET_H */
