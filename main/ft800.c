#include "ft800.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "analog_test_simple.h"
#include <math.h>

#define LOG_TAG "FT800"

// 전역 변수 정의
ft800_handle_t *driver_dev = NULL;

static void ft800_spi_transfer(ft800_handle_t *dev, const uint8_t *tx, uint8_t *rx, size_t len) {
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(dev->spi, &t);
}

void ft800_write8(ft800_handle_t *dev, uint32_t addr, uint8_t data) {
    uint8_t buf[4] = { ((addr >> 16) & 0x3F) | 0x80, (addr >> 8) & 0xFF, addr & 0xFF, data };
    ft800_spi_transfer(dev, buf, NULL, 4);
}
void ft800_write16(ft800_handle_t *dev, uint32_t addr, uint16_t data) {
    uint8_t buf[5] = { ((addr >> 16) & 0x3F) | 0x80, (addr >> 8) & 0xFF, addr & 0xFF, data & 0xFF, (data >> 8) & 0xFF };
    ft800_spi_transfer(dev, buf, NULL, 5);
}
void ft800_write32(ft800_handle_t *dev, uint32_t addr, uint32_t data) {
    uint8_t buf[7] = { ((addr >> 16) & 0x3F) | 0x80, (addr >> 8) & 0xFF, addr & 0xFF,
                       data & 0xFF, (data >> 8) & 0xFF, (data >> 16) & 0xFF, (data >> 24) & 0xFF };
    ft800_spi_transfer(dev, buf, NULL, 7);
}

uint8_t ft800_read8(ft800_handle_t *dev, uint32_t addr) {
    uint8_t tx[5] = { ((addr >> 16) & 0x3F), (addr >> 8) & 0xFF, addr & 0xFF, 0, 0 };
    uint8_t rx[5] = {0};
    ft800_spi_transfer(dev, tx, rx, 5);
    return rx[4];
}
uint16_t ft800_read16(ft800_handle_t *dev, uint32_t addr) {
    uint8_t tx[6] = { ((addr >> 16) & 0x3F), (addr >> 8) & 0xFF, addr & 0xFF, 0, 0, 0 };
    uint8_t rx[6] = {0};
    ft800_spi_transfer(dev, tx, rx, 6);
    return rx[4] | (rx[5] << 8);
}
uint32_t ft800_read32(ft800_handle_t *dev, uint32_t addr) {
    uint8_t tx[8] = { ((addr >> 16) & 0x3F), (addr >> 8) & 0xFF, addr & 0xFF, 0, 0, 0, 0, 0 };
    uint8_t rx[8] = {0};
    ft800_spi_transfer(dev, tx, rx, 8);
    return rx[4] | (rx[5] << 8) | (rx[6] << 16) | (rx[7] << 24);
}

esp_err_t ft800_init(ft800_handle_t *pdev, spi_host_device_t host, int mosi, int sclk, int cs) {
    return ft800_init_with_int(pdev, host, mosi, sclk, cs, -1);
}

