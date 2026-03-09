#include <stdio.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_random.h" 

// Include your custom hardware mapping
#include "pins.h"

static const char *TAG1 = "M5_APP";
static const char *TAG2 = "DISPLAY_TASK";

#define LCD_H_RES 135
#define LCD_V_RES 240

// Screen dimensions for landscape mode
#define SCREEN_W 240
#define SCREEN_H 135

#define LCD_HOST SPI2_HOST

// Upgraded Terminal dimensions for 1x scaling
#define TERMINAL_W 60
#define TERMINAL_H 22

// M5StickC Plus2 Main Button Pin
#define M5_BUTTON_A_PIN 37

// Global handle
extern esp_lcd_panel_handle_t panel_handle; 

// Custom 3x5 micro-font mapping for our 13 specific luminance levels
const uint8_t tiny_font[13][5] = {
    {0, 0, 0, 0, 0}, // 0: ' ' (Space)
    {0, 0, 0, 0, 2}, // 1: .
    {0, 0, 0, 2, 4}, // 2: ,
    {0, 0, 7, 0, 0}, // 3: -
    {0, 5, 2, 0, 0}, // 4: ~
    {0, 2, 0, 2, 0}, // 5: :
    {0, 2, 0, 2, 4}, // 6: ;
    {0, 7, 0, 7, 0}, // 7: =
    {2, 2, 2, 0, 2}, // 8: !
    {5, 2, 5, 0, 0}, // 9: *
    {5, 7, 5, 7, 5}, // 10: #
    {2, 7, 2, 7, 2}, // 11: $
    {7, 5, 5, 7, 0}  // 12: @
};

// Helper function to generate a guaranteed BRIGHT, saturated color
uint16_t get_random_bright_color() {
    uint8_t r = esp_random() % 256;
    uint8_t g = esp_random() % 256;
    uint8_t b = esp_random() % 256;

    // Force max brightness on the dominant channel
    if (r >= g && r >= b) r = 255;
    else if (g >= r && g >= b) g = 255;
    else b = 255;

    // Force high saturation by dropping the weakest channel
    if (r <= g && r <= b) r = 0;
    else if (g <= r && g <= b) g = 0;
    else b = 0;

    // Convert to RGB565 and swap bytes for the SPI display
    uint16_t color565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (color565 >> 8) | (color565 << 8); 
}

