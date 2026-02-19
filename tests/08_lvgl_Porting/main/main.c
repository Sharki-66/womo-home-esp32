/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "waveshare_rgb_lcd_port.h"
#include "gas_bottle.h"

void create_gas_display()
{
    // Schwarzer Hintergrund
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
    
    // Titel
    lv_obj_t *title = lv_label_create(lv_scr_act());
    lv_label_set_text(title, "WoMo Gasflaschen Monitor");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
    
    // Gasflasche 1 (links)
    gas_bottle_t *bottle1 = gas_bottle_create(lv_scr_act(), 150, 80);
    gas_bottle_set_weight(bottle1, 8.2f);  // 75% voll
    
    // Gasflasche 2 (rechts) 
    gas_bottle_t *bottle2 = gas_bottle_create(lv_scr_act(), 400, 80);
    gas_bottle_set_weight(bottle2, 3.1f);  // 28% voll (niedrig)
    
    // Labels für die Flaschen
    lv_obj_t *label1 = lv_label_create(lv_scr_act());
    lv_label_set_text(label1, "Flasche 1");
    lv_obj_set_style_text_color(label1, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label1, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(label1, 160, 220);
    
    lv_obj_t *label2 = lv_label_create(lv_scr_act());
    lv_label_set_text(label2, "Flasche 2");
    lv_obj_set_style_text_color(label2, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label2, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(label2, 410, 220);
    
    // Status Info
    lv_obj_t *info = lv_label_create(lv_scr_act());
    lv_label_set_text(info, "Grün: >60%  |  Gelb: 30-60%  |  Rot: <30%");
    lv_obj_set_style_text_color(info, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_12, 0);
    lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -20);
}

void app_main()
{
    waveshare_esp32_s3_rgb_lcd_init(); // Initialize the Waveshare ESP32-S3 RGB LCD 
    // wavesahre_rgb_lcd_bl_on();  //Turn on the screen backlight 
    // wavesahre_rgb_lcd_bl_off(); //Turn off the screen backlight 
    
    ESP_LOGI(TAG, "Display LVGL demos");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (lvgl_port_lock(-1)) {
        // Statt Demo - zeige Gasflaschen
        create_gas_display();
        
        // Alternativ: Original Demo
        // lv_demo_widgets();
        
        // Release the mutex
        lvgl_port_unlock();
    }
}
