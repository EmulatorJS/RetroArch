/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2011-2020 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

// Needed for memfd_create
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* See feature_test_macros(7) */
#endif

#include <stdint.h>
#include <string.h>

#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>

#include <string/stdstring.h>

#ifdef HAVE_LIBDECOR
#include <libdecor.h>
#endif

#include "wayland_common.h"

#include "../input_keymaps.h"
#include "../../frontend/frontend_driver.h"

#define SPLASH_SHM_NAME "retroarch-wayland-vk-splash"

static void keyboard_handle_keymap(void* data,
      struct wl_keyboard* keyboard,
      uint32_t format,
      int fd,
      uint32_t size)
{
   if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
   {
      close(fd);
      return;
   }

#ifdef HAVE_XKBCOMMON
   init_xkb(fd, size);
#endif
   close(fd);
}

static void keyboard_handle_enter(void* data,
      struct wl_keyboard* keyboard,
      uint32_t serial,
      struct wl_surface* surface,
      struct wl_array* keys)
{
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;
   wl->input.keyboard_focus   = true;
}

static void keyboard_handle_leave(void *data,
      struct wl_keyboard *keyboard,
      uint32_t serial,
      struct wl_surface *surface)
{
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;
   wl->input.keyboard_focus   = false;

   // Release all keys
   memset(wl->input.key_state, 0, sizeof(wl->input.key_state));
}

static void keyboard_handle_key(void *data,
      struct wl_keyboard *keyboard,
      uint32_t serial,
      uint32_t time,
      uint32_t key,
      uint32_t state)
{
   int value                  = 1;
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;
   uint32_t keysym            = key;

   /* Handle 'duplicate' inputs that correspond
    * to the same RETROK_* key */
   switch (key)
   {
      case KEY_OK:
      case KEY_SELECT:
         keysym = KEY_ENTER;
      case KEY_EXIT:
         keysym = KEY_CLEAR;
      default:
         break;
   }

   if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
   {
      BIT_SET(wl->input.key_state, keysym);
      value = 1;
   }
   else if (state == WL_KEYBOARD_KEY_STATE_RELEASED)
   {
      BIT_CLEAR(wl->input.key_state, keysym);
      value = 0;
   }

#ifdef HAVE_XKBCOMMON
   if (handle_xkb(keysym, value) == 0)
      return;
#endif
   input_keyboard_event(value,
			input_keymaps_translate_keysym_to_rk(keysym),
         0, 0, RETRO_DEVICE_KEYBOARD);
}

static void keyboard_handle_modifiers(void *data,
      struct wl_keyboard *keyboard,
      uint32_t serial,
      uint32_t modsDepressed,
      uint32_t modsLatched,
      uint32_t modsLocked,
      uint32_t group)
{
#ifdef HAVE_XKBCOMMON
   handle_xkb_state_mask(modsDepressed, modsLatched, modsLocked, group);
#endif
}

void keyboard_handle_repeat_info(void *data,
      struct wl_keyboard *wl_keyboard,
      int32_t rate,
      int32_t delay)
{
   /* TODO: Seems like we'll need this to get
    * repeat working. We'll have to do it on our own. */
}

void gfx_ctx_wl_show_mouse(void *data, bool state)
{
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;
   if (!wl->wl_pointer)
      return;

   if (state)
   {
      struct wl_cursor_image *image = wl->cursor.default_cursor->images[0];
      wl_pointer_set_cursor(wl->wl_pointer, wl->cursor.serial, wl->cursor.surface, image->hotspot_x, image->hotspot_y);
      wl_surface_attach(wl->cursor.surface, wl_cursor_image_get_buffer(image), 0, 0);
      wl_surface_damage(wl->cursor.surface, 0, 0, image->width, image->height);
      wl_surface_commit(wl->cursor.surface);
   }
   else
      wl_pointer_set_cursor(wl->wl_pointer, wl->cursor.serial, NULL, 0, 0);

   wl->cursor.visible = state;
}

