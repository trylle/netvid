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
		std::string out_filename;
		int seek;
		int stop;

		desc.add_options()
			("help", "produce help message")
			("input-file,i", po::value<std::string>(&in_filename)->required(), "input file [filename]")
			("output-file,o", po::value<std::string>(&out_filename)->required(), "output file [filename]")
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

		std::ifstream ifs_file;
		std::istream *pifs=&std::cin;

		if (in_filename!="-")
		{
			ifs_file.open(in_filename, std::ios::binary);
			pifs=&ifs_file;
		}

		std::istream &ifs=*pifs;

		std::ofstream ofs_real;
		std::ostream *pofs=&ofs_real;

		if (out_filename!="-")
			ofs_real.open(out_filename, std::ios::binary);
		else
			pofs=&std::cout;

		std::ostream &ofs=*pofs;

		struct packet_t
		{
			network_clock_t::duration time;
			std::string payload;
		};

		bool peeked=false;
		packet_t current_packet;

		netvid::chunk_validator validator;

		auto process_packet=[&] () -> bool
		{
			if (peeked)
			{
				peeked=false;

				return true;
			}

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

		while (process_packet())
		{
			auto sz=std::uint32_t(current_packet.payload.size());

			ofs.write(reinterpret_cast<const char *>(&current_packet.time), sizeof(current_packet.time));
			ofs.write(reinterpret_cast<const char *>(&sz), sizeof(sz));
			ofs.write(reinterpret_cast<const char *>(current_packet.payload.data()), sz);
		}
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << std::endl;
	}

	return 0;
}
