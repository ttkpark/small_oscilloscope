#include "ft800.h"
#include <string.h>
#include "esp_log.h"

#define TAG "FT800"

static void ft800_spi_transfer(ft800_handle_t *dev, const uint8_t *tx, uint8_t *rx, size_t len) {
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    spi_device_polling_transmit(dev->spi, &t);
}

void ft800_write8(ft800_handle_t *dev, uint32_t addr, uint8_t data) {
    uint8_t buf[4] = { (addr >> 16) & 0x3F, (addr >> 8) & 0xFF, addr & 0xFF, data };
    ft800_spi_transfer(dev, buf, NULL, 4);
}
void ft800_write16(ft800_handle_t *dev, uint32_t addr, uint16_t data) {
    uint8_t buf[5] = { (addr >> 16) & 0x3F, (addr >> 8) & 0xFF, addr & 0xFF, data & 0xFF, (data >> 8) & 0xFF };
    ft800_spi_transfer(dev, buf, NULL, 5);
}
void ft800_write32(ft800_handle_t *dev, uint32_t addr, uint32_t data) {
    uint8_t buf[7] = { (addr >> 16) & 0x3F, (addr >> 8) & 0xFF, addr & 0xFF,
                       data & 0xFF, (data >> 8) & 0xFF, (data >> 16) & 0xFF, (data >> 24) & 0xFF };
    ft800_spi_transfer(dev, buf, NULL, 7);
}

uint8_t ft800_read8(ft800_handle_t *dev, uint32_t addr) {
    uint8_t tx[4] = { ((addr >> 16) & 0x3F) | 0x80, (addr >> 8) & 0xFF, addr & 0xFF, 0 };
    uint8_t rx[4] = {0};
    ft800_spi_transfer(dev, tx, rx, 4);
    return rx[3];
}
uint16_t ft800_read16(ft800_handle_t *dev, uint32_t addr) {
    uint8_t tx[5] = { ((addr >> 16) & 0x3F) | 0x80, (addr >> 8) & 0xFF, addr & 0xFF, 0, 0 };
    uint8_t rx[5] = {0};
    ft800_spi_transfer(dev, tx, rx, 5);
    return rx[3] | (rx[4] << 8);
}
uint32_t ft800_read32(ft800_handle_t *dev, uint32_t addr) {
    uint8_t tx[7] = { ((addr >> 16) & 0x3F) | 0x80, (addr >> 8) & 0xFF, addr & 0xFF, 0, 0, 0, 0 };
    uint8_t rx[7] = {0};
    ft800_spi_transfer(dev, tx, rx, 7);
    return rx[3] | (rx[4] << 8) | (rx[5] << 16) | (rx[6] << 24);
}

esp_err_t ft800_init(ft800_handle_t *dev, spi_host_device_t host, int mosi, int sclk, int cs) {
    spi_bus_config_t buscfg = {
        .miso_io_num = 19,
        .mosi_io_num = 23,
        .sclk_io_num = 18,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 64,
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 1000000, // 1MHz
        .mode = 0,
        .spics_io_num = cs,
        .queue_size = 3,
    };
    
    // SPI 버스 초기화 (이미 초기화된 경우 무시)
    esp_err_t ret = spi_bus_initialize(host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // SPI 디바이스 추가
    ret = spi_bus_add_device(host, &devcfg, &dev->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    dev->cs_pin = cs;
    
    // FT800 초기화 시퀀스 (프로그래머 가이드 기반)
    ESP_LOGI(TAG, "Starting FT800 initialization sequence...");
    
    ret = ft800_check_id(dev);
    while (ret != ESP_OK)
    {
        ret = ft800_check_id(dev);
        vTaskDelay(0);
    }
    
    // 1. 칩 ID 확인
    ret = ft800_check_id(dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "FT800 chip ID check failed");
        return ret;
    }
    
    // 2. CPU 리셋
    ft800_write8(dev, FT800_REG_CPURESET, 0x01);
    vTaskDelay(pdMS_TO_TICKS(100));  // 100ms 대기
    
    // 3. 클럭 설정 (48MHz)
    ft800_write16(dev, FT800_REG_CLOCK, 0x60);  // 48MHz
    
    // 4. 디스플레이 타이밍 설정 (480x272 해상도)
    ft800_write16(dev, FT800_REG_HCYCLE, 548);      // Horizontal total cycle
    ft800_write16(dev, FT800_REG_HOFFSET, 43);      // Horizontal offset
    ft800_write16(dev, FT800_REG_HSIZE, 480);       // Horizontal size
    ft800_write16(dev, FT800_REG_HSYNC0, 0);        // Horizontal sync start
    ft800_write16(dev, FT800_REG_HSYNC1, 41);       // Horizontal sync end
    
    ft800_write16(dev, FT800_REG_VCYCLE, 292);      // Vertical total cycle
    ft800_write16(dev, FT800_REG_VOFFSET, 12);      // Vertical offset
    ft800_write16(dev, FT800_REG_VSIZE, 272);       // Vertical size
    ft800_write16(dev, FT800_REG_VSYNC0, 0);        // Vertical sync start
    ft800_write16(dev, FT800_REG_VSYNC1, 10);       // Vertical sync end
    
    // 5. 디스플레이 설정
    ft800_write8(dev, FT800_REG_DLSWAP, 0x00);      // Display list swap
    ft800_write8(dev, FT800_REG_ROTATE, 0x00);      // Rotation
    ft800_write8(dev, FT800_REG_OUTBITS, 0x00);     // Output bits
    ft800_write8(dev, FT800_REG_DITHER, 0x00);      // Dither
    ft800_write8(dev, FT800_REG_SWIZZLE, 0x00);     // Swizzle
    ft800_write8(dev, FT800_REG_CSPREAD, 0x00);     // CSpread
    
    // 6. 백라이트 초기화 (수정된 레지스터 주소)
    ft800_write8(dev, FT800_REG_PWM_HZ, 0x00);      // PWM frequency
    ft800_write8(dev, FT800_REG_PWM_DUTY, 0x80);    // PWM duty cycle (50%)
    
    // 7. 초기 디스플레이 리스트 설정
    ft800_write32(dev, FT800_RAM_DL, 0x00000000);   // Clear color
    ft800_write32(dev, FT800_RAM_DL + 4, 0x00000000); // Clear stencil
    ft800_write32(dev, FT800_RAM_DL + 8, 0x00000000); // Clear tag
    ft800_write32(dev, FT800_RAM_DL + 12, 0x00000000); // Display
    
    // 8. 인터럽트 설정
    ft800_write8(dev, FT800_REG_INT_EN, 0x00);      // Disable interrupts
    ft800_write8(dev, FT800_REG_INT_FLAGS, 0xFF);   // Clear interrupt flags
    
    // 9. GPIO 설정
    ft800_write8(dev, FT800_REG_GPIO_DIR, 0x00);    // All GPIO as inputs
    ft800_write8(dev, FT800_REG_GPIO, 0x00);        // GPIO state
    
    // 10. 오디오 설정
    ft800_write8(dev, FT800_REG_VOL_PB, 0x00);      // Playback volume
    ft800_write8(dev, FT800_REG_VOL_SOUND, 0xFF);   // Sound volume
    ft800_write8(dev, FT800_REG_SOUND, 0x00);       // Sound
    ft800_write8(dev, FT800_REG_PLAY, 0x00);        // Play
    
    ESP_LOGI(TAG, "FT800 initialization completed successfully");
    return ESP_OK;
}

esp_err_t ft800_check_id(ft800_handle_t *dev) {
    uint8_t chip_id = ft800_read8(dev, FT800_REG_ID);
    ESP_LOGI(TAG, "FT800 Chip ID: 0x%02X (expected: 0x%02X)", chip_id, FT800_CHIP_ID);
    
    if (chip_id == FT800_CHIP_ID) {
        ESP_LOGI(TAG, "FT800 chip ID verification successful");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "FT800 chip ID verification failed");
        return ESP_FAIL;
    }
}

