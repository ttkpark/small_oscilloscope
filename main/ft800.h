#ifndef FT800_H
#define FT800_H

#include "driver/spi_master.h"
#include "esp_err.h"

typedef struct {
    spi_device_handle_t spi;
    int cs_pin;
} ft800_handle_t;

// FT800 Memory Map (공식 데이터시트 기준)
#define FT800_RAM_G         0x000000    // Main graphics RAM (256KB)
#define FT800_ROM_CHIPID    0x0C0000    // Chip ID (4B, 0x0800 for FT800)
#define FT800_ROM_FONT      0x0B23C     // Font table and bitmap
#define FT800_ROM_FONT_ADDR 0x0FFFFC    // Font table pointer address
#define FT800_RAM_DL        0x100000    // Display List RAM (8KB)
#define FT800_RAM_PAL       0x102000    // Palette RAM (1KB)
#define FT800_REG_BASE      0x102400    // Register base (0x102400 ~)
#define FT800_RAM_CMD       0x108000    // Command buffer (4KB)

// FT800 Registers (공식 데이터시트 기준, 0x102400 ~)
#define FT800_REG_ID            0x102400 // Identification register (always 0x7C)
#define FT800_REG_FRAMES        0x102404 // Frame counter
#define FT800_REG_CLOCK         0x102408 // Clock cycles since reset
#define FT800_REG_FREQUENCY     0x10240C // Main clock frequency
#define FT800_REG_RENDERMODE    0x102410 // Rendering mode
#define FT800_REG_SNAPY         0x102414 // Scan line select for RENDERMODE 1
#define FT800_REG_SNAPSHOT      0x102418 // Trigger for RENDERMODE 1
#define FT800_REG_CPURESET      0x10241C // Graphics/audio/touch engines reset
#define FT800_REG_TAP_CRC       0x102420 // Live video tap CRC
#define FT800_REG_TAP_MASK      0x102424 // Live video tap mask
#define FT800_REG_HCYCLE        0x102428 // Horizontal total cycle count
#define FT800_REG_HOFFSET       0x10242C // Horizontal display start offset
#define FT800_REG_HSIZE         0x102430 // Horizontal display pixel count
#define FT800_REG_HSYNC0        0x102434 // Horizontal sync fall offset
#define FT800_REG_HSYNC1        0x102438 // Horizontal sync rise offset
#define FT800_REG_VCYCLE        0x10243C // Vertical total cycle count
#define FT800_REG_VOFFSET       0x102440 // Vertical display start offset
#define FT800_REG_VSIZE         0x102444 // Vertical display line count
#define FT800_REG_VSYNC0        0x102448 // Vertical sync fall offset
#define FT800_REG_VSYNC1        0x10244C // Vertical sync rise offset
#define FT800_REG_DLSWAP        0x102450 // Display list swap control
#define FT800_REG_ROTATE        0x102454 // Screen 180 degree rotate
#define FT800_REG_OUTBITS       0x102458 // Output bit resolution
#define FT800_REG_DITHER        0x10245C // Output dither enable
#define FT800_REG_SWIZZLE       0x102460 // Output RGB signal swizzle
#define FT800_REG_CSPREAD       0x102464 // Output clock spreading enable
#define FT800_REG_PCLK_POL      0x102468 // PCLK polarity
#define FT800_REG_PCLK          0x10246C // PCLK frequency divider
#define FT800_REG_TAG_X         0x102470 // Tag query X coordinate
#define FT800_REG_TAG_Y         0x102474 // Tag query Y coordinate
#define FT800_REG_TAG           0x102478 // Tag query result
#define FT800_REG_VOL_PB        0x10247C // Volume for playback
#define FT800_REG_VOL_SOUND     0x102480 // Volume for synthesizer sound
#define FT800_REG_SOUND         0x102484 // Sound effect select
#define FT800_REG_PLAY          0x102488 // Start effect playback
#define FT800_REG_GPIO_DIR      0x10248C // GPIO pin direction
#define FT800_REG_GPIO          0x102490 // GPIO pin value
#define FT800_REG_INT_FLAGS     0x102498 // Interrupt flags
#define FT800_REG_INT_EN        0x10249C // Global interrupt enable
#define FT800_REG_INT_MASK      0x1024A0 // Interrupt enable mask
#define FT800_REG_PLAYBACK_START 0x1024A4 // Audio playback RAM start address
#define FT800_REG_PLAYBACK_LENGTH 0x1024A8 // Audio playback sample length
#define FT800_REG_PLAYBACK_READPTR 0x1024AC // Audio playback current read pointer
#define FT800_REG_PLAYBACK_FREQ  0x1024B0 // Audio playback sampling frequency
#define FT800_REG_PLAYBACK_FORMAT 0x1024B4 // Audio playback format
#define FT800_REG_PLAYBACK_LOOP  0x1024B8 // Audio playback loop enable
#define FT800_REG_PLAYBACK_PLAY  0x1024BC // Start audio playback
#define FT800_REG_PWM_HZ        0x1024C0 // Backlight PWM output frequency
#define FT800_REG_PWM_DUTY      0x1024C4 // Backlight PWM output duty cycle
#define FT800_REG_MACRO_0       0x1024C8 // Display list macro command 0
#define FT800_REG_MACRO_1       0x1024CC // Display list macro command 1
#define FT800_REG_CMD_READ      0x1024E4 // Command buffer read pointer
#define FT800_REG_CMD_WRITE     0x1024E8 // Command buffer write pointer
#define FT800_REG_CMD_DL        0x1024EC // Command display list offset
#define FT800_REG_TOUCH_MODE    0x1024F0 // Touch-screen sampling mode
#define FT800_REG_TOUCH_ADC_MODE 0x1024F4 // Touch ADC mode
#define FT800_REG_TOUCH_CHARGE  0x1024F8 // Touch charge time
#define FT800_REG_TOUCH_SETTLE  0x1024FC // Touch settle time
#define FT800_REG_TOUCH_OVERSAMPLE 0x102500 // Touch oversample factor
#define FT800_REG_TOUCH_RZTHRESH 0x102504 // Touch resistance threshold
#define FT800_REG_TOUCH_RAW_XY  0x102508 // Touch raw x/y
#define FT800_REG_TOUCH_RZ      0x10250C // Touch resistance
#define FT800_REG_TOUCH_SCREEN_XY 0x102510 // Touch screen x/y
#define FT800_REG_TOUCH_TAG_XY  0x102514 // Touch tag x/y
#define FT800_REG_TOUCH_TAG     0x102518 // Touch tag result
#define FT800_REG_TOUCH_TRANSFORM_A 0x10251C // Touch transform coefficient A
#define FT800_REG_TOUCH_TRANSFORM_B 0x102520 // Touch transform coefficient B
#define FT800_REG_TOUCH_TRANSFORM_C 0x102524 // Touch transform coefficient C
#define FT800_REG_TOUCH_TRANSFORM_D 0x102528 // Touch transform coefficient D
#define FT800_REG_TOUCH_TRANSFORM_E 0x10252C // Touch transform coefficient E
#define FT800_REG_TOUCH_TRANSFORM_F 0x102530 // Touch transform coefficient F
#define FT800_REG_TOUCH_DIRECT_XY  0x102574 // Touch direct x/y
#define FT800_REG_TOUCH_DIRECT_Z1Z2 0x102578 // Touch direct z1/z2
#define FT800_REG_TRACKER       0x109000 // Track register

