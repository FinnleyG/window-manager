#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
#include <stdlib.h>
#include <fcntl.h>

#define STRINGIFY(x) STRINGIFY2(x)
#define STRINGIFY2(x) #x

#define MOD1 XCB_MOD_MASK_4
#define MOD2 XCB_MOD_MASK_SHIFT

#define MENUBAR_HEIGHT 30
#define WORKSPACE_WIDTH 20
#define INFO_WIDTH 100
#define WORKSPACE_COUNT 9

xcb_connection_t * connection;
xcb_screen_t * screen;
xcb_window_t menu_bar; 
uint8_t running = 1;
int fd;
char info[50];

typedef enum { MODE_STACKING, MODE_TILING } Mode;

typedef struct {
	xcb_window_t window_id;
	char * name;
	int name_len;
	int x, y, width, height;
	uint8_t floating, fullscreen;
	int min_width;
	int min_height;
	int max_width;
	int max_height;
} Window;

typedef struct {
	Window * windows;
	int window_count;
	int active_window;
	Mode mode;
} Workspace;

Workspace workspaces[WORKSPACE_COUNT];
int active_workspace = 0;

typedef struct {
    unsigned int mod;
    xcb_keysym_t keysym;
    void (*func)(void * args);
    void * args;
} Key;

char * terminalcmd[] = {"alacritty", NULL};
char * menucmd[] = {"dmenu_run", "-h", STRINGIFY(MENUBAR_HEIGHT), NULL};
int pos_one[] = {1};
int neg_one[] = {-1};

static void move_to_workspace(void * args);
static void set_active_workspace(void * args);
static void closewm(void * args);
static void launch(void * args);
static void toggle_fullscreen(void * args);
static void change_mode(void * args);
static void close_window(void * args);
static void shift_active_window(void * args);
static void swap_with(void * args);

static Key keys[] = {
	{ MOD1, 0xff0d, launch, (void*)terminalcmd },
	{ MOD1, 0x0072, launch, (void*)menucmd },
	{ MOD1, 0x006b, shift_active_window, (void*)&pos_one },
	{ MOD1, 0x006a, shift_active_window, (void*)&neg_one },
	{ MOD1|MOD2, 0x006b, swap_with, (void*)&pos_one },
	{ MOD1|MOD2, 0x006a, swap_with, (void*)&neg_one },
	{ MOD1, 0x0071, close_window, NULL },
	{ MOD1|MOD2, 0x0071, closewm, NULL },
	{ MOD1, 0x0066, toggle_fullscreen, NULL },
	{ MOD1, 0x0020, change_mode, NULL },
	{ MOD1, 0x0031, set_active_workspace, (void*)(int[]){0} },
	{ MOD1, 0x0032, set_active_workspace, (void*)(int[]){1} },
	{ MOD1, 0x0033, set_active_workspace, (void*)(int[]){2} },
	{ MOD1, 0x0034, set_active_workspace, (void*)(int[]){3} },
	{ MOD1, 0x0035, set_active_workspace, (void*)(int[]){4} },
	{ MOD1, 0x0036, set_active_workspace, (void*)(int[]){5} },
	{ MOD1, 0x0037, set_active_workspace, (void*)(int[]){6} },
	{ MOD1, 0x0038, set_active_workspace, (void*)(int[]){7} },
	{ MOD1, 0x0039, set_active_workspace, (void*)(int[]){8} },
	{ MOD1|MOD2, 0x0031, move_to_workspace, (void*)(int[]){0} },
	{ MOD1|MOD2, 0x0032, move_to_workspace, (void*)(int[]){1} },
	{ MOD1|MOD2, 0x0033, move_to_workspace, (void*)(int[]){2} },
	{ MOD1|MOD2, 0x0034, move_to_workspace, (void*)(int[]){3} },
	{ MOD1|MOD2, 0x0035, move_to_workspace, (void*)(int[]){4} },
	{ MOD1|MOD2, 0x0036, move_to_workspace, (void*)(int[]){5} },
	{ MOD1|MOD2, 0x0037, move_to_workspace, (void*)(int[]){6} },
	{ MOD1|MOD2, 0x0038, move_to_workspace, (void*)(int[]){7} },
	{ MOD1|MOD2, 0x0039, move_to_workspace, (void*)(int[]){8} }
};
enum { LIGHT_GRAY, DARK_GRAY, BLUE };
uint32_t colors[] = { 0x555555, 0x333333, 0x3333FF };
xcb_gcontext_t * gcs = NULL;
xcb_gcontext_t * font_gcs = NULL;