static void pointer_handle_enter(void *data,
      struct wl_pointer *pointer,
      uint32_t serial,
      struct wl_surface *surface,
      wl_fixed_t sx,
      wl_fixed_t sy)
{
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

   wl->input.mouse.surface = surface;
   wl->input.mouse.last_x  = wl_fixed_to_int(sx * (wl_fixed_t)wl->buffer_scale);
   wl->input.mouse.last_y  = wl_fixed_to_int(sy * (wl_fixed_t)wl->buffer_scale);
   wl->input.mouse.x       = wl->input.mouse.last_x;
   wl->input.mouse.y       = wl->input.mouse.last_y;
   wl->input.mouse.focus   = true;
   wl->cursor.serial       = serial;

   gfx_ctx_wl_show_mouse(data, wl->cursor.visible);
}

static void pointer_handle_leave(void *data,
      struct wl_pointer *pointer,
      uint32_t serial,
      struct wl_surface *surface)
{
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;
   wl->input.mouse.focus      = false;
   wl->input.mouse.left       = false;
   wl->input.mouse.right      = false;
   wl->input.mouse.middle     = false;

   if (wl->input.mouse.surface == surface)
      wl->input.mouse.surface = NULL;
}

static void pointer_handle_motion(void *data,
      struct wl_pointer *pointer,
      uint32_t time,
      wl_fixed_t sx,
      wl_fixed_t sy)
{
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;
   wl->input.mouse.x          = wl_fixed_to_int(
         (wl_fixed_t)wl->buffer_scale * sx);
   wl->input.mouse.y          = wl_fixed_to_int(
         (wl_fixed_t)wl->buffer_scale * sy);
}

static void pointer_handle_button(void *data,
      struct wl_pointer *wl_pointer,
      uint32_t serial,
      uint32_t time,
      uint32_t button,
      uint32_t state)
{
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

   if (wl->input.mouse.surface != wl->surface)
      return;

   if (state == WL_POINTER_BUTTON_STATE_PRESSED)
   {
      switch (button)
      {
         case BTN_LEFT:
            wl->input.mouse.left = true;

            if (BIT_GET(wl->input.key_state, KEY_LEFTALT))
            {
#ifdef HAVE_LIBDECOR
               libdecor_frame_move(wl->libdecor_frame, wl->seat, serial);
#else
               xdg_toplevel_move(wl->xdg_toplevel, wl->seat, serial);
#endif
            }
            break;
         case BTN_RIGHT:
            wl->input.mouse.right = true;
            break;
         case BTN_MIDDLE:
            wl->input.mouse.middle = true;
            break;
      }
   }
   else
   {
      switch (button)
      {
         case BTN_LEFT:
            wl->input.mouse.left = false;
            break;
         case BTN_RIGHT:
            wl->input.mouse.right = false;
            break;
         case BTN_MIDDLE:
            wl->input.mouse.middle = false;
            break;
      }
   }
}

static void pointer_handle_axis(void *data,
      struct wl_pointer *wl_pointer,
      uint32_t time,
      uint32_t axis,
      wl_fixed_t value) {
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;
   double dvalue = wl_fixed_to_double(value);
   switch (axis) {
      case WL_POINTER_AXIS_VERTICAL_SCROLL:
         if (dvalue < 0) {
            wl->input.mouse.wu = true;
         } else if (dvalue > 0) {
            wl->input.mouse.wd = true;
         }
         break;
      case WL_POINTER_AXIS_HORIZONTAL_SCROLL:
         if (dvalue < 0) {
            wl->input.mouse.wl = true;
         } else if (dvalue > 0) {
            wl->input.mouse.wr = true;
         }
         break;
   }
}

