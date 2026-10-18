/*
 * Klest — a lean, adaptable Wayland desktop environment in C.
 *
 * Build-skeleton entry point. For now it only reports its version, which is
 * enough to verify the Meson/Ninja toolchain (ADR 0006). The Wayland
 * compositor (ADR 0002, 0004) will grow from here.
 */

#include <stdio.h>

#ifndef KLEST_VERSION
#define KLEST_VERSION "unknown"
#endif

int main(void)
{
	printf("Klest %s\n", KLEST_VERSION);
	return 0;
}
