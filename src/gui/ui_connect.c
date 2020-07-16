#include "ui_connect.h"

#include "guilib.h"
#include "ime.h"

#include "ui_settings.h"

#include "../connection.h"
#include "../configuration.h"
#include "../video.h"
#include "../config.h"
#include "../util.h"
#include "../device.h"

#include "client.h"
#include "discover.h"
#include "../platform.h"

#include "../power/vita.h"
#include "../input/vita.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>

#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <vita2d.h>

struct {
  int menu_top_index;
  int menu_bottom_index;
} applist;

SERVER_DATA server;
PAPP_LIST server_applist;

int ui_reconnect();

int get_app_id(PAPP_LIST list, char *name) {
  while (list != NULL) {
    if (strcmp(list->name, name) == 0)
      return list->id;

    list = list->next;
  }
  return -1;
}

int get_app_name(PAPP_LIST list, int id, char *name) {
  while (list != NULL) {
    if (list->id == id) {
      strcpy(name, list->name);
      return 1;
    }

    list = list->next;
  }

  return 0;
}

void ui_connect_stream(int appId) {
  // TODO support force controller id
  int ret = gs_start_app(&server, &config.stream, appId, config.sops, config.localaudio, 1);
  if (ret < 0) {
    if (ret == GS_NOT_SUPPORTED_4K)
      display_error("Server doesn't support 4K\n");
    else if (ret == GS_NOT_SUPPORTED_MODE)
      display_error("Server doesn't support %dx%d (%d fps)\n", config.stream.width, config.stream.height, config.stream.fps);
    else
      display_error("Errorcode starting app: %d\n", ret);

    return;
  }

  enum platform system = VITA;
  int drFlags = 0;

  if (config.fullscreen)
    drFlags |= DISPLAY_FULLSCREEN;

  DECODER_RENDERER_CALLBACKS *video_callback = platform_get_video(system);
  if (config.enable_ref_frame_invalidation) {
    video_callback->capabilities |= CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC;
  } else {
    video_callback->capabilities &= ~CAPABILITY_REFERENCE_FRAME_INVALIDATION_AVC;
  }

  ret = LiStartConnection(&server.serverInfo, &config.stream, &connection_callbacks,
                          video_callback, platform_get_audio(system),
                          NULL, drFlags, NULL, 0);

  if (ret == 0) {
    server.currentGame = appId;
  } else {
    char *stage;
    switch (connection_failed_stage) {
      case STAGE_PLATFORM_INIT: stage = "Platform init"; break;
      case STAGE_NAME_RESOLUTION: stage = "Name resolution"; break;
      case STAGE_RTSP_HANDSHAKE: stage = "RTSP handshake"; break;
      case STAGE_CONTROL_STREAM_INIT: stage = "Control stream init"; break;
      case STAGE_VIDEO_STREAM_INIT: stage = "Video stream init"; break;
      case STAGE_AUDIO_STREAM_INIT: stage = "Audio stream init"; break;
      case STAGE_CONTROL_STREAM_START: stage = "Control stream start"; break;
      case STAGE_VIDEO_STREAM_START: stage = "Video stream start"; break;
      case STAGE_AUDIO_STREAM_START: stage = "Audio stream start"; break;
      case STAGE_INPUT_STREAM_START: stage = "Input stream start"; break;
    }

    display_error("Failed to start stream: error code %d\nFailed stage: %s\n(error code %d)",
                  ret, stage, connection_failed_stage_code);
    return;
  }
}

enum {
  CONNECT_PAIRUNPAIR = 13,
  CONNECT_DISCONNECT,
  CONNECT_QUITAPP
};

#define QUIT_RELOAD 2

