#include <fstream>

#include <boost/program_options.hpp>

#include "check.h"
#include "protocol.h"
#include "net.h"

using namespace boost;
using namespace boost::asio;
using namespace boost::asio::ip;
namespace po=boost::program_options;

typedef std::chrono::steady_clock network_clock_t;

static volatile bool interrupted = false;

void interrupt_handler(int)
{
    interrupted = true;
}

int main(int argc, char **argv)
{
	signal(SIGINT, interrupt_handler);

	try
	{
		po::options_description desc("Allowed options");
		std::string out_filename;

		desc.add_options()
			("help,h", "produce help message")
			("recv", po::value<std::string>()->required(), "recv [ip:port]")
			("file,f", po::value<std::string>(&out_filename)->required(), "output file [filename]")
			;

		po::variables_map vm;

		po::store(po::parse_command_line(argc, argv, desc), vm);

		if (vm.count("help"))
		{
			std::cerr << desc << std::endl;

			return 1;
		}

		po::notify(vm);

		netvid::io_service_wrapper io_service;
		netvid::socket_wrapper socket(io_service.io_service);

		socket.bind(vm["recv"].as<std::string>());

		netvid::receiver fr(socket);
		boost::asio::high_resolution_timer flush_timer(io_service.io_service);
		auto start_time=network_clock_t::now();
		std::ofstream ofs_real;
		std::ostream *pofs=&ofs_real;
		
		if (out_filename!="-")
			ofs_real.open(out_filename, std::ios::binary);
		else
			pofs=&std::cout;

		std::ostream &ofs=*pofs;
		std::size_t written=0;

		fr.on_live_packet=[&] (const std::uint8_t *data_begin, const std::uint8_t *data_end, const boost::asio::ip::udp::endpoint &remote_endpoint)
		{
			auto now=network_clock_t::now();
			auto diff=now-start_time;
			std::uint32_t sz=data_end-data_begin;

			ofs.write(reinterpret_cast<const char *>(&diff), sizeof(diff));
			ofs.write(reinterpret_cast<const char *>(&sz), sizeof(sz));
			ofs.write(reinterpret_cast<const char *>(data_begin), sz);

			written+=sizeof(diff);
			written+=sizeof(sz);
			written+=sz;
		};

		fr.start();

		auto flush_handler=[&] (auto &self) -> void
		{
			using namespace std::chrono_literals;

			ofs.flush();
			std::cerr << written << " bytes written...\r" << std::flush;

			if (interrupted)
			{
				std::cerr << std::endl << "Stopping..." << std::endl;
				io_service.io_service.stop();

				return;
			}

			flush_timer.expires_from_now(1s);
			flush_timer.async_wait([&] (auto error)
				{
					if (error)
					{
						std::cerr << error.message() << std::endl;

						return;
					}

					self(self);
				});
		};

		flush_handler(flush_handler);

		io_service.io_service.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
	}

	return 0;
}
