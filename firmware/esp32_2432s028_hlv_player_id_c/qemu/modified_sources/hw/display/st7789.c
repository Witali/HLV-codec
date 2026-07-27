/*
 * Sitronix ST7789 SPI LCD controller.
 *
 * This model implements the command subset used by the ESP-IDF ST7789
 * panel driver and a 320x240 landscape console for the ESP32-2432S028.
 *
 * Copyright (c) 2026 HLV codec contributors
 *
 * This code is licensed under the GPL version 2 or later.
 */

#include "qemu/osdep.h"
#include "hw/ssi/ssi.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "qom/object.h"

#define ST7789_WIDTH  320
#define ST7789_HEIGHT 240

#define ST7789_CMD_SWRESET 0x01
#define ST7789_CMD_SLPIN   0x10
#define ST7789_CMD_SLPOUT  0x11
#define ST7789_CMD_INVOFF  0x20
#define ST7789_CMD_INVON   0x21
#define ST7789_CMD_DISPOFF 0x28
#define ST7789_CMD_DISPON  0x29
#define ST7789_CMD_CASET   0x2a
#define ST7789_CMD_RASET   0x2b
#define ST7789_CMD_RAMWR   0x2c
#define ST7789_CMD_MADCTL  0x36
#define ST7789_CMD_COLMOD  0x3a
#define ST7789_CMD_RAMCTRL 0xb0

#define ST7789_MADCTL_BGR  0x08
#define ST7789_RAMCTRL_LITTLE_ENDIAN 0x08

#define TYPE_ST7789 "st7789"
OBJECT_DECLARE_SIMPLE_TYPE(ST7789State, ST7789)

struct ST7789State {
    SSIPeripheral parent_obj;
    QemuConsole *con;

    uint8_t command;
    uint8_t argument[4];
    uint8_t argument_index;
    uint8_t pixel_byte;
    uint8_t madctl;
    uint8_t colmod;
    uint8_t ramctrl;
    bool dc;
    bool backlight;
    bool display_on;
    bool sleeping;
    bool inverted;
    bool little_endian;
    bool redraw;

    uint16_t column;
    uint16_t row;
    uint16_t column_start;
    uint16_t column_end;
    uint16_t row_start;
    uint16_t row_end;
    uint16_t framebuffer[ST7789_WIDTH * ST7789_HEIGHT];
};

static void st7789_reset_state(ST7789State *s, bool clear_framebuffer)
{
    s->command = 0;
    s->argument_index = 0;
    s->pixel_byte = 0;
    s->madctl = 0;
    s->colmod = 0x55;
    s->ramctrl = 0xf0;
    s->display_on = false;
    s->sleeping = true;
    s->inverted = false;
    s->little_endian = false;
    s->column = 0;
    s->row = 0;
    s->column_start = 0;
    s->column_end = ST7789_WIDTH - 1;
    s->row_start = 0;
    s->row_end = ST7789_HEIGHT - 1;
    if (clear_framebuffer) {
        memset(s->framebuffer, 0, sizeof(s->framebuffer));
    }
    s->redraw = true;
}

static void st7789_advance_address(ST7789State *s)
{
    s->column++;
    if (s->column > s->column_end) {
        s->column = s->column_start;
        s->row++;
        if (s->row > s->row_end) {
            s->row = s->row_start;
        }
    }
}

static void st7789_write_pixel_byte(ST7789State *s, uint8_t data)
{
    uint16_t pixel;

    if (s->pixel_byte == 0) {
        s->argument[0] = data;
        s->pixel_byte = 1;
        return;
    }

    if (s->little_endian) {
        pixel = s->argument[0] | ((uint16_t)data << 8);
    } else {
        pixel = ((uint16_t)s->argument[0] << 8) | data;
    }
    s->pixel_byte = 0;

    if (s->column < ST7789_WIDTH && s->row < ST7789_HEIGHT) {
        s->framebuffer[s->row * ST7789_WIDTH + s->column] = pixel;
        s->redraw = true;
    }
    st7789_advance_address(s);
}

static unsigned st7789_argument_count(uint8_t command)
{
    switch (command) {
    case ST7789_CMD_CASET:
    case ST7789_CMD_RASET:
        return 4;
    case ST7789_CMD_MADCTL:
    case ST7789_CMD_COLMOD:
        return 1;
    case ST7789_CMD_RAMCTRL:
        return 2;
    default:
        return 0;
    }
}

