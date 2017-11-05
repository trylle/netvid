#ifndef CHECK_H
#define CHECK_H

#include <cstdio>

#include <boost/format.hpp>

template<typename result_type>
inline result_type check(result_type result, const char *code, const char *file, int line)
{
	if (result==reinterpret_cast<result_type>(-1))
	{
		std::perror(boost::str(boost::format("%1% failed at %2%:%3%") % code % file % line).c_str());

		exit(-1);
	}

	return result;
}

#define CHECK(x) \
	check((x), #x, __FILE__, __LINE__)

#endif /* CHECK_H */