static void touch_handle_down(void *data,
      struct wl_touch *wl_touch,
      uint32_t serial,
      uint32_t time,
      struct wl_surface *surface,
      int32_t id,
      wl_fixed_t x,
      wl_fixed_t y)
{
   int i;
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

   if (wl->num_active_touches < MAX_TOUCHES)
   {
      for (i = 0; i < MAX_TOUCHES; i++)
      {
         /* Use next empty slot */
         if (!wl->active_touch_positions[i].active)
         {
            wl->active_touch_positions[wl->num_active_touches].active = true;
            wl->active_touch_positions[wl->num_active_touches].id     = id;
            wl->active_touch_positions[wl->num_active_touches].x      = (unsigned)
               wl_fixed_to_int(x);
            wl->active_touch_positions[wl->num_active_touches].y      = (unsigned)
               wl_fixed_to_int(y);
            wl->num_active_touches++;
            break;
         }
      }
   }
}

static void reorder_touches(gfx_ctx_wayland_data_t *wl)
{
   int i, j;
   if (wl->num_active_touches == 0)
      return;

   for (i = 0; i < MAX_TOUCHES; i++)
   {
      if (!wl->active_touch_positions[i].active)
      {
         for (j=i+1; j<MAX_TOUCHES; j++)
         {
            if (wl->active_touch_positions[j].active)
            {
               wl->active_touch_positions[i].active =
                  wl->active_touch_positions[j].active;
               wl->active_touch_positions[i].id     =
                  wl->active_touch_positions[j].id;
               wl->active_touch_positions[i].x      = wl->active_touch_positions[j].x;
               wl->active_touch_positions[i].y      = wl->active_touch_positions[j].y;
               wl->active_touch_positions[j].active = false;
               wl->active_touch_positions[j].id     = -1;
               wl->active_touch_positions[j].x      = (unsigned) 0;
               wl->active_touch_positions[j].y      = (unsigned) 0;
               break;
            }

            if (j == MAX_TOUCHES)
               return;
         }
      }
   }
}

static void touch_handle_up(void *data,
      struct wl_touch *wl_touch,
      uint32_t serial,
      uint32_t time,
      int32_t id)
{
   int i;
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

   for (i = 0; i < MAX_TOUCHES; i++)
   {
      if (  wl->active_touch_positions[i].active &&
            wl->active_touch_positions[i].id == id)
      {
         wl->active_touch_positions[i].active = false;
         wl->active_touch_positions[i].id     = -1;
         wl->active_touch_positions[i].x      = (unsigned)0;
         wl->active_touch_positions[i].y      = (unsigned)0;
         wl->num_active_touches--;
      }
   }
   reorder_touches(wl);
}

static void touch_handle_motion(void *data,
      struct wl_touch *wl_touch,
      uint32_t time,
      int32_t id,
      wl_fixed_t x,
      wl_fixed_t y)
{
   int i;
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

   for (i = 0; i < MAX_TOUCHES; i++)
   {
      if (  wl->active_touch_positions[i].active &&
            wl->active_touch_positions[i].id == id)
      {
         wl->active_touch_positions[i].x = (unsigned) wl_fixed_to_int(x);
         wl->active_touch_positions[i].y = (unsigned) wl_fixed_to_int(y);
      }
   }
}

static void touch_handle_frame(void *data,
      struct wl_touch *wl_touch) { }

static void touch_handle_cancel(void *data,
      struct wl_touch *wl_touch)
{
   /* If i understand the spec correctly we have to reset all touches here
    * since they were not ment for us anyway */
   int i;
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

   for (i = 0; i < MAX_TOUCHES; i++)
   {
      wl->active_touch_positions[i].active = false;
      wl->active_touch_positions[i].id     = -1;
      wl->active_touch_positions[i].x      = (unsigned) 0;
      wl->active_touch_positions[i].y      = (unsigned) 0;
   }

   wl->num_active_touches = 0;
}

static void seat_handle_capabilities(void *data,
      struct wl_seat *seat, unsigned caps)
{
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