int ui_connect_loop(int id, void *context, const input_data *input) {
  int status = connection_get_status();

  if (status == LI_DISCONNECTED) {
    if(!ui_reconnect())
      goto disconnect;
    else
      return QUIT_RELOAD;
  }

  menu_entry *menu = context;
  for (int i = applist.menu_top_index; i < applist.menu_bottom_index; i += 1) {
    //menu[i].disabled = (server.currentGame != 0);
  }

  if ((input->buttons & config.btn_confirm) == 0 || input->buttons & SCE_CTRL_HOLD) {
    return 0;
  }

  int ret;

  switch (id) {
    case CONNECT_PAIRUNPAIR:
      if (server.paired) {
        flash_message("Unpairing...");
        ret = gs_unpair(&server);
        if (ret == GS_OK) {
          if (connection_terminate()) {
            display_error("Reconnect failed: %d", -1);
            return 0;
          }
          return QUIT_RELOAD;
        }
        display_error("Unpairing failed: %d", ret);
        return 0;
      }

      char pin[5];
      char message[256];
      sprintf(pin, "%d%d%d%d",
              (int)rand() % 10, (int)rand() % 10, (int)rand() % 10, (int)rand() % 10);
      flash_message("Please enter the following PIN\non the target PC:\n\n%s", pin);

      ret = gs_pair(&server, &pin[0]);
      if (ret == 0) {
        connection_paired();
        if (connection_terminate()) {
          display_error("Reconnect failed: %d", -2);
          return 0;
        }
        return QUIT_RELOAD;
      }
      display_error("Pairing failed: %d", ret);
      return 0;

    case CONNECT_DISCONNECT:
      goto disconnect;

    case CONNECT_QUITAPP:
      flash_message("Quitting...");
      ret = gs_quit_app(&server);
      if (ret == GS_OK) {
        connection_paired();
        server.currentGame = 0;
        sceKernelDelayThread(1000 * 1000);
        return QUIT_RELOAD;
      }
      display_error("Quitting failed: %d", ret);
      return 0;

    // application launcher / resume
    default:
      vitapower_config(config);
      vitainput_config(config);

      switch (status) {
        case LI_MINIMIZED:
          if (server.currentGame == id) {
            sceKernelDelayThread(500 * 1000);
            connection_resume();
            break;
          }
          // TODO: stop previous stream
        case LI_PAIRED:
          {
            if(server.currentGame != 0 && server.currentGame != id)
            {
              flash_message("Quitting running application...");
              ret = gs_quit_app(&server);
              if (ret != GS_OK) {
                display_error("Failed to exit running application: %d", ret);
                return 0;
              }

              sceKernelDelayThread(1000 * 1000);
            }

            connection_paired();
            sceKernelDelayThread(1000 * 1000);
            flash_message("Stream starting...");
            ui_connect_stream(id);
          }
          break;
      }

mainloop:
      while (connection_is_connected()) {
        sceKernelDelayThread(500 * 1000);
      }

      int status = connection_get_status();

      if (status == LI_DISCONNECTED) {
        if(!ui_reconnect())
          goto disconnect;
        else
          return QUIT_RELOAD;
      }

      return QUIT_RELOAD;
  }

disconnect:
  flash_message("Disconnecting...");
  connection_terminate();
  sceKernelDelayThread(1000 * 1000);
  return 1;
}

int ui_connect(char *name, char *address) {
  int ret;
  if (!connection_is_ready()) {
    flash_message("Connecting to:\n %s...", address);

    char key_dir[4096];
    sprintf(key_dir, "%s/%s", config.key_dir, name);

    ret = gs_init(&server, address, key_dir, 0, true);
    if (ret == GS_OUT_OF_MEMORY) {
      display_error("Not enough memory");
      return 0;
    } else if (ret == GS_INVALID) {
      display_error("Invalid data received from server: %s\n", address, gs_error);
      return 0;
    } else if (ret == GS_UNSUPPORTED_VERSION) {
      if (!config.unsupported_version) {
        display_error("Unsupported version: %s\n", gs_error);
        return 0;
      }
    } else if (ret == GS_ERROR) {
      display_error("Gamestream error: %s\n", gs_error);
      return 0;
    } else if (ret != GS_OK) {
      display_error("Can't connect to server\nIP: %s\nError: %s\nReturn Code: %s", address, gs_error, ret);
      return 0;
    }

    server.serverName = name;

    connection_reset();
  }
  return 1;
}