// FT800 Chip ID (공식)
#define FT800_CHIP_ID            0x7C

// FT800 명령 (프로그래머 가이드 기반)
#define FT800_CMD_DLSTART        0x00000000
#define FT800_CMD_SWAP           0x00000001
#define FT800_CMD_CLEAR          0x00000026
#define FT800_CMD_DISPLAY        0x00000000
#define FT800_CMD_TEXT           0x0000000C
#define FT800_CMD_NUMBER         0x0000002E
#define FT800_CMD_GAUGE          0x00000013
#define FT800_CMD_PROGRESS       0x0000000F
#define FT800_CMD_SCROLLBAR      0x00000011
#define FT800_CMD_SLIDER         0x00000010
#define FT800_CMD_BUTTON         0x0000000D
#define FT800_CMD_KEYS           0x0000000E
#define FT800_CMD_DIAL           0x0000002D
#define FT800_CMD_TOGGLE         0x00000012
#define FT800_CMD_COLDSTART      0x00000032
#define FT800_CMD_APPEND         0x0000001E
#define FT800_CMD_INFLATE        0x00000022
#define FT800_CMD_LOADIMAGE      0x00000024
#define FT800_CMD_MEMCPY         0x0000001D
#define FT800_CMD_MEMCRC         0x00000018
#define FT800_CMD_MEMSET         0x0000002B
#define FT800_CMD_MEMWRITE       0x0000001A
#define FT800_CMD_MEMZERO        0x0000001C
#define FT800_CMD_PLAYVIDEO      0x0000002A
#define FT800_CMD_REGREAD        0x00000019
#define FT800_CMD_ROMFONT        0x0000003F
#define FT800_CMD_SETBASE        0x00000007
#define FT800_CMD_SETBITMAP      0x00000043
#define FT800_CMD_SETFONT        0x0000002F
#define FT800_CMD_SETMATRIX      0x0000002C
#define FT800_CMD_SETROTATE      0x00000029
#define FT800_CMD_SETSIZE        0x0000000B
#define FT800_CMD_SETSPRITE      0x0000003B
#define FT800_CMD_SKETCH         0x00000030
#define FT800_CMD_SNAPSHOT       0x0000001F
#define FT800_CMD_SPINNER        0x00000016
#define FT800_CMD_STOP           0x00000017
#define FT800_CMD_SYNC           0x00000042
#define FT800_CMD_TRACK          0x0000002C
#define FT800_CMD_TRANSLATE      0x00000027
#define FT800_CMD_UPDATE         0x00000000

