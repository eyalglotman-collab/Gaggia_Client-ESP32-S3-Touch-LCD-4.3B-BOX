# LVGL Integration with Toggle Button for ESP32-S3-Touch-LCD-4.3b-Box

**Hardware**: ESP32-S3-Touch-LCD-4.3b-Box  
**Display**: 480x480 IPS RGB LCD (16-bit color)  
**Touch**: GT911 Capacitive Touchscreen (I2C)

---

## ✅ Project Structure

### Core LVGL Components
- **lvgl_port.h / lvgl_port.c** - LVGL porting layer with direct-mode rendering
- **ui_screen.h / ui_screen.c** - UI with toggle button widget
- **hardware_init.h / hardware_init.c** - Complete LCD & touch hardware setup

### Key Files
```
main/
├── Eyal_espresso_ESP32_main.c      (main entry point)
├── lvgl_port.h/c                    (LVGL framework)
├── ui_screen.h/c                    (UI widgets - toggle button)
├── hardware_init.h/c                (LCD + touch drivers)  ✨ NEW
└── CMakeLists.txt
```

---

## 🎯 Features Implemented

### 1. **RGB LCD Display (480x480)**
```c
- 16-bit RGB565 color format
- Pixel clock: 21 MHz
- Double buffering for flicker-free rendering
- Direct mode with tear-free rendering
- Backlight control via GPIO
```

**Pin Configuration:**
```
Data pins:    D0-D15 on GPIO 1-15, 21, 45-48
Sync signals: HSYNC=39, VSYNC=40, DE=41, PCLK=42
Reset:        GPIO 2
Backlight:    GPIO 10
```

### 2. **GT911 Capacitive Touch (I2C)**
```c
- I2C address: 0x5D (91 decimal)
- I2C frequency: 400 kHz
- Interrupt pin: GPIO 37
- Reset pin: GPIO 38
```

**Pin Configuration:**
```
I2C SDA:  GPIO 19
I2C SCL:  GPIO 20
INT:      GPIO 37
RST:      GPIO 38
```

### 3. **LVGL UI with Toggle Button**
- **State logging**: 
  - `I (xxx) ui_screen: brew ON` when toggle activated
  - `I (xxx) ui_screen: brew OFF` when toggle deactivated
- **Visual feedback**: ON/OFF label updates in real-time
- **Dimensions**: 480x480 resolution, fully utilizing the display

---

## ⚙️ Build & Flash

### Prerequisites
```bash
# Set target to ESP32-S3
idf.py set-target esp32s3

# Configure (optional - defaults already set in sdkconfig.defaults)
idf.py menuconfig
```

### Build & Flash
```bash
# Build the project
idf.py build

# Flash to ESP32-S3
idf.py -p COM3 flash

# Monitor serial output
idf.py -p COM3 monitor
```

Replace `COM3` with your actual serial port (or `/dev/ttyUSB0` on Linux).

---

## 📊 Expected Serial Output

```
I (234) hw_init: === Hardware Initialization ===
I (234) hw_init: Board: ESP32-S3-Touch-LCD-4.3b-Box
I (234) hw_init: Display: 480x480 IPS RGB LCD
I (234) hw_init: Touch: GT911 Capacitive
I (245) hw_init: Backlight initialized
I (256) hw_init: I2C bus initialized (SDA:19, SCL:20)
I (345) hw_init: RGB LCD Panel initialized (480x480, 16-bit)
I (456) hw_init: GT911 Touch Controller initialized
I (456) hw_init: === Hardware Initialization Complete ===
I (567) lv_port: Create LVGL task
I (578) ui_screen: UI screen created successfully
I (789) ui_screen: brew OFF  <- Initial state
I (1234) ui_screen: brew ON  <- When you tap the toggle
I (5678) ui_screen: brew OFF <- When you tap again
```

---

## 🔧 Hardware Pin Mapping

### Display Pins (RGB565)
| Function | GPIO | Description |
|----------|------|-------------|
| D0  | 8  | Data bit 0 |
| D1  | 3  | Data bit 1 |
| ... | ...| ... |
| D15 | 14 | Data bit 15 |
| HSYNC | 39 | Horizontal sync |
| VSYNC | 40 | Vertical sync |
| DE | 41 | Data enable |
| PCLK | 42 | Pixel clock (~21MHz) |
| RST | 2 | LCD reset |
| BL | 10 | Backlight enable |

### Touch Panel (I2C)
| Function | GPIO | Details |
|----------|------|---------|
| SDA | 19 | I2C data |
| SCL | 20 | I2C clock |
| INT | 37 | Touch interrupt |
| RST | 38 | Touch reset |
| ADDR | 0x5D | I2C address |

---

## 📋 Configuration Files

### sdkconfig.defaults
Contains all required settings:
```ini
# Chip & Memory
CONFIG_IDF_TARGET="esp32s3"
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y

# RGB LCD
CONFIG_BSP_LCD_RGB_BUFFER_NUMS=2
CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y
CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y

# LVGL
CONFIG_LV_COLOR_16_SWAP=y
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2
CONFIG_LV_USE_SWITCH=y

# Touch
CONFIG_BSP_DISPLAY_WIDTH=480
CONFIG_BSP_DISPLAY_HEIGHT=480
```

### idf_component.yml
Component dependencies:
```yaml
dependencies:
  lvgl/lvgl: "^9.0.0"
  espressif/esp_lcd_touch_gt911: ">=1.0.0"
  espressif/esp_lcd_panel_rgb: ">=1.0.0"
  idf: ">=5.0.0"
```

---

## 🚀 How It Works

1. **Startup Sequence:**
   - Initialize FreeRTOS & ESP-IDF
   - Initialize backlight GPIO
   - Initialize I2C bus (SDA=19, SCL=20)
   - Create RGB LCD panel (480x480, 21MHz)
   - Initialize GT911 touch via I2C
   - Initialize LVGL with display & touch
   - Create UI screen with toggle button

2. **LVGL Rendering:**
   - LVGL task runs at 3kHz (2ms tick)
   - Direct mode rendering minimizes latency
   - Double-buffering prevents flicker
   - Touch input handled by GT911 driver

3. **Toggle Button Logic:**
   - User touches toggle button on display
   - GT911 sends touch coordinates over I2C
   - LVGL detects button press
   - Callback logs "brew ON" or "brew OFF"
   - Label updates to show current state

---

## 🔍 Debugging

### To view LVGL debug logs:
```bash
idf.py -p COM3 monitor | grep -E "(lv_port|ui_screen|hw_init)"
```

### To check component build:
```bash
idf.py fullclean
idf.py build -v  # Verbose output
```

### Common Issues:

| Issue | Solution |
|-------|----------|
| Display shows garbage | Check pin definitions match your board; verify RGB panel init |
| Touch not responding | Check I2C pins (SDA/SCL), GT911 address 0x5D, I2C frequency 400kHz |
| LVGL crashes on init | Increase SPIRAM allocation; check sdkconfig.defaults |
| Compilation errors | Verify esp_lcd_panel_rgb & esp_lcd_touch_gt911 components installed |

---

## 📚 References

- [ESP-IDF LCD Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/lcd.html)
- [LVGL Documentation](https://docs.lvgl.io/)
- [GT911 Touch Datasheet](https://www.goodix.com/)
- [Example Project](C:\Espressif\ESP32-S3-Touch_DEMO_FILES\ESP-IDF\08_lvgl_Porting)

---

**Status**: ✅ Ready to build and flash  
**Last Updated**: March 2, 2026

