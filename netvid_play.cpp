#include <fstream>
#include <iostream>
#include <chrono>

#include <boost/program_options.hpp>
#include <boost/asio/steady_timer.hpp>

#include "check.h"
#include "protocol.h"
#include "net.h"

using namespace boost;
using namespace boost::asio;
using namespace boost::asio::ip;
namespace po=boost::program_options;

typedef std::chrono::high_resolution_clock network_clock_t;

int main(int argc, char **argv)
{
	try
	{
		po::options_description desc("Allowed options");
		std::string in_filename;
		double speed;
		int seek;
		int stop;

		desc.add_options()
			("help", "produce help message")
			("send", po::value<std::string>()->required(), "send [ip:port]")
			("file,f", po::value<std::string>(&in_filename)->required(), "input file [filename]")
			("speed,s", po::value<double>(&speed)->default_value(1), "speed [real, 1=normal speed]")
			("seek", po::value<int>(&seek)->default_value(0), "seek [frame]")
			("stop", po::value<int>(&stop)->default_value(-1), "stop [frame]")
			;

		po::variables_map vm;

		po::store(po::parse_command_line(argc, argv, desc), vm);

		if (vm.count("help"))
		{
			std::cout << desc << std::endl;

			return 1;
		}

		po::notify(vm);

		netvid::io_service_wrapper io_service;
		netvid::socket_wrapper socket(io_service.io_service);
		auto remote_endpoint=socket.string_to_endpoint(vm["send"].as<std::string>());
		boost::optional<network_clock_t::duration> first_packet_time;
		std::ifstream ifs_file;
		std::istream *pifs=&std::cin;

		if (in_filename!="-")
		{
			ifs_file.open(in_filename, std::ios::binary);
			pifs=&ifs_file;
		}

		std::istream &ifs=*pifs;

		boost::asio::high_resolution_timer send_timer(io_service.io_service);
		boost::asio::high_resolution_timer status_timer(io_service.io_service);

		struct packet_t
		{
			network_clock_t::duration time;
			std::string payload;
		};

		//boost::optional<packet_t> packet_peeked;
		bool peeked=false;
		packet_t current_packet;

		netvid::chunk_validator validator;
		boost::optional<std::uint32_t> last_frame_id;

		validator.frame_completed=[&] (auto frame_id)
		{
			if (last_frame_id && *last_frame_id+1!=frame_id)
				std::cerr << "Got frame " << frame_id << " after " << *last_frame_id << std::endl;

			last_frame_id=frame_id;
			validator.trace_missing_chunks();
		};

		auto process_packet=[&] () -> bool
		{
			if (peeked)
			{
				peeked=false;

				return true;
			}

			//network_clock_t::duration packet_time;
			std::uint32_t payload_size;

			ifs.read(reinterpret_cast<char *>(&current_packet.time), sizeof(current_packet.time));
			ifs.read(reinterpret_cast<char *>(&payload_size), sizeof(payload_size));

			if (ifs.eof() || ifs.bad() || ifs.fail())
				return false;

			current_packet.payload.resize(payload_size);

			ifs.read(&current_packet.payload[0], payload_size);

			validator.process(reinterpret_cast<const std::uint8_t *>(current_packet.payload.data()), reinterpret_cast<const std::uint8_t *>(current_packet.payload.data()+payload_size), boost::asio::ip::udp::endpoint());

			if (stop>=0 && validator.frame_id && *validator.frame_id>=stop)
				return false;

			return true;
		};

		for (; seek>0;)
		{
			if (!process_packet())
				break;

			if (!validator.frame_id || *validator.frame_id<seek)
				continue;

			peeked=true;

			break;
		}

		auto start_time=network_clock_t::now();

		auto prepare_timer=[&] (auto &self)
		{
			if (!process_packet())
			{
				io_service.stop();

				return;
			}

			if (!first_packet_time)
				first_packet_time=current_packet.time;

			auto next=network_clock_t::duration(static_cast<network_clock_t::duration::rep>((current_packet.time-*first_packet_time).count()/speed));
			auto expire_time=start_time+next;

			send_timer.expires_at(expire_time);
			send_timer.async_wait([&self, &socket, &remote_endpoint, &current_packet] (auto error)
				{
					if (error)
					{
						std::cerr << error.message() << std::endl;

						return;
					}

					socket.socket.async_send_to(boost::asio::buffer(current_packet.payload), remote_endpoint, [&self] (const boost::system::error_code &error, std::size_t bytes_transferred)
						{
							if (error)
							{
								std::cerr << error.message() << std::endl;

								return;
							}

							self(self);
						});
				});
		};

		prepare_timer(prepare_timer);

		auto prepare_status_timer=[&] (auto &self)
		{
			using namespace std::chrono_literals;

			if (!io_service.work)
				return;

			std::cout << "\r\033[K";

			if (last_frame_id)
				std::cout << "frame: " << *last_frame_id;
			else
				std::cout << "bytes: " << ifs.tellg();

			if (first_packet_time)
			{
				// adapted from https://stackoverflow.com/a/22069038
				using namespace std::chrono;
				using namespace std;
				auto dur=current_packet.time;
				auto h = duration_cast<hours>(dur);
				auto m = duration_cast<minutes>(dur -= h);
				auto s = duration_cast<seconds>(dur -= m);
				auto ms = duration_cast<seconds>(dur -= s);

				cout << "\ttime: "
						<< setfill('0')
						<< h.count() << ":"
						<< setw(2) << m.count() << ":"
						<< setw(2) << s.count() << "."
						<< setw(3) << ms.count();
			}

			std::cout << "\r" << std::flush;

			status_timer.expires_from_now(1s);
			status_timer.async_wait([&self] (auto error)
				{
					if (error)
					{
						std::cerr << error.message() << std::endl;

						return;
					}

					self(self);
				});
		};

		prepare_status_timer(prepare_status_timer);

		io_service.io_service.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
	}

	return 0;
}
