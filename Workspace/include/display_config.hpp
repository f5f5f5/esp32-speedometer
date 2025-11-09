#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Display configuration class for GC9A01 round LCD
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_GC9A01      _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;

public:
        LGFX(void) {
                // Establish pin mapping once so both bus and panel blocks can see PIN_* constants.
                #if defined(LCD_MAP_A)
                    const int PIN_SCLK = 10, PIN_MOSI = 11, PIN_DC = 8, PIN_CS = 9, PIN_RST = 14, PIN_BL = 2;
                #elif defined(LCD_MAP_B)
                    const int PIN_SCLK = 12, PIN_MOSI = 11, PIN_DC = 10, PIN_CS = 13, PIN_RST = 14, PIN_BL = 2;
                #elif defined(LCD_MAP_C)
                    const int PIN_SCLK = 5,  PIN_MOSI = 4,  PIN_DC = 6, PIN_CS = 7, PIN_RST = 8,  PIN_BL = 2;
                #elif defined(LCD_MAP_D)
                    const int PIN_SCLK = 10, PIN_MOSI = 11, PIN_DC = 9, PIN_CS = 8, PIN_RST = 14, PIN_BL = 2;
                #else
                    // Default to MAP_A
                    const int PIN_SCLK = 10, PIN_MOSI = 11, PIN_DC = 8, PIN_CS = 9, PIN_RST = 14, PIN_BL = 2;
                #endif

                { // Bus configuration
                        auto cfg = _bus_instance.config();

            // Set bus pins
            cfg.pin_sclk = PIN_SCLK;
            cfg.pin_mosi = PIN_MOSI;
            cfg.pin_miso = -1;
            cfg.pin_dc   = PIN_DC;

                        // Allow selecting SPI host via macro (default SPI2_HOST)
                        #if defined(LCD_USE_SPI3)
                            cfg.spi_host = SPI3_HOST;
                        #else
                            cfg.spi_host = SPI2_HOST;
                        #endif
                        // Try SPI mode; wrong mode can bit-shift color fields. Default 0; try 3 if colors look mixed.
                        #if defined(LCD_SPI_MODE_3)
                            cfg.spi_mode = 3;
                        #else
                            cfg.spi_mode = 0;
                        #endif
                        // Use conservative write freq during bring-up to avoid sampling issues.
                        cfg.freq_write = 20000000;
            cfg.freq_read  = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        { // Display panel configuration
            auto cfg = _panel_instance.config();
            
            cfg.pin_cs   = PIN_CS;    // Chip select
            cfg.pin_rst  = PIN_RST;   // Reset
            cfg.pin_busy = -1;   // Not used
            
            // Panel parameters
            cfg.panel_width  = 240;
            cfg.panel_height = 240;
            cfg.offset_x     = 0;
            cfg.offset_y     = 0;
            cfg.offset_rotation = 0;
                        cfg.dummy_read_pixel = 8;    // Common GC9A01 default
                        cfg.dummy_read_bits  = 0;

                        // Color profile selector: choose invert/rgb order via build flags
                        // - LCD_COLOR_PROFILE_0: invert=true,  rgb_order=true
                        // - LCD_COLOR_PROFILE_1: invert=false, rgb_order=false (swapped R/B)
                        // - LCD_COLOR_PROFILE_2: invert=true,  rgb_order=false
                        // - LCD_COLOR_PROFILE_3: invert=false, rgb_order=false (default)
                        #if defined(LCD_COLOR_PROFILE_0)
                            cfg.invert = true;  cfg.rgb_order = true;
                        #elif defined(LCD_COLOR_PROFILE_1)
                            cfg.invert = false; cfg.rgb_order = false;
                        #elif defined(LCD_COLOR_PROFILE_2)
                            cfg.invert = true;  cfg.rgb_order = false;
                        #else
                            cfg.invert = false; cfg.rgb_order = false;
                        #endif

                        cfg.dlen_16bit = false; // Use 8-bit transfers (panel handles 16-bit color)
            cfg.bus_shared = true;

            _panel_instance.config(cfg);

        { // Backlight configuration
            auto lcfg = _light_instance.config();
            lcfg.pin_bl = PIN_BL;
            lcfg.invert = false;
            lcfg.freq = 5000;
            lcfg.pwm_channel = 7;
            _light_instance.config(lcfg);
            _panel_instance.setLight(&_light_instance);
        }
        }

        setPanel(&_panel_instance);
    }
};