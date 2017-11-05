#ifndef LINUX_FRAMEBUFFER_H
#define LINUX_FRAMEBUFFER_H

#if __linux__

#include <fstream>
#include <iostream>

#include <boost/optional.hpp>

#include "framebuffer.h"

struct linux_framebuffer
{
	std::shared_ptr<int> framebuffer_handle;
	boost::optional<std::ofstream> ttyfs;
	frame_data screen;

	linux_framebuffer(const std::string &fb_path, const std::string &tty_path=std::string());

	void disable_blanking();
	void hide_cursor();
	void wake_up();
	void wait_for_vsync();
};

#endif

#endif /* LINUX_FRAMEBUFFER_H */
