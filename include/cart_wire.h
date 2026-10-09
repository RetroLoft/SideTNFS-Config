#ifndef SIDETNFS_CART_WIRE_H
#define SIDETNFS_CART_WIRE_H
/* Both 68000 and 68030 send bytes in the reserved configuration window.
 * Raw 16-bit words can collide with native GEMDRIVE/SIDEETH addresses. */
#define CONFIG_WIRE_HEADER 0xA5A5UL
static unsigned long config_wire_byte(unsigned int byte)
{
    return 0x7E00UL + ((unsigned long)(byte & 255u) << 1);
}
static int config_needs_030_cache(unsigned long mch, unsigned long cpu)
{
    return (mch >> 16) == 3UL && cpu == 30UL;
}
#endif