int ui_connected_menu() {
  int ret;
  int app_count = 0;
  if (server.paired) {
    ret = gs_applist(&server, &server_applist);
    if (ret != GS_OK) {
      display_error("Can't get applist!\n%d\n%s", ret, gs_error);
      return 0;
    }

    if (server_applist != NULL) {
      sort_app_list(server_applist);
      PAPP_LIST list = server_applist;
      while (list) {
        list = list->next;
        app_count += 1;
      }
    }
  }

  // current menu = 11 + app_count. but little more alloc ;)
  struct menu_entry menu[app_count + 16];

  int idx = 0;

#define MENU_CATEGORY(NAME) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .disabled = true, .separator = true }; \
    idx++; \
  } while (0)
#define MENU_ENTRY(ID, NAME) \
  do { \
    menu[idx] = (menu_entry) { .name = (NAME), .id = (ID) }; \
    idx++; \
  } while(0)
#define MENU_MESSAGE(MESSAGE, COLOR) \
  do { \
    menu[idx] = (menu_entry) { .name = (MESSAGE), .disabled = true, .color = (COLOR) }; \
    idx++; \
  } while(0)

  //header
  //MENU
  MENU_MESSAGE("Connected to the server:", 0xffffffff);
  char server_info[256];
  snprintf(server_info, 256,
           "IP: %s, GPU %s, GFE %s",
           server.serverInfo.address,
           server.gpuType,
           server.serverInfo.serverInfoGfeVersion);

  MENU_MESSAGE(server_info, 0xffffffff);
  MENU_MESSAGE("", 0);

  if (!server.paired) {
    // pairing
    MENU_CATEGORY("Not paired");
    MENU_ENTRY(CONNECT_PAIRUNPAIR, "Pair");

    MENU_ENTRY(CONNECT_DISCONNECT, "Disconnect");
  } else {
    // current stream
    if (server.currentGame != 0) {
      char current_appname[256];
      char current_status[256];

      if (!get_app_name(server_applist, server.currentGame, current_appname)) {
        strcpy(current_appname, "unknown");
      }
      sprintf(current_status, "Streaming %s", current_appname);

      MENU_CATEGORY(current_status);
      MENU_ENTRY(server.currentGame, "Resume");
      MENU_ENTRY(CONNECT_QUITAPP, "Quit");
    }

    // pairing
    MENU_CATEGORY("Paired");
    connection_paired();
    // FIXME: unpair not work
    // MENU_ENTRY(CONNECT_PAIRUNPAIR, "Unpair");

    MENU_ENTRY(CONNECT_DISCONNECT, "Disconnect");

    // app list
    if (server_applist != NULL) {
      MENU_CATEGORY("Applications");

      applist.menu_top_index = idx;

      PAPP_LIST list = server_applist;
      while (list) {
        MENU_ENTRY(list->id, list->name);
        list = list->next;
      }

      applist.menu_bottom_index = idx;
    } else {
      applist.menu_top_index = -1;
    }
  }

  return display_menu(menu, idx, NULL, &ui_connect_loop, NULL, NULL, &menu);
}

int ui_reconnect()
{
  int tries = 0;
  int conn_stat = 0;
  int ret = 1;

  connection_terminate();

  sceKernelDelayThread(1000 * 1000);
    
  do {
    flash_message("Connection broken. Attempting to reestablish...\nAttempt %d", tries + 1);

    if(ui_connect(server.serverName, server.serverInfo.address)) break;

    sceKernelDelayThread(1000 * 1000);
    ++tries;
  } while(tries < 3);

  if(connection_get_status() == LI_DISCONNECTED || connection_paired())
    ret = 0;

  return ret;
}