esp_err_t ft800_spi_speedup(ft800_handle_t *pdev, spi_host_device_t host) {
    // 이미 spi_bus_add_device()가 호출된 상태에서 다시 이 함수를 호출하면,
    // 같은 host에 device가 중복 등록될 수 있으므로 기존 device를 먼저 제거하는 것이 안전함.
    // 또한, 필요하다면 spi_bus_free()로 bus도 정리할 수 있지만, 여러 디바이스가 공유할 수 있으니 주의.
    // 아래는 기존 device가 등록되어 있으면 spi_bus_remove_device()로 제거 후 재등록하는 예시임.

    // 기존 device 핸들이 유효하면 제거
    if (pdev->spi != NULL) {
        spi_bus_remove_device(pdev->spi);
        pdev->spi = NULL;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = FT800_SPI_SPEED_HZ, // 32MHz로 설정
        .mode = 0,
        .spics_io_num = pdev->cs_pin,
        .queue_size = 3,
    };

    // buscfg는 bus마다 1회만 initialize 필요, 이미 되어 있다면 생략 가능
    // 여기서는 bus가 이미 초기화되어 있다고 가정하고, buscfg는 생략

    // SPI 디바이스 재등록
    esp_err_t ret = spi_bus_add_device(host, &devcfg, &pdev->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

esp_err_t ft800_init_with_int(ft800_handle_t *pdev, spi_host_device_t host, int mosi, int sclk, int cs, int int_pin) {
    if (pdev == NULL) {
        ESP_LOGE(LOG_TAG, "Invalid device pointer making new instance");
        pdev = malloc(sizeof(ft800_handle_t));
        if (!pdev) {
            ESP_LOGE(LOG_TAG, "Failed to allocate memory for device");
            pdev = NULL;
            return ESP_ERR_NO_MEM;
        }
        pdev->spi = NULL;
        pdev->cs_pin = cs;
        pdev->int_pin = int_pin;
    }
    
    // 전역 변수 설정
    *get_driver_dev_ptr() = pdev;
    
    spi_bus_config_t buscfg = {
        .miso_io_num = FT800_SPI_MISO_PIN,
        .mosi_io_num = mosi,
        .sclk_io_num = sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = FT800_SPI_INIT_SPEED_HZ, // 5MHz로 낮춤
        .mode = 0,
        .spics_io_num = cs,
        .queue_size = 3,
    };
    
    // SPI 버스 초기화 (이미 초기화된 경우 무시)
    esp_err_t ret = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(LOG_TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // SPI 디바이스 추가
    ret = spi_bus_add_device(host, &devcfg, &pdev->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    pdev->cs_pin = cs;
    pdev->int_pin = int_pin;
    
    ESP_LOGI(LOG_TAG, "FT800 initialization completed successfully");
    return ESP_OK;
}

ft800_handle_t *get_driver_dev(void) {
    return driver_dev;
}
ft800_handle_t **get_driver_dev_ptr(void) {
    return &driver_dev;
}



/*
    Function: HOST_CMD_WRITE
    ARGS:     CMD:  5 bit Command
             
    Description: Writes Command to FT800
*/
void HOST_CMD_WRITE(uint8_t CMD)
{
  uint8_t buf[3] = { CMD|0x40, 0x00, 0x00 };
  ft800_spi_transfer(driver_dev, buf, NULL, 3);
}

void HOST_CMD_ACTIVE(void)
{
  ft800_read8(driver_dev, 0x000000);
}

/*
    Function: HOST_MEM_WR8
    ARGS:     addr: 24 Bit Command Address 
              data: 8bit Data Byte

    Description: Writes 1 byte of data to addr
*/
void HOST_MEM_WR8(uint32_t addr, uint8_t data)
{
  ft800_write8(driver_dev, addr, data); 
}

/*
    Function: HOST_MEM_WR16
    ARGS:     addr: 24 Bit Command Address 
              data: 16bit (2 bytes)

    Description: Writes 2 bytes of data to addr
*/
void HOST_MEM_WR16(uint32_t addr, uint32_t data)
{
  ft800_write16(driver_dev, addr, data);
}

/*
    Function: HOST_MEM_WR32
    ARGS:     addr: 24 Bit Command Address 
              data: 32bit (4 bytes)

    Description: Writes 4 bytes of data to addr
*/
void HOST_MEM_WR32(uint32_t addr, uint32_t data)
{
  ft800_write32(driver_dev, addr, data);
}

/*
    Function: HOST_MEM_RD8
    ARGS:     addr: 24 Bit Command Address 

    Description: Returns 1 byte of data from addr
*/
uint8_t HOST_MEM_RD8(uint32_t addr)
{
  return ft800_read8(driver_dev, addr);
}

/*
    Function: HOST_MEM_RD16
    ARGS:     addr: 24 Bit Command Address 

    Description: Returns 2 byte of data from addr in a 32bit variable
*/
uint32_t HOST_MEM_RD16(uint32_t addr)
{
  return ft800_read16(driver_dev, addr);
}

/*
    Function: HOST_MEM_RD32
    ARGS:     addr: 24 Bit Command Address 

    Description: Returns 4 byte of data from addr in a 32bit variable
*/
uint32_t HOST_MEM_RD32(uint32_t addr)
{
  return ft800_read32(driver_dev, addr);
}



/*** CMD Functions *****************************************************************/
uint8_t cmd_execute(uint32_t data)
{
	uint32_t cmdBufferRd = 0;
    uint32_t cmdBufferWr = 0;
    
    cmdBufferRd = HOST_MEM_RD32(REG_CMD_READ);
    cmdBufferWr = HOST_MEM_RD32(REG_CMD_WRITE);
    
    uint32_t cmdBufferDiff = cmdBufferWr-cmdBufferRd;
    
    if( (4096-cmdBufferDiff) > 4)
    {
        HOST_MEM_WR32(RAM_CMD + cmdBufferWr, data);
        HOST_MEM_WR32(REG_CMD_WRITE, cmdBufferWr + 4);
        return 1;
    }
    return 0;
}

uint8_t cmd(uint32_t data)
{
	for(int8_t tryCount = 10; tryCount > 0; --tryCount)
	{
		if(cmd_execute(data)) { return 1; }
	}
	return 0;
}

uint8_t cmd_ready(void)
{
    uint32_t cmdBufferRd = HOST_MEM_RD32(REG_CMD_READ);
    uint32_t cmdBufferWr = HOST_MEM_RD32(REG_CMD_WRITE);
    
    return (cmdBufferRd == cmdBufferWr) ? 1 : 0;
}

/*** Track *************************************************************************/
void cmd_track(int16_t x, int16_t y, int16_t w, int16_t h, int16_t tag)
{
    cmd(CMD_TRACK);
    cmd( ((uint32_t)y<<16)|(x & 0xffff) );
    cmd( ((uint32_t)h<<16)|(w & 0xffff) );
    cmd( (uint32_t)tag );
}

/*** Draw Spinner ******************************************************************/
void cmd_spinner(int16_t x, int16_t y, uint16_t style, uint16_t scale)
{    
    cmd(CMD_SPINNER);
    cmd( ((uint32_t)y<<16)|(x & 0xffff) );
    cmd( ((uint32_t)scale<<16)|style );
    
}

/*** Draw Slider *******************************************************************/
void cmd_slider(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t options, uint16_t val, uint16_t range)
{
	cmd(CMD_SLIDER);
	cmd( ((uint32_t)y<<16)|(x & 0xffff) );
	cmd( ((uint32_t)h<<16)|(w & 0xffff) );
	cmd( ((uint32_t)val<<16)|(options & 0xffff) );
	cmd( (uint32_t)range );
}

/*** Draw Text *********************************************************************/
void cmd_text(int16_t x, int16_t y, int16_t font, uint16_t options, const char* str)
{
	/* 	
		i: data pointer
		q: str  pointer
		j: loop counter
	*/
	
	uint16_t i,j,q;
	const uint16_t length = strlen(str);
	if(!length) return ;	
	
	uint32_t* data = (uint32_t*) calloc((length/4)+1, sizeof(uint32_t));
	
	q = 0;
	for(i=0; i<(length/4); ++i, q=q+4)
	{
		data[i] = (uint32_t)str[q+3]<<24 | (uint32_t)str[q+2]<<16 | (uint32_t)str[q+1]<<8 | (uint32_t)str[q];
	}
	for(j=0; j<(length%4); ++j, ++q)
	{
		data[i] |= (uint32_t)str[q] << (j*8);
	}
	
	cmd(CMD_TEXT);
	cmd( ((uint32_t)y<<16)|(x & 0xffff) );
    cmd( ((uint32_t)options<<16)|(font & 0xffff) );
	for(j=0; j<(length/4)+1; ++j)
	{
		cmd(data[j]);
	}
	free(data);
}

/*** Draw Button *******************************************************************/
void cmd_button(int16_t x, int16_t y, int16_t w, int16_t h, int16_t font, uint16_t options, const char* str)
{	
	/* 	
		i: data pointer
		q: str  pointer
		j: loop counter
	*/
	
	uint16_t i,j,q;
	const uint16_t length = strlen(str);
	if(!length) return ;
	
	uint32_t* data = (uint32_t*) calloc((length/4)+1, sizeof(uint32_t));
	
	q = 0;
	for(i=0; i<(length/4); ++i, q=q+4)
	{
		data[i] = (uint32_t)str[q+3]<<24 | (uint32_t)str[q+2]<<16 | (uint32_t)str[q+1]<<8 | (uint32_t)str[q];
	}
	for(j=0; j<(length%4); ++j, ++q)
	{
		data[i] |= (uint32_t)str[q] << (j*8);
	}
	
	cmd(CMD_BUTTON);
	cmd( ((uint32_t)y<<16)|(x & 0xffff) );
	cmd( ((uint32_t)h<<16)|(w & 0xffff) );
    cmd( ((uint32_t)options<<16)|(font & 0xffff) );
	for(j=0; j<(length/4)+1; ++j)
	{
		cmd(data[j]);
	}
	free(data);
}

/*** Draw Keyboard *****************************************************************/
void cmd_keys(int16_t x, int16_t y, int16_t w, int16_t h, int16_t font, uint16_t options, const char* str)
{
	/* 	
		i: data pointer
		q: str  pointer
		j: loop counter
	*/
	
	uint16_t i,j,q;
	const uint16_t length = strlen(str);
	if(!length) return ;
	
	uint32_t* data = (uint32_t*) calloc((length/4)+1, sizeof(uint32_t));
	
	q = 0;
	for(i=0; i<(length/4); ++i, q=q+4)
	{
		data[i] = (uint32_t)str[q+3]<<24 | (uint32_t)str[q+2]<<16 | (uint32_t)str[q+1]<<8 | (uint32_t)str[q];
	}
	for(j=0; j<(length%4); ++j, ++q)
	{
		data[i] |= (uint32_t)str[q] << (j*8);
	}
	
	cmd(CMD_KEYS);
	cmd( ((uint32_t)y<<16)|(x & 0xffff) );
	cmd( ((uint32_t)h<<16)|(w & 0xffff) );
    cmd( ((uint32_t)options<<16)|(font & 0xffff) );
	for(j=0; j<(length/4)+1; ++j)
	{
		cmd(data[j]);
	}
	free(data);
}

/*** Write zero to a block of memory ***********************************************/
void cmd_memzero(uint32_t ptr, uint32_t num)
{
	cmd(CMD_MEMZERO);
	cmd(ptr);
	cmd(num);
}

/*** Set FG color ******************************************************************/
void cmd_fgcolor(uint32_t c)
{
	cmd(CMD_FGCOLOR);
	cmd(c);
}

/*** Set BG color ******************************************************************/
void cmd_bgcolor(uint32_t c)
{
	cmd(CMD_BGCOLOR);
	cmd(c);
}

/*** Set Gradient color ************************************************************/
void cmd_gradcolor(uint32_t c)
{
	cmd(CMD_GRADCOLOR);
	cmd(c);
}

/*** Draw Gradient *****************************************************************/
void cmd_gradient(int16_t x0, int16_t y0, uint32_t rgb0, int16_t x1, int16_t y1, uint32_t rgb1)
{
	cmd(CMD_GRADIENT);
	cmd( ((uint32_t)y0<<16)|(x0 & 0xffff) );
	cmd(rgb0);
	cmd( ((uint32_t)y1<<16)|(x1 & 0xffff) );
	cmd(rgb1);
}

/*** Matrix Functions **************************************************************/
void cmd_loadidentity(void)
{
	cmd(CMD_LOADIDENTITY);
}

void cmd_setmatrix(void)
{
	cmd(CMD_SETMATRIX);
}

void cmd_rotate(int32_t angle)
{
	cmd(CMD_ROTATE);
	cmd(angle);
}

void cmd_translate(int32_t tx, int32_t ty)
{
	cmd(CMD_TRANSLATE);
	cmd(tx);
	cmd(ty);
}


uint8_t spi_speedup(void)
{
    ft800_handle_t *pdev = get_driver_dev();
    spi_host_device_t host = SPI2_HOST;
    return ft800_spi_speedup(pdev, host);
}
extern void ft800_isr_handler(void* arg);

/* Init function for an 5" LCD display */
uint8_t initFT800(void)
{   
	ft800_handle_t *pdev = get_driver_dev();
    /*
    // CH423 테스트 실행
    uint8_t ch423_data;
    esp_err_t ret = test_ch423_circuit_simple(&ch423_data);
    if (ret == ESP_OK) {
        ESP_LOGI(LOG_TAG, "CH423 test passed, data: 0x%02X", ch423_data);
    } else {
        ESP_LOGE(LOG_TAG, "CH423 test failed: %s", esp_err_to_name(ret));
    }
    vTaskDelay(pdMS_TO_TICKS(100));*/

    // FT800 초기화 (새로운 핀 매핑 사용)
    esp_err_t ret = ft800_init_with_int(pdev, SPI2_HOST, 
                                    FT800_SPI_MOSI_PIN, 
                                    FT800_SPI_SCK_PIN, 
                                    FT800_SPI_SS_PIN, 
                                    FT800_INT_PIN);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "FT800 init failed: %s", esp_err_to_name(ret));
        return 1;
    }
	pdev = get_driver_dev();

	//WAKE
	HOST_CMD_ACTIVE();
    vTaskDelay(pdMS_TO_TICKS(100));

	//Ext Clock
	HOST_CMD_WRITE(CMD_CLKEXT);         // Send CLK_EXT Command (0x44)

	//PLL (48M) Clock
	HOST_CMD_WRITE(CMD_CLK48M);         // Send CLK_48M Command (0x62)

	//Read Dev ID
	uint8_t dev_id = HOST_MEM_RD8(REG_ID);      // Read device id
	if(dev_id != 0x7C)                  // Device ID should always be 0x7C
	{   
        ESP_LOGE(LOG_TAG, "FT800 device ID mismatch: 0x%02X", dev_id);
		return 1;
	}

	HOST_MEM_WR8(REG_GPIO, 0x00);			// Set REG_GPIO to 0 to turn off the LCD DISP signal
	HOST_MEM_WR8(REG_PCLK, 0x00);      		// Pixel Clock Output disable

	HOST_MEM_WR16(REG_HCYCLE, 548);         // Set H_Cycle to 548
	HOST_MEM_WR16(REG_HOFFSET, 43);         // Set H_Offset to 43
	HOST_MEM_WR16(REG_HSYNC0, 0);           // Set H_SYNC_0 to 0
	HOST_MEM_WR16(REG_HSYNC1, 41);          // Set H_SYNC_1 to 41
	HOST_MEM_WR16(REG_VCYCLE, 292);         // Set V_Cycle to 292
	HOST_MEM_WR16(REG_VOFFSET, 12);         // Set V_OFFSET to 12
	HOST_MEM_WR16(REG_VSYNC0, 0);           // Set V_SYNC_0 to 0
	HOST_MEM_WR16(REG_VSYNC1, 10);          // Set V_SYNC_1 to 10
	HOST_MEM_WR8(REG_SWIZZLE, 0);           // Set SWIZZLE to 0
	HOST_MEM_WR8(REG_PCLK_POL, 1);          // Set PCLK_POL to 1
	HOST_MEM_WR8(REG_CSPREAD, 1);           // Set CSPREAD to 1
	HOST_MEM_WR16(REG_HSIZE, 480);          // Set H_SIZE to 480
	HOST_MEM_WR16(REG_VSIZE, 272);          // Set V_SIZE to 272

	/* configure touch & audio */
	//HOST_MEM_WR8(REG_TOUCH_MODE, 0x03);     	//set touch on: continous
	//HOST_MEM_WR8(REG_TOUCH_ADC_MODE, 0x01); 	//set touch mode: differential
	//HOST_MEM_WR8(REG_TOUCH_OVERSAMPLE, 0x0F); 	//set touch oversampling to max
	//HOST_MEM_WR16(REG_TOUCH_RZTHRESH, 5000);	//set touch resistance threshold

	//HOST_MEM_WR8(REG_VOL_SOUND, 0xFF);      	
	//HOST_MEM_WR16(REG_SOUND, (0x6C<< 8) | 0x41);      	//C8 MIDI xylophone
	//HOST_MEM_WR8(REG_PLAY, 0);      	

	/* write first display list */
	HOST_MEM_WR32(RAM_DL+0, CLEAR_COLOR_RGB(128,128,128));  // Set Initial Color to BLACK
	HOST_MEM_WR32(RAM_DL+4, CLEAR(1,1,1));            // Clear to the Initial Color
	HOST_MEM_WR32(RAM_DL+8, DISPLAY());               // End Display List

	HOST_MEM_WR8(REG_DLSWAP, DLSWAP_FRAME);           // Make this display list active on the next frame

	HOST_MEM_WR8(REG_GPIO_DIR, 0x80);                 // Set Disp GPIO Direction 
	HOST_MEM_WR8(REG_GPIO, 0x80);                     // Enable Disp (if used)
	//HOST_MEM_WR16(REG_PWM_HZ, 0x00FA);                // Backlight PWM frequency
	//HOST_MEM_WR8(REG_PWM_DUTY, 0x80);                 // Backlight PWM duty    

	HOST_MEM_WR8(REG_PCLK, 0x05);                     // After this display is visible on the LCD

    spi_speedup();

	return 0;
}

/* Clear Screen */
void clrscr(void)
{
	cmd(CMD_DLSTART);
	cmd(CLEAR_COLOR_RGB(0,0,0));
	cmd(CLEAR(1,1,1));
	cmd(DISPLAY());
	cmd(CMD_SWAP);
}

/* Demo Screen */
void lcd_start_screen(uint32_t frames)
{
    uint32_t color = COLOR_RGB((int)(128.f+127.f*(float)cos(frames/320.f*8)), (int)(128.f+127.f*(float)cos(frames/480.f*8)),(int)(128.f+127.f*(float)cos(frames/270.f*8)));
    
	cmd(CMD_DLSTART);
	cmd(CLEAR_COLOR_RGB(0,0,0));
	cmd(CLEAR(1,1,1));//0xA1E1FF
	cmd_gradient(0,0,~color, 0,250,0x000080);

	cmd(color);
    cmd(POINT_SIZE(320)); // set point size to 20 pixels in radius
    cmd(BEGIN(POINTS)); // start drawing points
    cmd(VERTEX2II((int)(240.f+240*(float)cos(frames/480.f*8)), (int)(136.f+136.f*(float)sin(frames/272.f*8)), 0, 0)); // red point
    cmd(END());

	cmd(COLOR_RGB(255,255,255));
	cmd_text(10,245, 27,0, "Designed by: Akos Pasztor");
	cmd_text(470,250, 26,OPT_RIGHTX, "http://akospasztor.com");

	cmd(COLOR_RGB(0xDE,0x00,0x08));
	cmd_text(240,40, 31,OPT_CENTERX, "FT800 Demo");

	cmd(COLOR_RGB(255,255,255));
	//cmd(TAG(1));

	/*if(!button)
	{
		cmd_fgcolor(0x228B22);
		cmd_button(130,150, 220,48, 28,0, "Tap to Continue");
	}
	else
	{
		cmd_fgcolor(0x0A520A);
		cmd_button(130,150, 220,48, 28,OPT_FLAT, "Tap to Continue");
	}
    */
}