static xcb_keycode_t * xcb_get_keycodes(xcb_keysym_t keysym);
static xcb_keysym_t xcb_get_keysym(xcb_keycode_t keycode);
static void handle_key_press(xcb_generic_event_t * ev);
static void handle_map_request(xcb_generic_event_t * ev);
static void add_window(Window * window, Workspace * ws);
static void remove_window(xcb_window_t window);
static void handle_destory_notify(xcb_generic_event_t * ev);
static void handle_unmap_notify(xcb_generic_event_t * ev);
static void handle_property_notify(xcb_generic_event_t * ev);
static void handle_expose_event(xcb_generic_event_t * ev);
static void focus_window(xcb_window_t window);
static void stack(Window * window);
static void tile(Workspace * ws);
static void setup_gcs();
static void setup();
static void draw_info();
static void draw_menubar();

static void set_active_workspace(void * args) {
	if(active_workspace == *(int*)args) return;
	Workspace * prev_ws = &workspaces[active_workspace];
	active_workspace = *(int*)args;
	Workspace * new_ws = &workspaces[active_workspace];

	uint32_t values[] = { 0 };
	xcb_change_window_attributes_checked(connection, screen->root, XCB_CW_EVENT_MASK, values);
	for(int i = 0; i < prev_ws->window_count; ++i) {
		xcb_unmap_window(connection, prev_ws->windows[i].window_id);
	}
	values[0] = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE;
	xcb_change_window_attributes_checked(connection, screen->root, XCB_CW_EVENT_MASK, values);
	for(int i = 0; i < new_ws->window_count; ++i) {
		xcb_map_window(connection, new_ws->windows[i].window_id);
		if(i == new_ws->active_window) {
			focus_window(new_ws->windows[i].window_id);
		}
	}
	draw_menubar();
}

static void move_to_workspace(void * args) {
	if(active_workspace == *(int*)args) return;
	Workspace * ws = &workspaces[active_workspace];
	Workspace * target_ws = &workspaces[*(int*)args];
	Window win = ws->windows[ws->active_window];
	remove_window(win.window_id);
	add_window(&win, target_ws);
	uint32_t values[] = { 0 };
	xcb_change_window_attributes_checked(connection, screen->root, XCB_CW_EVENT_MASK, values);
	xcb_unmap_window(connection, win.window_id);
	values[0] = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE;
	xcb_change_window_attributes_checked(connection, screen->root, XCB_CW_EVENT_MASK, values);
	focus_window(win.window_id);
}

