# ESP32-S3-Touch-LCD-4.3b-Box Hardware Quick Reference

## Device Specifications
```
Display: 480×480 IPS RGB LCD, 4.3-inch
Touch:   GT911 Capacitive Capacitive Touchscreen
MCU:     ESP32-S3 (240 MHz dual core, 512KB SRAM, 8/16MB PSRAM)
Colors:  RGB565 (16-bit)
```

---

## Pin Assignments

### RGB LCD Data Bus (16-bit RGB565)
```
GPIO  Signal  GPIO  Signal  GPIO  Signal
8     D0      15    D8      3     D1
46    D2      9     D3      1     D4
5     D5      6     D6      7     D7
16    D9      4     D10     45    D11
48    D12     47    D13     21    D14
14    D15
```

### LCD Control Signals
| Signal | GPIO | Function |
|--------|------|----------|
| HSYNC | 39 | Horizontal synchronization |
| VSYNC | 40 | Vertical synchronization |
| DE | 41 | Data enable |
| PCLK | 42 | Pixel clock (21 MHz) |
| RST | 2 | Display reset |
| BL | 10 | Backlight enable (active HIGH) |

### I2C Bus (Touch Controller)
| Signal | GPIO | Function |
|--------|------|----------|
| SDA | 19 | I2C data |
| SCL | 20 | I2C clock |

### Touch Controller (GT911)
| Signal | GPIO | Notes |
|--------|------|-------|
| INT | 37 | Interrupt (active LOW) |
| RST | 38 | Reset (active LOW) |
| ADDR | 0x5D | I2C address |
| FREQ | 400kHz | I2C frequency |

---

## Display Timing Parameters
```
Resolution:      480×480 pixels
Pixel Clock:     21 MHz
HSYNC Pulse:     10 clocks
HSYNC Back Porch: 8 clocks
HSYNC Front Porch: 8 clocks
VSYNC Pulse:     10 lines
VSYNC Back Porch: 8 lines
VSYNC Front Porch: 8 lines
```

---

## Software Components
| Component | Version | Source |
|-----------|---------|--------|
| LVGL | 9.0.0+ | lvgl/lvgl |
| ESP-LCD-Panel-RGB | 1.0+ | espressif/esp_lcd_panel_rgb |
| ESP-LCD-Touch-GT911 | 1.0+ | espressif/esp_lcd_touch_gt911 |
| ESP-IDF | 5.0+ | Espressif |

---

## Configuration References
- LCD Config:   `hardware_init.c` (lines 1-60)
- Touch Config: `hardware_init.c` (lines 100-150)
- LVGL Config:  `lvgl_port.h` (all defines)
- Board Config: `sdkconfig.defaults`

---

## Power & Performance
- Operating Voltage: 3.3V
- Typical Power: ~500mA (display + touch)
- CPU Frequency: 240 MHz (both cores)
- PSRAM: Octamode (133 MHz)
- Backlight Current: ~200mA (full brightness)

---

## Troubleshooting Quick Links
1. **No Display Output** → Check PCLK (GPIO 42), HSYNC (39), VSYNC (40)
2. **Distorted Image** → Verify data pins D0-D15 match GPIO assignments
3. **Touch Not Working** → Check I2C SDA (19), SCL (20), GT911 address 0x5D
4. **LVGL Crashes** → Increase SPIRAM/SRAM in sdkconfig, check buffer sizes
5. **Compile Errors** → Run `idf.py fullclean`, then `idf.py build`

---

**File Source**: hardware_init.c, lvgl_port.h, sdkconfig.defaults  
**Last Updated**: March 2, 2026