void ft800_cmd_start(ft800_handle_t *dev) {
    ft800_write32(dev, FT800_RAM_CMD, FT800_CMD_DLSTART);
}
void ft800_cmd_swap(ft800_handle_t *dev) {
    ft800_write32(dev, FT800_RAM_CMD + 4, FT800_CMD_SWAP);
}
void ft800_cmd_clear(ft800_handle_t *dev, uint8_t c, uint8_t s, uint8_t t) {
    uint32_t cmd = FT800_CMD_CLEAR | (c ? 0x01 : 0) | (s ? 0x02 : 0) | (t ? 0x04 : 0);
    ft800_write32(dev, FT800_RAM_CMD + 8, cmd);
}
void ft800_cmd_display(ft800_handle_t *dev) {
    ft800_write32(dev, FT800_RAM_CMD + 12, FT800_CMD_DISPLAY);
}
void ft800_cmd_text(ft800_handle_t *dev, int x, int y, int font, int options, const char *str) {
    uint32_t addr = FT800_RAM_CMD + 16;
    ft800_write32(dev, addr, FT800_CMD_TEXT);
    ft800_write32(dev, addr + 4, (y << 16) | (x & 0xFFFF));
    ft800_write32(dev, addr + 8, (options << 16) | (font & 0xFFFF));
    uint8_t buf[128] = {0};
    strncpy((char*)buf, str, 127);
    for (int i = 0; i < 32; i++) {
        ft800_write32(dev, addr + 12 + i * 4, ((uint32_t*)buf)[i]);
    }
}

void ft800_dl_start(ft800_handle_t *dev) {
    ft800_write32(dev, FT800_RAM_DL, 0x00000000);
}
void ft800_dl_end(ft800_handle_t *dev) {
    ft800_write32(dev, FT800_RAM_DL + 4, 0x00000000);
}
void ft800_dl_point(ft800_handle_t *dev, int x, int y, int size, uint32_t color) {
    // 구현 필요: Display List에 점 추가
}
void ft800_dl_line(ft800_handle_t *dev, int x0, int y0, int x1, int y1, uint32_t color) {
    // 구현 필요: Display List에 라인 추가
}
void ft800_dl_rect(ft800_handle_t *dev, int x0, int y0, int x1, int y1, uint32_t color) {
    // 구현 필요: Display List에 사각형 추가
}

void ft800_backlight(ft800_handle_t *dev, uint8_t duty) {
    // PWM 듀티 사이클 설정 (0-128)
    if (duty > 128) duty = 128;
    ft800_write8(dev, FT800_REG_PWM_DUTY, duty);
}

void ft800_backlight_on(ft800_handle_t *dev) {
    // 백라이트 최대 밝기로 켜기
    ft800_write8(dev, FT800_REG_PWM_DUTY, 128);
}

void ft800_backlight_off(ft800_handle_t *dev) {
    // 백라이트 끄기
    ft800_write8(dev, FT800_REG_PWM_DUTY, 0);
} 