   if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !wl->wl_keyboard)
   {
      wl->wl_keyboard = wl_seat_get_keyboard(seat);
      wl_keyboard_add_listener(wl->wl_keyboard, &keyboard_listener, wl);
   }
   else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && wl->wl_keyboard)
   {
      wl_keyboard_destroy(wl->wl_keyboard);
      wl->wl_keyboard = NULL;
   }
   if ((caps & WL_SEAT_CAPABILITY_POINTER) && !wl->wl_pointer)
   {
      wl->wl_pointer = wl_seat_get_pointer(seat);
      wl_pointer_add_listener(wl->wl_pointer, &pointer_listener, wl);
   }
   else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && wl->wl_pointer)
   {
      wl_pointer_destroy(wl->wl_pointer);
      wl->wl_pointer = NULL;
   }
   if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !wl->wl_touch)
   {
      wl->wl_touch = wl_seat_get_touch(seat);
      wl_touch_add_listener(wl->wl_touch, &touch_listener, wl);
   }
   else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && wl->wl_touch)
   {
      wl_touch_destroy(wl->wl_touch);
      wl->wl_touch = NULL;
   }

}

static void seat_handle_name(void *data,
      struct wl_seat *seat, const char *name) { }

/* Surface callbacks. */

static void wl_surface_enter(void *data, struct wl_surface *wl_surface,
      struct wl_output *output)
{
    output_info_t *oi;
    gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

    wl->input.mouse.surface = wl_surface;

    /* TODO: track all outputs the surface is on, pick highest scale */

    wl_list_for_each(oi, &wl->all_outputs, link)
    {
       if (oi->output == output)
       {
          wl->current_output    = oi;
          wl->last_buffer_scale = wl->buffer_scale;
          wl->buffer_scale      = oi->scale;
          break;
       }
    };
}

static void wl_nop(void *a, struct wl_surface *b, struct wl_output *c) { }

/* Shell surface callbacks. */
static void xdg_shell_ping(void *data, struct xdg_wm_base *shell, uint32_t serial)
{
    xdg_wm_base_pong(shell, serial);
}

static void handle_surface_config(void *data, struct xdg_surface *surface,
                                  uint32_t serial)
{
    xdg_surface_ack_configure(surface, serial);
}

void handle_toplevel_close(void *data,
      struct xdg_toplevel *xdg_toplevel)
{
	gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;
	command_event(CMD_EVENT_QUIT, NULL);
}

static void display_handle_geometry(void *data,
      struct wl_output *output,
      int x, int y,
      int physical_width, int physical_height,
      int subpixel,
      const char *make,
      const char *model,
      int transform)
{
   output_info_t *oi          = (output_info_t*)data;
   oi->physical_width         = physical_width;
   oi->physical_height        = physical_height;
   oi->make                   = strdup(make);
   oi->model                  = strdup(model);
}

static void display_handle_mode(void *data,
      struct wl_output *output,
      uint32_t flags,
      int width,
      int height,
      int refresh)
{
   (void)output;
   (void)flags;

   output_info_t *oi          = (output_info_t*)data;
   oi->width                  = width;
   oi->height                 = height;
   oi->refresh_rate           = refresh;
}

static void display_handle_done(void *data,
      struct wl_output *output) { }

static void display_handle_scale(void *data,
      struct wl_output *output,
      int32_t factor)
{
   output_info_t *oi = (output_info_t*)data;
   oi->scale = factor;
}

