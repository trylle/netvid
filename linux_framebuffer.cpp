#if __linux__

#include "linux_framebuffer.h"

#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <linux/fb.h>

#include "check.h"

linux_framebuffer::linux_framebuffer(const std::string &fb_path, const std::string &tty_path/*=std::string()*/)
{
	int handle=CHECK(open(fb_path.c_str(), O_RDWR));

	framebuffer_handle.reset(new int(handle), [] (int *handle) { close(*handle); delete handle; });
	ttyfs.emplace(tty_path);

	if (ttyfs->bad())
		ttyfs.reset();

	fb_fix_screeninfo fixed_info;
	fb_var_screeninfo variable_info;

	CHECK(ioctl(*framebuffer_handle, FBIOGET_VSCREENINFO, &variable_info));

	variable_info.bits_per_pixel=32;

	CHECK(ioctl(*framebuffer_handle, FBIOPUT_VSCREENINFO, &variable_info));

	CHECK(ioctl(*framebuffer_handle, FBIOGET_FSCREENINFO, &fixed_info));

	variable_info.xoffset=0;
	variable_info.yoffset=0;

	CHECK(ioctl(*framebuffer_handle, FBIOPAN_DISPLAY, &variable_info));

	screen.width=variable_info.xres;
	screen.height=variable_info.yres;
	screen.pitch=(variable_info.xres_virtual*variable_info.bits_per_pixel+7)/8;
	screen.bpp=variable_info.bits_per_pixel;
	screen.data=static_cast<std::uint8_t *>(CHECK(mmap(0, screen.bytes(), PROT_READ | PROT_WRITE, MAP_SHARED, *framebuffer_handle, 0)));

	std::cout << screen.width << "x" << screen.height << " " << screen.bpp << "bpp" << " pitch: " << screen.pitch << std::endl;
	std::cout << "Pixel clock: " << 1e12/double(variable_info.pixclock) << " Hz" << std::endl;
	std::cout << "Left margin: " << variable_info.left_margin << " pixels" << std::endl;
	std::cout << "Right margin: " << variable_info.right_margin << " pixels" << std::endl;
	std::cout << "Upper margin: " << variable_info.upper_margin << " pixels" << std::endl;
	std::cout << "Lower margin: " << variable_info.lower_margin << " pixels" << std::endl;
	std::cout << "HSYNC: " << variable_info.hsync_len << " pixels" << std::endl;
	std::cout << "VSYNC: " << variable_info.vsync_len << " pixels" << std::endl;
}

void linux_framebuffer::disable_blanking()
{
	if (!ttyfs)
		return;

	*ttyfs << "\033[9;0]" << std::flush;
}

void linux_framebuffer::hide_cursor()
{
	if (!ttyfs)
		return;

	*ttyfs << "\033[?25l" << std::flush;
}

void linux_framebuffer::wake_up()
{
	if (!ttyfs)
		return;

	*ttyfs << "\033[13]" << std::flush; // Wake up terminal
}

void linux_framebuffer::wait_for_vsync()
{
	ioctl(*framebuffer_handle, FBIO_WAITFORVSYNC, 0);
}

#endif