static void st7789_apply_arguments(ST7789State *s)
{
    switch (s->command) {
    case ST7789_CMD_CASET:
        s->column_start = ((uint16_t)s->argument[0] << 8) | s->argument[1];
        s->column_end = ((uint16_t)s->argument[2] << 8) | s->argument[3];
        s->column = s->column_start;
        break;
    case ST7789_CMD_RASET:
        s->row_start = ((uint16_t)s->argument[0] << 8) | s->argument[1];
        s->row_end = ((uint16_t)s->argument[2] << 8) | s->argument[3];
        s->row = s->row_start;
        break;
    case ST7789_CMD_MADCTL:
        s->madctl = s->argument[0];
        s->redraw = true;
        break;
    case ST7789_CMD_COLMOD:
        s->colmod = s->argument[0];
        break;
    case ST7789_CMD_RAMCTRL:
        s->ramctrl = s->argument[1];
        s->little_endian =
            (s->ramctrl & ST7789_RAMCTRL_LITTLE_ENDIAN) != 0;
        break;
    default:
        break;
    }
}

static void st7789_command(ST7789State *s, uint8_t command)
{
    s->command = command;
    s->argument_index = 0;
    s->pixel_byte = 0;

    switch (command) {
    case ST7789_CMD_SWRESET:
        st7789_reset_state(s, true);
        break;
    case ST7789_CMD_SLPIN:
        s->sleeping = true;
        s->redraw = true;
        break;
    case ST7789_CMD_SLPOUT:
        s->sleeping = false;
        s->redraw = true;
        break;
    case ST7789_CMD_INVOFF:
        s->inverted = false;
        s->redraw = true;
        break;
    case ST7789_CMD_INVON:
        s->inverted = true;
        s->redraw = true;
        break;
    case ST7789_CMD_DISPOFF:
        s->display_on = false;
        s->redraw = true;
        break;
    case ST7789_CMD_DISPON:
        s->display_on = true;
        s->redraw = true;
        break;
    default:
        break;
    }
}

static uint32_t st7789_transfer(SSIPeripheral *dev, uint32_t data)
{
    ST7789State *s = ST7789(dev);
    uint8_t byte = data;
    unsigned argument_count;

    if (!s->dc) {
        st7789_command(s, byte);
        return 0;
    }

    if (s->command == ST7789_CMD_RAMWR) {
        if ((s->colmod & 0x77) == 0x55) {
            st7789_write_pixel_byte(s, byte);
        }
        return 0;
    }

    argument_count = st7789_argument_count(s->command);
    if (s->argument_index < argument_count) {
        s->argument[s->argument_index++] = byte;
        if (s->argument_index == argument_count) {
            st7789_apply_arguments(s);
        }
    }
    return 0;
}

static uint32_t st7789_rgb_to_surface(DisplaySurface *surface,
                                      uint8_t red, uint8_t green,
                                      uint8_t blue)
{
    switch (surface_bits_per_pixel(surface)) {
    case 8:
        return rgb_to_pixel8(red, green, blue);
    case 15:
        return rgb_to_pixel15(red, green, blue);
    case 16:
        return rgb_to_pixel16(red, green, blue);
    case 24:
        return rgb_to_pixel24(red, green, blue);
    case 32:
        return rgb_to_pixel32(red, green, blue);
    default:
        return 0;
    }
}

static void st7789_store_surface_pixel(uint8_t *dest, int bytes_per_pixel,
                                       uint32_t pixel)
{
    switch (bytes_per_pixel) {
    case 1:
        *dest = pixel;
        break;
    case 2:
        *(uint16_t *)dest = pixel;
        break;
    case 3:
        memcpy(dest, &pixel, 3);
        break;
    case 4:
        *(uint32_t *)dest = pixel;
        break;
    default:
        break;
    }
}