// Display List 명령 (프로그래머 가이드)
#define FT800_DL_CLEAR_COLOR_RGB 0x00000002
#define FT800_DL_CLEAR_COLOR_RGB565 0x00000003
#define FT800_DL_CLEAR          0x00000026
#define FT800_DL_COLOR_RGB      0x00000004
#define FT800_DL_COLOR_RGB565   0x00000005
#define FT800_DL_COLOR_A        0x00000006
#define FT800_DL_VERTEX2F       0x00000001
#define FT800_DL_VERTEX2II      0x00000002
#define FT800_DL_BITMAP_HANDLE  0x00000005
#define FT800_DL_BITMAP_SOURCE  0x00000001
#define FT800_DL_BITMAP_LAYOUT  0x00000007
#define FT800_DL_BITMAP_SIZE    0x00000008
#define FT800_DL_BITMAP_TRANSFORM_A 0x00000015
#define FT800_DL_BITMAP_TRANSFORM_B 0x00000016
#define FT800_DL_BITMAP_TRANSFORM_C 0x00000017
#define FT800_DL_BITMAP_TRANSFORM_D 0x00000018
#define FT800_DL_BITMAP_TRANSFORM_E 0x00000019
#define FT800_DL_BITMAP_TRANSFORM_F 0x0000001A

// 함수 선언
esp_err_t ft800_init(ft800_handle_t *dev, spi_host_device_t host, int mosi, int sclk, int cs);
esp_err_t ft800_check_id(ft800_handle_t *dev);
void ft800_write8(ft800_handle_t *dev, uint32_t addr, uint8_t data);
void ft800_write16(ft800_handle_t *dev, uint32_t addr, uint16_t data);
void ft800_write32(ft800_handle_t *dev, uint32_t addr, uint32_t data);
uint8_t ft800_read8(ft800_handle_t *dev, uint32_t addr);
uint16_t ft800_read16(ft800_handle_t *dev, uint32_t addr);
uint32_t ft800_read32(ft800_handle_t *dev, uint32_t addr);

// 명령 함수
void ft800_cmd_start(ft800_handle_t *dev);
void ft800_cmd_swap(ft800_handle_t *dev);
void ft800_cmd_clear(ft800_handle_t *dev, uint8_t c, uint8_t s, uint8_t t);
void ft800_cmd_display(ft800_handle_t *dev);
void ft800_cmd_text(ft800_handle_t *dev, int x, int y, int font, int options, const char *str);

// Display List 함수
void ft800_dl_start(ft800_handle_t *dev);
void ft800_dl_end(ft800_handle_t *dev);
void ft800_dl_point(ft800_handle_t *dev, int x, int y, int size, uint32_t color);
void ft800_dl_line(ft800_handle_t *dev, int x0, int y0, int x1, int y1, uint32_t color);
void ft800_dl_rect(ft800_handle_t *dev, int x0, int y0, int x1, int y1, uint32_t color);

// 백라이트 제어 함수
void ft800_backlight(ft800_handle_t *dev, uint8_t duty);
void ft800_backlight_on(ft800_handle_t *dev);
void ft800_backlight_off(ft800_handle_t *dev);

#endif // FT800_H 