#include <cmath>
#include <cstdio>
#include <cstring>
#include "board_port.h"
#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
lv_obj_t *values[4], *bars[4], *status, *clock_label;
lv_obj_t* content;
double pc_idle_seconds = -1;
uint64_t last_touch = 0;
int brightness = -1;
uint64_t last_packet = 0;
bool connected = false;
lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y,
                const lv_font_t* font, uint32_t color) {
  auto* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}
double number(cJSON* root, const char* key) {
  auto* v = cJSON_GetObjectItemCaseSensitive(root, key);
  return cJSON_IsNumber(v) && std::isfinite(v->valuedouble) ? v->valuedouble : -1;
}
void update(cJSON* root) {
  if (number(root, "v") != 1) return;
  pc_idle_seconds = number(root, "idle_s");
  const char* keys[] = {"cpu", "gpu", "quota5", "quota7"};
  char buf[80];
  for (int i = 0; i < 4; ++i) {
    const double n = number(root, keys[i]);
    if(i < 2) {
      const double t = number(root, i == 0 ? "cpu_temp" : "gpu_temp");
      char load[20], temp[20];
      if(n >= 0 && n <= 100) snprintf(load,sizeof(load),"%.0f%%",n); else strcpy(load,"--%%");
      if(t >= 0 && t < 150) snprintf(temp,sizeof(temp),"%.0f C",t); else strcpy(temp,"-- C");
      snprintf(buf,sizeof(buf),"%s   %s",load,temp);
    } else {
      if(n >= 0 && n <= 100) snprintf(buf,sizeof(buf),"%.0f%% LEFT",n); else strcpy(buf,"UNAVAILABLE");
    }
    lv_label_set_text(values[i],buf);
    lv_bar_set_value(bars[i], n >= 0 && n <= 100 ? static_cast<int>(n) : 0, LV_ANIM_OFF);
  }
  auto* tm = cJSON_GetObjectItemCaseSensitive(root,"time");
  if(cJSON_IsString(tm) && strlen(tm->valuestring) <= 8) lv_label_set_text(clock_label,tm->valuestring);
  double age = number(root,"quota_age");
  if(age >= 0) snprintf(buf,sizeof(buf),"USB LIVE  /  CODEX %.0fm AGO",age/60);
  else strcpy(buf,"USB LIVE  /  CODEX UNAVAILABLE");
  lv_label_set_text(status,buf);
  last_packet = esp_timer_get_time()/1000;
  connected = true;
  char ack[64];
  int len=snprintf(ack,sizeof(ack),"PCMON1 OK brightness=%d\n",brightness);
  usb_serial_jtag_write_bytes(ack,len,0);
}
void protection_tick(uint64_t now) {
  board_port_lock(UINT32_MAX);
  for(auto* input=lv_indev_get_next(nullptr); input; input=lv_indev_get_next(input)) {
    if(lv_indev_get_type(input)==LV_INDEV_TYPE_POINTER && lv_indev_get_state(input)==LV_INDEV_STATE_PRESSED)
      last_touch=now;
  }
  // Minute-by-minute orbit distributes fixed glyphs across neighbouring pixels.
  static const int offsets[9][2]={{0,0},{4,0},{4,4},{0,4},{-4,4},{-4,0},{-4,-4},{0,-4},{4,-4}};
  const unsigned index=(now/60000)%9;
  lv_obj_set_pos(content,offsets[index][0],offsets[index][1]);
  int target=55;
  if(pc_idle_seconds>=120) target=18;
  if(pc_idle_seconds>=600) target=0;
  if(now-last_packet>30000) target=0;
  if(last_touch && now-last_touch<60000) target=55;
  if(target!=brightness) {
    if(board_port_set_brightness(target)==ESP_OK) {
      brightness=target;
      ESP_LOGI("pc_protection","brightness=%d",target);
    }
  }
  board_port_unlock();
}
}
extern "C" void app_main() {
  ESP_ERROR_CHECK(board_port_init());
  board_port_lock(UINT32_MAX);
  auto* screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen,lv_color_hex(0x000000),0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
  content=lv_obj_create(screen);
  lv_obj_remove_style_all(content);
  lv_obj_set_size(content,466,466);
  lv_obj_remove_flag(content,LV_OBJ_FLAG_SCROLLABLE);
  screen=content;
  label(screen,"DESK / MONITOR",137,37,&lv_font_montserrat_20,0x6BE4CE);
  clock_label=label(screen,"--:--",196,67,&lv_font_montserrat_20,0x7D8CA5);
  const char* names[]={"CPU", "GPU", "CODEX / 5H", "CODEX / WEEK"};
  uint32_t colors[]={0x6BE4CE,0x70B7FF,0xBB9DFF,0xFFCA7A};
  for(int i=0;i<4;++i) {
    int y=105+i*72;
    label(screen,names[i],78,y,&lv_font_montserrat_16,colors[i]);
    values[i]=label(screen,"WAITING",225,y-3,&lv_font_montserrat_20,0xF0F5FF);
    bars[i]=lv_bar_create(screen);
    lv_obj_set_pos(bars[i],78,y+30); lv_obj_set_size(bars[i],310,7);
    lv_obj_set_style_bg_color(bars[i],lv_color_hex(0x202A3C),LV_PART_MAIN);
    lv_obj_set_style_bg_color(bars[i],lv_color_hex(colors[i]),LV_PART_INDICATOR);
    lv_bar_set_range(bars[i],0,100);
  }
  status=label(screen,"CONNECT USB / START PC APP",0,409,&lv_font_montserrat_16,0x7D8CA5);
  lv_obj_set_width(status,466); lv_obj_set_style_text_align(status,LV_TEXT_ALIGN_CENTER,0);
  board_port_unlock();
  ESP_ERROR_CHECK(board_port_reveal_display());
  board_port_lock(UINT32_MAX);
  ESP_ERROR_CHECK(board_port_set_brightness(55));
  brightness=55;
  board_port_unlock();
  usb_serial_jtag_driver_config_t cfg{}; cfg.rx_buffer_size=2048; cfg.tx_buffer_size=512;
  ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
  char line[768]; size_t used=0; bool overflow=false;
  while(true) {
    char chunk[128];
    int n=usb_serial_jtag_read_bytes(chunk,sizeof(chunk),pdMS_TO_TICKS(100));
    for(int i=0;i<n;++i) {
      if(chunk[i]=='\n') {
        if(!overflow && used) {
          line[used]=0;
          auto* root=cJSON_Parse(line);
          if(root) { board_port_lock(UINT32_MAX); update(root); board_port_unlock(); cJSON_Delete(root); }
        }
        used=0;overflow=false;
      } else if(used<sizeof(line)-1 && !overflow) line[used++]=chunk[i];
      else overflow=true;
    }
    if(connected && esp_timer_get_time()/1000-last_packet > 7000) {
      board_port_lock(UINT32_MAX);
      lv_label_set_text(status,"PC DISCONNECTED / DATA STALE");
      for(int i=0;i<4;++i) {lv_label_set_text(values[i],"--");lv_bar_set_value(bars[i],0,LV_ANIM_OFF);}
      board_port_unlock(); connected=false;
    }
    protection_tick(esp_timer_get_time()/1000);
  }
}