static void toggle_fullscreen(void * args) {
	(void)args;
	Workspace * ws = &workspaces[active_workspace];
	Window * win = &ws->windows[ws->active_window];

	if(ws->window_count < 1) return;

	uint32_t values[4];
	if(win->fullscreen) {
		win->fullscreen = 0;
		values[0] = win->x;
		values[1] = win->y;
		values[2] = win->width;
		values[3] = win->height;
	} else {
		win->fullscreen = 1;
		values[0] = 0;
		values[1] = 0;
		values[2] = screen->width_in_pixels;
		values[3] = screen->height_in_pixels;
	}
	xcb_configure_window(connection, win->window_id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
	if(ws->mode == MODE_TILING) tile(ws);
	xcb_flush(connection);

}

static void change_mode(void * args) {
	(void)args;
	Workspace * ws = &workspaces[active_workspace];
	if(ws->mode == MODE_STACKING) {
		ws->mode = MODE_TILING;
		tile(ws);
	} else {
		ws->mode = MODE_STACKING;
		for(int i = 0; i < ws->window_count; ++i) {
			if(!ws->windows[i].floating && !ws->windows[i].fullscreen) stack(&ws->windows[i]);
		}
	}
}

static void close_window(void * args) {
	(void)args;
	Workspace * ws = &workspaces[active_workspace];

	if(ws->window_count < 1) return;

	xcb_client_message_event_t * event = calloc(32, 1);

	const char protocols_atom[] = "WM_PROTOCOLS";
	const char delete_atom[] = "WM_DELETE_WINDOW";
	xcb_intern_atom_cookie_t cookie_protocols =  xcb_intern_atom(connection, 1, strlen(protocols_atom), protocols_atom);
	xcb_intern_atom_cookie_t cookie_delete =  xcb_intern_atom(connection, 1, strlen(delete_atom), delete_atom);
	xcb_intern_atom_reply_t * reply_protocols = xcb_intern_atom_reply(connection, cookie_protocols, NULL);
	xcb_intern_atom_reply_t * reply_delete = xcb_intern_atom_reply(connection, cookie_delete, NULL);

	event->window = ws->windows[ws->active_window].window_id;
	event->response_type = XCB_CLIENT_MESSAGE;
	event->format = 32;
	event->type = reply_protocols->atom;
	event->data.data32[0] = reply_delete->atom;
	event->data.data32[1] = XCB_CURRENT_TIME;
	xcb_send_event(connection, 0, event->window, XCB_EVENT_MASK_NO_EVENT, (char*)event);
	xcb_flush(connection);
	free(reply_protocols);
	free(reply_delete);
	free(event);

	draw_menubar();
}

static void shift_active_window(void * args) {
	Workspace * ws = &workspaces[active_workspace];
	if(ws->window_count < 1) return;
	if(ws->windows[ws->active_window].fullscreen) {
		const uint32_t values[] = { XCB_STACK_MODE_BELOW };
		xcb_configure_window(connection, ws->windows[ws->active_window].window_id, XCB_CONFIG_WINDOW_STACK_MODE, values);
	}
	ws->active_window = ws->active_window + *(int*)args;
	if(ws->active_window < 0)
		ws->active_window = ws->window_count - 1;
	else if(ws->active_window >= ws->window_count)
		ws->active_window = 0;
	draw_menubar();

	if(ws->active_window < 0) return;
	focus_window(ws->windows[ws->active_window].window_id);
}

static void swap_with(void * args) {
	Workspace * ws = &workspaces[active_workspace];
	if(ws->window_count < 2) return;
	int target_window = ws->active_window + *(int*)args;
	if(target_window < 0)
		target_window = ws->window_count - 1;
	else if(target_window >= ws->window_count)
		target_window = 0;
	Window win = ws->windows[target_window];
	ws->windows[target_window] = ws->windows[ws->active_window];
	ws->windows[ws->active_window] = win;

	ws->active_window = target_window;
	draw_menubar();
	if(ws->mode == MODE_TILING) tile(ws);
	focus_window(ws->windows[ws->active_window].window_id);
}

static void closewm(void * args) {
	(void)args;
	running = 0;
}

static void launch(void * args) {
	char ** command;
	command = (char**)args;
	if(fork() == 0) {
		setsid();
		if(fork() != 0) {
			_exit(0);
		}
		execvp((char*)command[0], (char**)command);
		_exit(0);
	}
	wait(NULL);
}

static xcb_keycode_t *xcb_get_keycodes(xcb_keysym_t keysym) {
	xcb_key_symbols_t *keysyms = xcb_key_symbols_alloc(connection);
	xcb_keycode_t *keycode;
	keycode = (!(keysyms) ? NULL : xcb_key_symbols_get_keycode(keysyms, keysym));
	xcb_key_symbols_free(keysyms);
	return keycode;
}

static xcb_keysym_t xcb_get_keysym(xcb_keycode_t keycode) {
	xcb_key_symbols_t * keysyms = xcb_key_symbols_alloc(connection);
	xcb_keysym_t keysym;
	keysym = (!(keysyms) ? 0 : xcb_key_symbols_get_keysym(keysyms, keycode, 0));
	xcb_key_symbols_free(keysyms);
	return keysym;
}

static void handle_key_press(xcb_generic_event_t * ev) {
	// Workspace * ws = &workspaces[active_workspace];
	xcb_key_press_event_t * e = (xcb_key_press_event_t*)ev;
	xcb_keysym_t keysym = xcb_get_keysym(e->detail);
	// if(ws->active_window > -1 && e->child != ws->windows[ws->active_window].window_id) {
		// for(int i = 0; i < window_count; ++i) {
		// 	if(windows[i].window_id == e->child) {
		// 		active_window = i;	
		// 		const uint32_t values[] = { XCB_STACK_MODE_ABOVE };
		// 		xcb_configure_window(connection, windows[active_window].window_id, XCB_CONFIG_WINDOW_STACK_MODE, values);
		// 		break;
		// 	}
		// }
	// }
	int key_table_size = sizeof(keys) / sizeof(*keys);
	for(int i = 0; i < key_table_size; ++i) {
		if((keys[i].keysym == keysym) && (keys[i].mod == e->state)) {
			keys[i].func(keys[i].args);
		}
	}
}

static void handle_map_request(xcb_generic_event_t * ev) {
	Workspace * ws = &workspaces[active_workspace];
	xcb_map_request_event_t * e = (xcb_map_request_event_t*)ev;

	xcb_get_window_attributes_cookie_t attributes_cookie = xcb_get_window_attributes(connection, e->window);
	xcb_get_window_attributes_reply_t * attributes_reply = xcb_get_window_attributes_reply(connection, attributes_cookie, NULL);
	if(!attributes_reply || attributes_reply->override_redirect) return;
	free(attributes_reply);

	Window new_window;
	new_window.window_id = e->window;

	// char atom_name[] = "_NET_WM_NAME";
	// char atom_type[] = "UTF8_STRING";
	// xcb_intern_atom_cookie_t cookie_atom =  xcb_intern_atom(connection, 1, strlen(atom_name), atom_name);
	// xcb_intern_atom_reply_t * reply_atom = xcb_intern_atom_reply(connection, cookie_atom, NULL);
	// xcb_intern_atom_cookie_t cookie_type =  xcb_intern_atom(connection, 1, strlen(atom_type), atom_type);
	// xcb_intern_atom_reply_t * reply_type = xcb_intern_atom_reply(connection, cookie_type, NULL);

	xcb_get_property_reply_t * name_reply;
	xcb_get_property_reply_t * size_reply;
	xcb_get_property_cookie_t name_cookie = xcb_get_property(connection, 0, e->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 4);
	// xcb_get_property_cookie_t name_cookie = xcb_get_property(connection, 0, e->window, reply_atom->atom, reply_type->atom, 0, 4);
	xcb_get_property_cookie_t size_cookie = xcb_get_property(connection, 0, e->window, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 0, 64);
	if((name_reply = xcb_get_property_reply(connection, name_cookie, NULL))) {
        	int len = xcb_get_property_value_length(name_reply);
		new_window.name = malloc((len+1) * sizeof(char));
		strncpy(new_window.name, xcb_get_property_value(name_reply), len);
		new_window.name[len] = '\0';
		new_window.name_len = len;
    	}
	free(name_reply);
 
	if((size_reply = xcb_get_property_reply(connection, size_cookie, NULL))) {
		new_window.min_width = *((int*)xcb_get_property_value(size_reply)+5);
		new_window.min_height = *((int*)xcb_get_property_value(size_reply)+6);
		new_window.max_width = *((int*)xcb_get_property_value(size_reply)+7);
		new_window.max_height = *((int*)xcb_get_property_value(size_reply)+8);
	}
	free(size_reply);

	xcb_map_window(connection, e->window);
	
	new_window.width = screen->width_in_pixels;
	new_window.height = screen->height_in_pixels - MENUBAR_HEIGHT;
	if(new_window.max_width > 0 && new_window.width > new_window.max_width) {
		new_window.width = new_window.max_width;
		new_window.floating = 1;
	}
	if(new_window.max_height > 0 && new_window.height > new_window.max_height) {
		new_window.height = new_window.max_height;
		new_window.floating = 1;
	}
	if(new_window.width < new_window.min_width) {
		new_window.width = new_window.min_width;
		new_window.floating = 1;
	}
	if(new_window.height < new_window.min_height) {
		new_window.height = new_window.min_height;
		new_window.floating = 1;
	}
	if(new_window.floating) {
		new_window.x = screen->width_in_pixels/2 - new_window.width/2;
		new_window.y = (screen->height_in_pixels+MENUBAR_HEIGHT)/2 - new_window.height/2;
	} else {
		new_window.x = 0;
		new_window.y = MENUBAR_HEIGHT;
	}

	uint32_t values[5];
	values[0] = new_window.x;
	values[1] = new_window.y;
	values[2] = new_window.width;
	values[3] = new_window.height;
	values[4] = 0;
	xcb_configure_window(connection, e->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH, values);
	xcb_flush(connection);

	values[0] = XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE;
	xcb_change_window_attributes_checked(connection, e->window,
		XCB_CW_EVENT_MASK, values);

	if ((e->window != 0) && (e->window != screen->root)) {
		focus_window(e->window);
	}

	add_window(&new_window, ws);
}

static void add_window(Window * window, Workspace * ws) {
	ws->active_window = ws->window_count;
	(ws->window_count)++;
	ws->windows = reallocarray(ws->windows, ws->window_count, sizeof(Window));
	ws->windows[ws->window_count-1] = *window;
	if(ws->mode == MODE_TILING) tile(ws);
	draw_menubar();
}

static void remove_window(xcb_window_t window) {
	for(int i = 0; i < WORKSPACE_COUNT; ++i) {
		Workspace * ws = &workspaces[i];
		for(int j = 0; j < ws->window_count; ++j) {
			if(ws->windows[j].window_id == window) {
				for(int k = j; k < ws->window_count-1; ++k) ws->windows[k] = ws->windows[k+1];

				if(ws->active_window == --(ws->window_count)) ws->active_window--;
				if(ws->active_window >= 0) focus_window(ws->windows[ws->active_window].window_id);

				ws->windows = reallocarray(ws->windows, ws->window_count, sizeof(Window));
				draw_menubar();
			}
		}
		if(ws->mode == MODE_TILING) tile(ws);
	}
}

static void handle_destory_notify(xcb_generic_event_t * ev) {
	xcb_destroy_notify_event_t * e = (xcb_destroy_notify_event_t*)ev;
	remove_window(e->window);
}

static void handle_unmap_notify(xcb_generic_event_t * ev) {
	xcb_unmap_notify_event_t * e = (xcb_unmap_notify_event_t*)ev;
	remove_window(e->window);
}

static void handle_property_notify(xcb_generic_event_t * ev) {
	xcb_property_notify_event_t * e = (xcb_property_notify_event_t*)ev;
	if(e->window == screen->root && e->atom == XCB_ATOM_WM_NAME) {
		xcb_get_property_reply_t * name_reply;
		xcb_get_property_cookie_t name_cookie = xcb_get_property(connection, 0, e->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 4);
		if((name_reply = xcb_get_property_reply(connection, name_cookie, NULL))) {
			int len = xcb_get_property_value_length(name_reply);
			strncpy(info, xcb_get_property_value(name_reply), len);
			info[len] = '\0';
		}
		free(name_reply);
		draw_info();
	}
}

static void handle_expose_event(xcb_generic_event_t * ev) {
	xcb_expose_event_t * e = (xcb_expose_event_t *)ev;
	if(e->window == menu_bar) {
		draw_menubar();
		draw_info();
	}
}

// static void handle_client_message(xcb_generic_event_t * ev) {
	// xcb_client_message_event_t * e = (xcb_client_message_event_t*)ev;
	// const char atom_type[] = "_NET_WM_STATE";
	// const char atom_fullscreen[] = "_NET_WM_STATE_FULLSCREEN";
	// xcb_intern_atom_cookie_t cookie_type =  xcb_intern_atom(connection, 1, strlen(atom_type), atom_type);
	// xcb_intern_atom_cookie_t cookie_fullscreen =  xcb_intern_atom(connection, 1, strlen(atom_fullscreen), atom_fullscreen);
	// xcb_intern_atom_reply_t * reply_type = xcb_intern_atom_reply(connection, cookie_type, NULL);
	// xcb_intern_atom_reply_t * reply_fullscreen = xcb_intern_atom_reply(connection, cookie_fullscreen, NULL);
	//
	// xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name(connection, e->data.data32[1]);
	// xcb_get_atom_name_reply_t * reply = xcb_get_atom_name_reply(connection, cookie, NULL);
	// char * atomname = xcb_get_atom_name_name(reply);
	// dprintf(fd, "%s\n", atomname);

	// free(reply_type);
	// free(reply_fullscreen);
	// free(reply);
// }

static void focus_window(xcb_window_t window) {
	const uint32_t values[] = { XCB_STACK_MODE_ABOVE };
	xcb_configure_window(connection, menu_bar, XCB_CONFIG_WINDOW_STACK_MODE, values);
	xcb_configure_window(connection, window, XCB_CONFIG_WINDOW_STACK_MODE, values);
	xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, window, XCB_CURRENT_TIME);
}