/* Registry callbacks. */
static void registry_handle_global(void *data, struct wl_registry *reg,
      uint32_t id, const char *interface, uint32_t version)
{
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

   RARCH_DBG("[Wayland]: Add global %u, interface %s, version %u\n", id, interface, version);

   if (string_is_equal(interface, "wl_compositor"))
      wl->compositor = (struct wl_compositor*)wl_registry_bind(reg,
            id, &wl_compositor_interface, MIN(version, 4));
   else if (string_is_equal(interface, "wl_output"))
   {
      output_info_t *oi = (output_info_t*)
         calloc(1, sizeof(output_info_t));

      oi->global_id     = id;
      oi->output        = (struct wl_output*)wl_registry_bind(reg,
            id, &wl_output_interface, MIN(version, 2));
      wl_output_add_listener(oi->output, &output_listener, oi);
      wl_list_insert(&wl->all_outputs, &oi->link);
      wl_display_roundtrip(wl->input.dpy);
   }
   else if (string_is_equal(interface, "xdg_wm_base"))
      wl->xdg_shell = (struct xdg_wm_base*)
         wl_registry_bind(reg, id, &xdg_wm_base_interface, MIN(version, 3));
   else if (string_is_equal(interface, "wl_shm"))
      wl->shm = (struct wl_shm*)wl_registry_bind(reg, id, &wl_shm_interface, MIN(version, 1));
   else if (string_is_equal(interface, "wl_seat"))
   {
      wl->seat = (struct wl_seat*)wl_registry_bind(reg, id, &wl_seat_interface, MIN(version, 2));
      wl_seat_add_listener(wl->seat, &seat_listener, wl);
   }
   else if (string_is_equal(interface, "zwp_idle_inhibit_manager_v1"))
      wl->idle_inhibit_manager = (struct zwp_idle_inhibit_manager_v1*)wl_registry_bind(
                                  reg, id, &zwp_idle_inhibit_manager_v1_interface, MIN(version, 1));
   else if (string_is_equal(interface, "zxdg_decoration_manager_v1"))
      wl->deco_manager = (struct zxdg_decoration_manager_v1*)wl_registry_bind(
                                  reg, id, &zxdg_decoration_manager_v1_interface, MIN(version, 1));
}

static void registry_handle_global_remove(void *data,
      struct wl_registry *registry, uint32_t id)
{
   output_info_t *oi, *tmp;
   gfx_ctx_wayland_data_t *wl = (gfx_ctx_wayland_data_t*)data;

   wl_list_for_each_safe(oi, tmp, &wl->all_outputs, link)
   {
      if (oi->global_id == id)
      {
         wl_list_remove(&oi->link);
         free(oi);
         break;
      }
   }
}

static void shm_buffer_handle_release(void *data,
   struct wl_buffer *wl_buffer)
{
   shm_buffer_t *buffer = data;

   wl_buffer_destroy(buffer->wl_buffer);
   munmap(buffer->data, buffer->data_size);
   free(buffer);
}

const struct wl_registry_listener registry_listener = {
   registry_handle_global,
   registry_handle_global_remove,
};


const struct wl_output_listener output_listener = {
   display_handle_geometry,
   display_handle_mode,
   display_handle_done,
   display_handle_scale,
};

const struct xdg_wm_base_listener xdg_shell_listener = {
    xdg_shell_ping,
};

const struct xdg_surface_listener xdg_surface_listener = {
    handle_surface_config,
};

const struct wl_surface_listener wl_surface_listener = {
    wl_surface_enter,
    wl_nop,
};

const struct wl_seat_listener seat_listener = {
   seat_handle_capabilities,
   seat_handle_name,
};

const struct wl_touch_listener touch_listener = {
   touch_handle_down,
   touch_handle_up,
   touch_handle_motion,
   touch_handle_frame,
   touch_handle_cancel,
};

const struct wl_keyboard_listener keyboard_listener = {
   keyboard_handle_keymap,
   keyboard_handle_enter,
   keyboard_handle_leave,
   keyboard_handle_key,
   keyboard_handle_modifiers,
   keyboard_handle_repeat_info
};

const struct wl_pointer_listener pointer_listener = {
   pointer_handle_enter,
   pointer_handle_leave,
   pointer_handle_motion,
   pointer_handle_button,
   pointer_handle_axis,
};

const struct wl_buffer_listener shm_buffer_listener = {
   shm_buffer_handle_release,
};

void flush_wayland_fd(void *data)
{
   struct pollfd fd = {0};
   input_ctx_wayland_data_t *wl = (input_ctx_wayland_data_t*)data;

   wl_display_dispatch_pending(wl->dpy);
   wl_display_flush(wl->dpy);

   fd.fd     = wl->fd;
   fd.events = POLLIN | POLLOUT | POLLERR | POLLHUP;

   if (poll(&fd, 1, 0) > 0)
   {
      if (fd.revents & (POLLERR | POLLHUP))
      {
         close(wl->fd);
         frontend_driver_set_signal_handler_state(1);
      }

      if (fd.revents & POLLIN)
         wl_display_dispatch(wl->dpy);
      if (fd.revents & POLLOUT)
         wl_display_flush(wl->dpy);
   }
}