static void st7789_update_display(void *opaque)
{
    ST7789State *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);
    int bytes_per_pixel;
    bool blanked;
    int x;
    int y;

    if (!s->redraw || surface_bits_per_pixel(surface) == 0) {
        return;
    }

    bytes_per_pixel = (surface_bits_per_pixel(surface) + 7) >> 3;
    if (bytes_per_pixel < 1 || bytes_per_pixel > 4) {
        return;
    }
    blanked = !s->backlight || !s->display_on || s->sleeping;

    for (y = 0; y < ST7789_HEIGHT; y++) {
        uint8_t *dest =
            surface_data(surface) + y * surface_stride(surface);

        for (x = 0; x < ST7789_WIDTH; x++) {
            uint16_t color =
                blanked ? 0 : s->framebuffer[y * ST7789_WIDTH + x];
            uint8_t red;
            uint8_t green;
            uint8_t blue;
            uint32_t pixel;

            if (s->inverted && !blanked) {
                color ^= 0xffff;
            }
            red = ((color >> 11) & 0x1f) * 255 / 31;
            green = ((color >> 5) & 0x3f) * 255 / 63;
            blue = (color & 0x1f) * 255 / 31;
            if (s->madctl & ST7789_MADCTL_BGR) {
                uint8_t swap = red;
                red = blue;
                blue = swap;
            }
            pixel = st7789_rgb_to_surface(surface, red, green, blue);
            st7789_store_surface_pixel(dest, bytes_per_pixel, pixel);
            dest += bytes_per_pixel;
        }
    }

    s->redraw = false;
    dpy_gfx_update_full(s->con);
}

static void st7789_invalidate_display(void *opaque)
{
    ST7789State *s = opaque;

    s->redraw = true;
}

static void st7789_dc(void *opaque, int line, int level)
{
    ST7789State *s = opaque;

    s->dc = level != 0;
}

static void st7789_backlight(void *opaque, int line, int level)
{
    ST7789State *s = opaque;
    bool enabled = level != 0;

    if (s->backlight != enabled) {
        s->backlight = enabled;
        s->redraw = true;
    }
}

static int st7789_post_load(void *opaque, int version_id)
{
    st7789_invalidate_display(opaque);
    return 0;
}

static const VMStateDescription vmstate_st7789 = {
    .name = "st7789",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = st7789_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(command, ST7789State),
        VMSTATE_UINT8_ARRAY(argument, ST7789State, 4),
        VMSTATE_UINT8(argument_index, ST7789State),
        VMSTATE_UINT8(pixel_byte, ST7789State),
        VMSTATE_UINT8(madctl, ST7789State),
        VMSTATE_UINT8(colmod, ST7789State),
        VMSTATE_UINT8(ramctrl, ST7789State),
        VMSTATE_BOOL(dc, ST7789State),
        VMSTATE_BOOL(backlight, ST7789State),
        VMSTATE_BOOL(display_on, ST7789State),
        VMSTATE_BOOL(sleeping, ST7789State),
        VMSTATE_BOOL(inverted, ST7789State),
        VMSTATE_BOOL(little_endian, ST7789State),
        VMSTATE_UINT16(column, ST7789State),
        VMSTATE_UINT16(row, ST7789State),
        VMSTATE_UINT16(column_start, ST7789State),
        VMSTATE_UINT16(column_end, ST7789State),
        VMSTATE_UINT16(row_start, ST7789State),
        VMSTATE_UINT16(row_end, ST7789State),
        VMSTATE_UINT16_ARRAY(framebuffer, ST7789State,
                             ST7789_WIDTH * ST7789_HEIGHT),
        VMSTATE_SSI_PERIPHERAL(parent_obj, ST7789State),
        VMSTATE_END_OF_LIST()
    },
};

static const GraphicHwOps st7789_ops = {
    .invalidate = st7789_invalidate_display,
    .gfx_update = st7789_update_display,
};

static void st7789_realize(SSIPeripheral *dev, Error **errp)
{
    DeviceState *device = DEVICE(dev);
    ST7789State *s = ST7789(dev);

    s->con = graphic_console_init(device, 0, &st7789_ops, s);
    qemu_console_resize(s->con, ST7789_WIDTH, ST7789_HEIGHT);
    qdev_init_gpio_in_named(device, st7789_dc, "dc", 1);
    qdev_init_gpio_in_named(device, st7789_backlight, "backlight", 1);
    st7789_reset_state(s, true);
}

static void st7789_reset(DeviceState *device)
{
    ST7789State *s = ST7789(device);

    st7789_reset_state(s, true);
}

static void st7789_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSIPeripheralClass *ssi = SSI_PERIPHERAL_CLASS(klass);

    ssi->realize = st7789_realize;
    ssi->transfer = st7789_transfer;
    ssi->cs_polarity = SSI_CS_LOW;
    dc->vmsd = &vmstate_st7789;
    device_class_set_legacy_reset(dc, st7789_reset);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo st7789_info = {
    .name = TYPE_ST7789,
    .parent = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(ST7789State),
    .class_init = st7789_class_init,
};

static void st7789_register_types(void)
{
    type_register_static(&st7789_info);
}

type_init(st7789_register_types)