static void stack(Window * window) {
	uint32_t values[4];
	values[0] = 0;
	values[1] = MENUBAR_HEIGHT;
	values[2] = screen->width_in_pixels;
	values[3] = screen->height_in_pixels - MENUBAR_HEIGHT;
	xcb_configure_window(connection, window->window_id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
}

static void tile(Workspace * ws) {
	if(ws->window_count < 1) return;
	int tileable_window_count = 0;
	for(int i = 0; i < ws->window_count; ++i) {
		if(!ws->windows[i].floating && !ws->windows[i].fullscreen) ++tileable_window_count;
	}

	int secondary_window_height = (tileable_window_count > 1) ? (screen->height_in_pixels-MENUBAR_HEIGHT)/(tileable_window_count-1) : 0;
	uint32_t values[4];
	int j = 0;
	for(int i = 0; i < ws->window_count; ++i) {
		if(ws->windows[i].floating || ws->windows[i].fullscreen) continue;
		values[0] = (j == 0) ? 0 : screen->width_in_pixels/2;
		values[1] = (j == 0) ? MENUBAR_HEIGHT : MENUBAR_HEIGHT+(j-1)*secondary_window_height;
		values[2] = (tileable_window_count > 1) ? screen->width_in_pixels/2 : screen->width_in_pixels;
		values[3] = (j == 0) ? screen->height_in_pixels : secondary_window_height;
		xcb_configure_window(connection, ws->windows[i].window_id, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
		++j;
	}
}

static void setup_gcs() {
	int color_count = sizeof(colors)/sizeof(*colors);
	gcs = (xcb_gcontext_t*)malloc(color_count * sizeof(xcb_gcontext_t));
	font_gcs = (xcb_gcontext_t*)malloc(color_count * sizeof(xcb_gcontext_t));
	uint32_t mask = XCB_GC_FOREGROUND;
	uint32_t font_mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
	xcb_font_t font = xcb_generate_id(connection);
	char font_name[] = "fixed";
	xcb_open_font(connection, font, strlen(font_name), font_name);
	for(int i = 0; i < color_count; ++i) {
		gcs[i] = xcb_generate_id(connection);
		font_gcs[i] = xcb_generate_id(connection);
		uint32_t value = colors[i];
		uint32_t font_values[3] = { screen->white_pixel, colors[i], font };
		xcb_create_gc(connection, gcs[i], screen->root, mask, &value);
		xcb_create_gc(connection, font_gcs[i], screen->root, font_mask, font_values);
	}
}

static void setup() {
	uint32_t values[1];
	values[0] = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE;
		// | XCB_EVENT_MASK_ENTER_WINDOW
		// | XCB_EVENT_MASK_LEAVE_WINDOW;
	xcb_change_window_attributes_checked(connection, screen->root, XCB_CW_EVENT_MASK, values);
	xcb_ungrab_key(connection, XCB_GRAB_ANY, screen->root, XCB_MOD_MASK_ANY);
	int key_table_size = sizeof(keys) / sizeof(*keys);
	for (int i = 0; i < key_table_size; ++i) {
		xcb_keycode_t *keycode = xcb_get_keycodes(keys[i].keysym);
		if (keycode != NULL) {
			xcb_grab_key(connection, 1, screen->root, keys[i].mod, *keycode,
				XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC );
		}
	}
	xcb_flush(connection);
	xcb_grab_button(connection, 0, screen->root, XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
		XCB_GRAB_MODE_ASYNC, screen->root, XCB_NONE, 1, MOD1);
	xcb_grab_button(connection, 0, screen->root, XCB_EVENT_MASK_BUTTON_PRESS |
		XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
		XCB_GRAB_MODE_ASYNC, screen->root, XCB_NONE, 3, MOD1);
	xcb_flush(connection);

	for(int i = 0; i < WORKSPACE_COUNT; ++i) {
		workspaces[i].window_count = 0;
		workspaces[i].active_window = -1;
	}
}

static void draw_info() {
	xcb_rectangle_t rect = { screen->width_in_pixels-INFO_WIDTH, 0, INFO_WIDTH, MENUBAR_HEIGHT };
	xcb_poly_fill_rectangle(connection, menu_bar, gcs[LIGHT_GRAY], 1, &rect);
	xcb_image_text_8(connection, strlen(info), menu_bar, font_gcs[LIGHT_GRAY], screen->width_in_pixels-INFO_WIDTH+15, MENUBAR_HEIGHT-10, info);
}

static void draw_menubar() {
	xcb_rectangle_t rect = { 0, 0, screen->width_in_pixels-INFO_WIDTH, MENUBAR_HEIGHT };
	xcb_poly_fill_rectangle(connection, menu_bar, gcs[LIGHT_GRAY], 1, &rect);

	int col;
	for(int i = 0; i < WORKSPACE_COUNT; ++i) {
		if(i == active_workspace)
			col = BLUE;
		else
			col = LIGHT_GRAY;
		int x = WORKSPACE_WIDTH*i;
		xcb_rectangle_t rect = { x, 0, WORKSPACE_WIDTH, MENUBAR_HEIGHT };
		xcb_poly_fill_rectangle(connection, menu_bar, gcs[col], 1, &rect);
		char num[2];
		sprintf(num, "%i", i+1);
		xcb_image_text_8(connection, strlen(num), menu_bar, font_gcs[col], WORKSPACE_WIDTH/2+x-1, MENUBAR_HEIGHT-10, num);
		if(workspaces[i].window_count > 0) xcb_clear_area(connection, 0, menu_bar, x, 0, 4, 4);
	}
	
	Workspace * ws = &workspaces[active_workspace];
	if(ws->window_count == 0) return;

	int tab_width = (screen->width_in_pixels-WORKSPACE_WIDTH*WORKSPACE_COUNT-INFO_WIDTH)/ws->window_count;

	for(int i = 0; i < ws->window_count; ++i) {
		xcb_rectangle_t rect = { tab_width*i+WORKSPACE_WIDTH*WORKSPACE_COUNT, 0, tab_width, MENUBAR_HEIGHT };
		if(i == ws->active_window)
			col = BLUE;
		else
			col = i % 2; //alternate between dark and light gray
		xcb_poly_fill_rectangle(connection, menu_bar, gcs[col], 1, &rect);
		xcb_image_text_8(connection, strlen(ws->windows[i].name), menu_bar, font_gcs[col], 15+tab_width*i+WORKSPACE_WIDTH*WORKSPACE_COUNT, MENUBAR_HEIGHT-10, ws->windows[i].name);
	}
	xcb_flush(connection);
}

int main() {
	connection = xcb_connect(NULL, NULL);
	if(xcb_connection_has_error(connection)) { 
		printf("Connection failed.\n");
		return 1;
	}

	fd = open("output-file.txt", O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR);
	screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
	setup();

	menu_bar = xcb_generate_id(connection);
	uint32_t prop_values[2];
	uint32_t prop_names = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	prop_values[0] = screen->white_pixel;
	prop_values[1] = XCB_EVENT_MASK_EXPOSURE;

	xcb_create_window(connection, screen->root_depth, menu_bar, screen->root, 0, 0, screen->width_in_pixels, MENUBAR_HEIGHT, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, prop_names, prop_values);
	xcb_map_window(connection, menu_bar);
	xcb_flush(connection);

	setup_gcs();

	while(running) {
		xcb_generic_event_t * ev = xcb_wait_for_event(connection);
		if(!ev) continue;
		switch(ev->response_type & ~0x80) {
		case XCB_KEY_PRESS:
			handle_key_press(ev);
			break;
		case XCB_MAP_REQUEST:
			handle_map_request(ev);
			break;
		case XCB_EXPOSE:
			handle_expose_event(ev);
			break;
		case XCB_DESTROY_NOTIFY:
			handle_destory_notify(ev);
			break;
		case XCB_UNMAP_NOTIFY:
			handle_unmap_notify(ev);
			break;
		case XCB_PROPERTY_NOTIFY:
			handle_property_notify(ev);
			break;
		// case XCB_CLIENT_MESSAGE:
		// 	handle_client_message(ev);
		// 	break;
		}
		free(ev);
		xcb_flush(connection);
	}

	close(fd);
	free(gcs);
	free(font_gcs);
	if(connection != NULL)
		xcb_disconnect(connection);
	return 0;
}
