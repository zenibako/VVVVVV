/* Replaces the SDK's SDL2 shim (of_sdl2.c) in this build: the port brings
 * its own SDL3 layer, and the two define many of the same names. */
typedef int vvv_pocket_no_sdl2;