#ifdef HAVE_MEMFD_CREATE
int create_anonymous_file(off_t size)
{
   int fd;

   int ret;

   fd = memfd_create(SPLASH_SHM_NAME, MFD_CLOEXEC | MFD_ALLOW_SEALING);

   if (fd < 0)
      return -1;

   fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK);

   do {
      ret = posix_fallocate(fd, 0, size);
   } while (ret == EINTR);
   if (ret != 0) {
      close(fd);
      errno = ret;
      return -1;
   }

   return fd;
}
#endif

shm_buffer_t *create_shm_buffer(gfx_ctx_wayland_data_t *wl, int width,
   int height,
   uint32_t format)
{
   struct wl_shm_pool *pool;
   int fd, size, stride, ofd;
   void *data;
   shm_buffer_t *buffer;

   stride = width * 4;
   size = stride * height;

#ifdef HAVE_MEMFD_CREATE
   fd = create_anonymous_file(size);
#else
   fd = shm_open(SPLASH_SHM_NAME, O_RDWR | O_CREAT, 0660);
   ftruncate(fd, size);
#endif
   if (fd < 0) {
      RARCH_ERR("[Wayland] [SHM]: Creating a buffer file for %d B failed: %s\n",
         size, strerror(errno));
      return NULL;
   }

   data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (data == MAP_FAILED) {
      RARCH_ERR("[Wayland] [SHM]: mmap failed: %s\n", strerror(errno));
      close(fd);
      return NULL;
   }

   buffer = calloc(1, sizeof *buffer);

   pool = wl_shm_create_pool(wl->shm, fd, size);
   buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0,
      width, height,
      stride, format);
   wl_buffer_add_listener(buffer->wl_buffer, &shm_buffer_listener, buffer);
   wl_shm_pool_destroy(pool);
#ifndef HAVE_MEMFD_CREATE
   shm_unlink(SPLASH_SHM_NAME);
#endif
   close(fd);

   buffer->data = data;
   buffer->data_size = size;

   return buffer;
}


void shm_buffer_paint_checkerboard(shm_buffer_t *buffer,
      int width, int height, int scale,
      size_t chk, uint32_t bg, uint32_t fg)
{
   uint32_t *pixels = buffer->data;
   uint32_t color;
   int y, x, sx, sy;
   size_t off;
   int stride = width * scale;

   for (y = 0; y < height; y++) {
      for (x = 0; x < width; x++) {
         color = (x & chk) ^ (y & chk) ? fg : bg;
         for (sx = 0; sx < scale; sx++) {
            for (sy = 0; sy < scale; sy++) {
               off = x * scale + sx
                     + (y * scale + sy) * stride;
               pixels[off] = color;
            }
         }
      }
   }
}


void draw_splash_screen(gfx_ctx_wayland_data_t *wl)
{
   shm_buffer_t *buffer;

   buffer = create_shm_buffer(wl,
      wl->width * wl->buffer_scale,
      wl->height * wl->buffer_scale,
      WL_SHM_FORMAT_XRGB8888);
   shm_buffer_paint_checkerboard(buffer, wl->width,
      wl->height, wl->buffer_scale,
      16, 0xffbcbcbc, 0xff8e8e8e);

   wl_surface_attach(wl->surface, buffer->wl_buffer, 0, 0);
   wl_surface_set_buffer_scale(wl->surface, wl->buffer_scale);
   if (wl_surface_get_version(wl->surface) >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION)
      wl_surface_damage_buffer(wl->surface, 0, 0,
         wl->width * wl->buffer_scale,
         wl->height * wl->buffer_scale);
   wl_surface_commit(wl->surface);
}