void display_task(void *pvParameters)
{
    ESP_LOGI(TAG2,"Initialize SPI bus...");
    spi_bus_config_t buscfg = 
    {
        .sclk_io_num = M5_TFT_SCLK_PIN,
        .mosi_io_num = M5_TFT_MOSI_PIN,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t),
    };

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG2, "Installing panel IO...");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = M5_TFT_DC_PIN,
        .cs_gpio_num = M5_TFT_CS_PIN,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    ESP_LOGI(TAG2, "Installing ST7789 panel driver...");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = M5_TFT_RST_PIN,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16, 
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    // Shift the hardware rendering window
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 40, 53));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true)); 
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG2, "Turning on backlight...");
    gpio_reset_pin(M5_TFT_BACKLIGHT_PIN);
    gpio_set_direction(M5_TFT_BACKLIGHT_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(M5_TFT_BACKLIGHT_PIN, 1);

    // Initialize the main button
    gpio_reset_pin(M5_BUTTON_A_PIN);
    gpio_set_direction(M5_BUTTON_A_PIN, GPIO_MODE_INPUT);
    // Use PULLUP or no pullup depending on your specific board layout; 
    // M5Stick usually has an external pullup on the button, but this is safe.
    gpio_set_pull_mode(M5_BUTTON_A_PIN, GPIO_PULLUP_ONLY);

    float A = 0, B = 0;
    float z[TERMINAL_W * TERMINAL_H];
    uint8_t b[TERMINAL_W * TERMINAL_H];

    // --- DOUBLE BUFFERING ALLOCATION ---
    uint16_t *frame_buffer[2];
    frame_buffer[0] = (uint16_t *)heap_caps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t), MALLOC_CAP_DMA);
    frame_buffer[1] = (uint16_t *)heap_caps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t), MALLOC_CAP_DMA);
    uint8_t buf_idx = 0; 

    // Start with Matrix Green
    uint16_t current_color = 0xFFFF; 
    current_color = (current_color >> 8) | (current_color << 8);
    bool last_btn_state = true;

    while(1) {
        // --- BUTTON HANDLING ---
        bool current_btn_state = gpio_get_level(M5_BUTTON_A_PIN);
        if (last_btn_state && !current_btn_state) {
            current_color = get_random_bright_color();
        }
        last_btn_state = current_btn_state;

        // 1. Clear math buffers and the CURRENT TFT buffer
        memset(b, 0, sizeof(b)); 
        memset(z, 0, sizeof(z));
        memset(frame_buffer[buf_idx], 0, SCREEN_W * SCREEN_H * sizeof(uint16_t)); 

        // 2. The Fast ASCII Math Loop
        for (float j = 0; j < 6.28; j += 0.15) {
            for (float i = 0; i < 6.28; i += 0.08) {
                float c = sin(i), d = cos(j), e = sin(A), f = sin(j), g = cos(A);
                float h = d + 2;
                float D = 1 / (c * h * e + f * g + 5);
                float l = cos(i), m = cos(B), n = sin(B);
                float t = c * h * g - f * e;

                int x = 30 + 24 * D * (l * h * m - t * n);
                int y = 11 + 12 * D * (l * h * n + t * m);
                int o = x + TERMINAL_W * y;

                int N = 8 * ((f * e - c * d * g) * m - c * d * e - f * g - l * d * n);

                if (TERMINAL_H > y && y >= 0 && x >= 0 && TERMINAL_W > x && D > z[o]) {
                    z[o] = D;
                    
                    int luminance = N > 0 ? (N * 12) / 8 : 0;
                    if (luminance > 12) luminance = 12;
                    b[o] = luminance; 
                }
            }
        }

        // 3. The 1x Micro-Rasterizer
        int offset_x = (SCREEN_W - (TERMINAL_W * 4)) / 2; 
        int offset_y = (SCREEN_H - (TERMINAL_H * 6)) / 2; 

        for (int ty = 0; ty < TERMINAL_H; ty++) {
            for (int tx = 0; tx < TERMINAL_W; tx++) {
                uint8_t char_idx = b[tx + TERMINAL_W * ty];
                if (char_idx > 12) char_idx = 12; 
                
                if (char_idx > 0) {
                    for (int py = 0; py < 5; py++) {
                        uint8_t row_bits = tiny_font[char_idx][py];
                        for (int px = 0; px < 3; px++) {
                            if ((row_bits >> (2 - px)) & 1) {
                                int pixel_x = offset_x + (tx * 4) + px;
                                int pixel_y = offset_y + (ty * 6) + py;
                                
                                // Write to the CURRENT buffer
                                frame_buffer[buf_idx][pixel_y * SCREEN_W + pixel_x] = current_color;
                            }
                        }
                    }
                }
            }
        }

        // 4. Blast the CURRENT buffer to the ST7789 via DMA
        esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, SCREEN_W, SCREEN_H, frame_buffer[buf_idx]);

        // 5. Swap the buffer index for the next frame
        buf_idx = !buf_idx;

        A += 0.12; 
        B += 0.06;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    // 1. Initialize Power Hold (CRITICAL)
    gpio_reset_pin(M5_POWER_HOLD_PIN);
    gpio_set_direction(M5_POWER_HOLD_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(M5_POWER_HOLD_PIN, 1);
    ESP_LOGI(TAG1, "Power hold engaged.");

    // Create the display task, pinned to Core 1
    xTaskCreatePinnedToCore(display_task, "DisplayTask", 16384, NULL, 5, NULL, 1);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}