device_info_t* ui_connect_and_pairing(device_info_t *info) {
  flash_message("Test connecting to:\n %s...", info->internal);
  char key_dir[4096];
  sprintf(key_dir, "%s/%s", config.key_dir, info->name);
  sceIoMkdir(key_dir, 0777);

  int ret = gs_init(&server, info->internal, key_dir, 0, true);

  if (ret == GS_OUT_OF_MEMORY) {
    display_error("Not enough memory");
    return NULL;
  } else if (ret == GS_INVALID) {
    display_error("Invalid data received from server: %s\n", info->internal, gs_error);
    return NULL;
  } else if (ret == GS_UNSUPPORTED_VERSION) {
    if (!config.unsupported_version) {
      display_error("Unsupported version: %s\n", gs_error);
      return NULL;
    }
  } else if (ret == GS_ERROR) {
    display_error("Gamestream error: %s\n", gs_error);
    return NULL;
  } else if (ret != GS_OK) {
    display_error("Can't connect to server\n%s", info->internal);
    return NULL;
  }

  connection_reset();

  device_info_t *p = append_device(info);
  if (p == NULL) {
    display_error("Can't add device list\n%s", info->name);
    return NULL;
  }

  info = p;
  // connectable address
  save_device_info(info);

  if (server.paired) {
    // no more need, move next action
    goto paired;
  }

  char pin[5];
  char message[256];
  sprintf(pin, "%d%d%d%d",
          (int)rand() % 10, (int)rand() % 10, (int)rand() % 10, (int)rand() % 10);
  flash_message("Please enter the following PIN\non the target PC:\n\n%s", pin);

  ret = gs_pair(&server, pin);
  if (ret != GS_OK) {
    display_error("Pairing failed: %d", ret);
    connection_terminate();
    return NULL;
  }

paired:
  connection_paired();

  info->paired = true;
  save_device_info(info);

  if (connection_terminate()) {
    display_error("Reconnect failed: %d", -2);
    return info;
  }

  return info;
}

void ui_connect_resume() {
  while (ui_connected_menu() == QUIT_RELOAD);
}

void ui_connect_manual() {
  device_info_t info = {0};
  if (ime_dialog_string(info.name, "Enter Name:", "") != 0) {
    return;
  }
  if (ime_dialog_string(info.internal, "Enter IP or Address:", "") != 0) {
    return;
  }
  ui_connect_and_pairing(&info);
}

bool check_connection(const char *name, char *addr) {
  // someone already connected
  if (connection_is_ready()) {
    return false;
  }

  flash_message("Check connecting to:\n %s...", addr);

  char key_dir[4096];
  sprintf(key_dir, "%s/%s", config.key_dir, name);

  if (gs_init(&server, addr, key_dir, 0, true) != GS_OK) {
    return false;
  }
  connection_terminate();
  return true;
}

void ui_connect_paired_device(device_info_t *info) {
  if (!info->paired) {
    display_error("Unpaired device\n%s", info->name);
    return;
  }

  char *addr = NULL;
  if (info->prefer_external && info->external && check_connection(info->name, info->external)) {
    addr = info->external;
  } else if (info->internal && check_connection(info->name, info->internal)) {
    addr = info->internal;
  } else if (info->external && check_connection(info->name, info->external)) {
    addr = info->external;
  }
  info->prefer_external = addr == info->external;
  save_device_info(info);
  
  if (addr == NULL) {
    display_error("Can't connect to server\n%s", info->name);
    return;
  }

  if (!ui_connect(info->name, addr)) {
    return;
  }

  while (ui_connected_menu() == QUIT_RELOAD);
}

bool ui_connect_connected() {
  return connection_is_ready();
}

void ui_connect_address(char *addr) {
  strcpy(addr, server.serverInfo.address);
}
