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

#define MOD1 XCB_MOD_MASK_4
#define MOD2 XCB_MOD_MASK_SHIFT

#define MAX(a,b)\
	({\
	 __typeof__ (a) _a = (a);\
	 __typeof__ (b) _b = (b);\
	 _a > _b ? _a : _b;\
	 })

#define MIN(a,b)\
	({\
	 __typeof__ (a) _a = (a);\
	 __typeof__ (b) _b = (b);\
	 _a < _b ? _a : _b;\
	 })

xcb_connection_t * connection;
xcb_screen_t* screen;
xcb_window_t window;
static uint32_t values[3];
xcb_window_t menu_bar; 

// xcb_atom_t wm_delete_window;
// xcb_atom_t wm_protocols;

typedef struct {
	// int id;
	xcb_window_t window_id;
	char * name;
	int name_len;
	int x, y, width, height;
	uint8_t floating;
	int min_width;
	int min_height;
	int max_width;
	int max_height;
} Window;

typedef struct {
    unsigned int mod;
    xcb_keysym_t keysym;
    void (*func)(char **com);
    char **com;
} Key;

static void closewm(char **command);
static void launch(char **command);
static void kill_client(char **command);
static void close_window(char **command);
// static void remap_test(char **command);

static void draw_menubar();

static xcb_gc_t getFontGC (xcb_connection_t *c, xcb_screen_t *screen, xcb_window_t window, const char *font_name );

static void drawText(xcb_screen_t * screen, xcb_window_t  window, int16_t x1, int16_t y1, const char *label);

int fd;

char * terminalcmd[] = {"alacritty", NULL};
char * menucmd[] = {"dmenu_run", NULL};

static Window * windows;
int window_count = 0;

static Key keys[] = {
	{ MOD1,      0x0062, NULL,      NULL },				
	{ MOD1,      0xff0d, launch,    terminalcmd },
	{ MOD1,      0x0072, launch,    menucmd },										 
	{ MOD1,      0x0066, NULL,		NULL },										
	{ MOD1,      0x0071, close_window,		NULL },										
	{ MOD1|MOD2, 0x0071, closewm,   NULL }										
};


	/* 0x0062 = XK_b */
	/* 0xff0d = XK_Enter */
	/* 0xff0d = XK_Enter */
	/* 0x0020 = XK_space */
	/* 0x0066 = XK_f */
	/* 0x0071 = XK_q */
	/* 0x0071 = XK_q */

// static void remap_test(char **command) {
// 	xcb_map_window(connection, windows[0].window_id);
// 	xcb_map_window(connection, windows[1].window_id);
// 	xcb_map_window(connection, window);
// 	xcb_flush(connection);
// }

static void kill_client(char **command) {
	// if(window_count > 0) {
	xcb_kill_client(connection, window);
	// 	for(int i = 0; i < window_count; ++i) {
	// 		if(windows[i].window_id == window) {
	// 			for(int j = i; j < window_count-1; ++j) {
	// 				windows[j] = windows[j+1];
	// 			}	
	// 		}
	// 	}
	// 	window_count--;
	// 	windows = reallocarray(windows, window_count, sizeof(Window));
	// 	draw_menubar();
	// }
	draw_menubar();
}

static void close_window(char **command) {
	if(window_count > 0) {
		// xcb_unmap_window(connection, window);
		xcb_client_message_event_t * event = calloc(32, 1);

		const char protocols_atom[] = "WM_PROTOCOLS";
		const char delete_atom[] = "WM_DELETE_WINDOW";
		xcb_intern_atom_cookie_t cookie_protocols =  xcb_intern_atom(connection, 1, strlen(protocols_atom), protocols_atom);
		xcb_intern_atom_cookie_t cookie_delete =  xcb_intern_atom(connection, 1, strlen(delete_atom), delete_atom);
		xcb_intern_atom_reply_t * reply_protocols = xcb_intern_atom_reply(connection, cookie_protocols, NULL);
		xcb_intern_atom_reply_t * reply_delete = xcb_intern_atom_reply(connection, cookie_delete, NULL);

		event->window = window;
		event->response_type = XCB_CLIENT_MESSAGE;
		event->format = 32;
		event->type = reply_protocols->atom;
		event->data.data32[0] = reply_delete->atom;
		event->data.data32[1] = XCB_CURRENT_TIME;
		xcb_send_event(connection, 0, window, XCB_EVENT_MASK_NO_EVENT, (char*)event);
		xcb_flush(connection);
		free(reply_protocols);
		free(reply_delete);
		free(event);

		// for(int i = 0; i < window_count; ++i) {
		// 	if(windows[i].window_id == window) {
		// 		for(int j = i; j < window_count-1; ++j) {
		// 			windows[j] = windows[j+1];
		// 		}	
		// 	}
		// }
		// window_count--;
		// windows = reallocarray(windows, window_count, sizeof(Window));
		draw_menubar();
	}
}

static void closewm(char **command) {
	if(connection != NULL) {
		xcb_disconnect(connection);
	}
}

static void launch(char **command) {
	if (fork() == 0) {
		setsid();
		if (fork() != 0) {
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
	xcb_key_symbols_t *keysyms = xcb_key_symbols_alloc(connection);
	xcb_keysym_t keysym;
	keysym = (!(keysyms) ? 0 : xcb_key_symbols_get_keysym(keysyms, keycode, 0));
	xcb_key_symbols_free(keysyms);
	return keysym;
}

static void handle_key_press(xcb_generic_event_t *ev) {
	xcb_key_press_event_t *e = ( xcb_key_press_event_t *) ev;
	xcb_keysym_t keysym = xcb_get_keysym(e->detail);
	window = e->child;
	int key_table_size = sizeof(keys) / sizeof(*keys);
	// dprintf(fd, "%i\n", keysym);
	// dprintf(fd, "test\n");
	for (int i = 0; i < key_table_size; ++i) {
		if ((keys[i].keysym == keysym) && (keys[i].mod == e->state)) {
			keys[i].func(keys[i].com);
		}
	}
}

#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 400
#define BORDER_WIDTH 0
#define MENUBAR_HEIGHT 30

static void handle_map_request(xcb_generic_event_t * ev) {
	xcb_map_request_event_t * e = (xcb_map_request_event_t*)ev;

	xcb_get_window_attributes_cookie_t attributes_cookie = xcb_get_window_attributes(connection, e->window);
	xcb_get_window_attributes_reply_t * attributes_reply = xcb_get_window_attributes_reply(connection, attributes_cookie, NULL);
	if(!attributes_reply || attributes_reply->override_redirect) return;
	free(attributes_reply);

	
	Window new_window;
	new_window.window_id = e->window;

	// xcb_get_geometry_cookie_t geom_now = xcb_get_geometry(connection, e->window);
	// xcb_get_geometry_reply_t *geom = xcb_get_geometry_reply(connection, geom_now, NULL);
	// dprintf(fd, "amogus: %i, %i \n", geom->width, geom->height);

	xcb_get_property_reply_t * reply;
	xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, e->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 4);
	if((reply = xcb_get_property_reply(connection, cookie, NULL))) {
        int len = xcb_get_property_value_length(reply);
		new_window.name = malloc((len+1) * sizeof(char));
		strncpy(new_window.name, xcb_get_property_value(reply), len);
		new_window.name[len] = '\0';
		new_window.name_len = len;
    }
	free(reply);
 
	xcb_get_property_reply_t * size_reply;
	cookie = xcb_get_property(connection, 0, e->window, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 0, 64);
	if((size_reply = xcb_get_property_reply(connection, cookie, NULL))) {
		new_window.min_width = *((int*)xcb_get_property_value(size_reply)+5);
		new_window.min_height = *((int*)xcb_get_property_value(size_reply)+6);
		new_window.max_width = *((int*)xcb_get_property_value(size_reply)+7);
		new_window.max_height = *((int*)xcb_get_property_value(size_reply)+8);
		dprintf(fd, "min width: %i\n", new_window.min_width);
		dprintf(fd, "min height: %i\n", new_window.min_height);
		dprintf(fd, "max width: %i\n", new_window.max_width);
		dprintf(fd, "max height: %i\n", new_window.max_height);
	}
	free(size_reply);


	xcb_map_window(connection, e->window);
	// xcb_unmap_window(connection, e->window);
	// xcb_map_window(connection, e->window);

	
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

	uint32_t vals[5];
	vals[0] = new_window.x;
	vals[1] = new_window.y;
	vals[2] = new_window.width;
	vals[3] = new_window.height;
	// vals[0] = (screen->width_in_pixels / 2) - (WINDOW_WIDTH / 2);
	// vals[1] = (screen->height_in_pixels / 2) - (WINDOW_HEIGHT / 2);
	// vals[2] = new_window.max_width > 0 ? MAX(MIN(screen->width_in_pixels, new_window.max_width), new_window.min_width) : screen->width_in_pixels;
	// vals[3] = new_window.max_height > 0 ? MAX(MIN(screen->height_in_pixels - MENUBAR_HEIGHT, new_window.max_height), new_window.min_height) : screen->height_in_pixels - MENUBAR_HEIGHT;
	// vals[2] = screen->width_in_pixels;
	// vals[3] = screen->height_in_pixels - MENUBAR_HEIGHT;
	// vals[2] = MIN(screen->width_in_pixels, new_window.max_width);
	// vals[3] = MIN(screen->height_in_pixels - MENUBAR_HEIGHT, new_window.max_height);
	vals[4] = BORDER_WIDTH;
	xcb_configure_window(connection, e->window, XCB_CONFIG_WINDOW_X |
		XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
		XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH, vals);
	xcb_flush(connection);
	values[0] = XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_FOCUS_CHANGE;
	xcb_change_window_attributes_checked(connection, e->window,
		XCB_CW_EVENT_MASK, values);


	if ((e->window != 0) && (e->window != screen->root)) {
		xcb_set_input_focus(connection, XCB_INPUT_FOCUS_POINTER_ROOT, e->window, XCB_CURRENT_TIME);
	}

	window_count++;
	windows = reallocarray(windows, window_count, sizeof(Window));
	windows[window_count-1] = new_window;
}

static void handle_destory_notify(xcb_generic_event_t * ev) {
	xcb_destroy_notify_event_t * e = (xcb_destroy_notify_event_t*)ev;
	for(int i = 0; i < window_count; ++i) {
		if(windows[i].window_id == e->window) {
			// dprintf(fd, "destory: %s\n", windows[i].name);
			for(int j = i; j < window_count-1; ++j) {
				windows[j] = windows[j+1];
			}	
			window_count--;
			windows = reallocarray(windows, window_count, sizeof(Window));
			draw_menubar();
			return;
		}
	}
}

static void handle_unmap_notify(xcb_generic_event_t * ev) {
	xcb_unmap_notify_event_t * e = (xcb_unmap_notify_event_t*)ev;
	for(int i = 0; i < window_count; ++i) {
		if(windows[i].window_id == e->window) {
			dprintf(fd, "unmap: %s\n", windows[i].name);
			for(int j = i; j < window_count-1; ++j) {
				windows[j] = windows[j+1];
			}	
			window_count--;
			windows = reallocarray(windows, window_count, sizeof(Window));
			draw_menubar();
			return;
		}
	}
}


static void setup(void) {
	values[0] = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT
		| XCB_EVENT_MASK_STRUCTURE_NOTIFY
		| XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
		| XCB_EVENT_MASK_PROPERTY_CHANGE;
		// | XCB_EVENT_MASK_ENTER_WINDOW
		// | XCB_EVENT_MASK_LEAVE_WINDOW;
	xcb_change_window_attributes_checked(connection, screen->root,
		XCB_CW_EVENT_MASK, values);
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
}

static void
testCookie (xcb_void_cookie_t cookie,
			xcb_connection_t *connection,
			char *errMessage )
{
	xcb_generic_error_t *error = xcb_request_check (connection, cookie);
	if (error) {
		dprintf (fd, "ERROR: %s : %i \n", errMessage , error->error_code);
		xcb_disconnect (connection);
		exit (-1);
	}
}

static void
drawText (xcb_screen_t     *screen, xcb_window_t      window_id, int16_t           x1, int16_t           y1, const char       *label ) {

	/* get graphics context */
	xcb_gcontext_t gc = getFontGC (connection, screen, window_id, "fixed");


	/* draw the text */
	xcb_void_cookie_t textCookie = xcb_image_text_8_checked (connection,
															 strlen (label),
															 window_id,
															 gc,
															 x1, y1,
															 label );

	testCookie(textCookie, connection, "can't paste text");


	/* free the gc */
	xcb_void_cookie_t gcCookie = xcb_free_gc (connection, gc);

	testCookie(gcCookie, connection, "can't free gc");
}


static xcb_gc_t
getFontGC (xcb_connection_t  *connection,
		   xcb_screen_t      *screen,
		   xcb_window_t       window,
		   const char        *font_name )
{
	/* get font */
	xcb_font_t font = xcb_generate_id (connection);
	xcb_void_cookie_t fontCookie = xcb_open_font_checked (connection,
														  font,
														  strlen (font_name),
														  font_name );

	testCookie(fontCookie, connection, "can't open font");


	/* create graphics context */
	xcb_gcontext_t  gc            = xcb_generate_id (connection);
	uint32_t        mask          = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT;
	uint32_t        value_list[3] = { screen->black_pixel,
									  screen->white_pixel,
									  font };

	xcb_void_cookie_t gcCookie = xcb_create_gc_checked (connection,
														gc,
														window,
														mask,
														value_list );

	testCookie(gcCookie, connection, "can't create gc");


	/* close font */
	fontCookie = xcb_close_font_checked (connection, font);

	testCookie(fontCookie, connection, "can't close font");

	return gc;
}

static void draw_menubar() {
	xcb_clear_area(connection, 0, menu_bar, 0, 0, screen->width_in_pixels, MENUBAR_HEIGHT);
	// int count;
	// xcb_window_t * children;
	// xcb_query_tree_cookie_t cookie = xcb_query_tree(connection, screen->root);
	// xcb_query_tree_reply_t * reply = xcb_query_tree_reply(connection, cookie, NULL);
	// xcb_get_property_cookie_t * cookies;
	// xcb_get_property_reply_t * name_reply;
	// char name[32];
	// int len;
	// if(reply) {
	// 	count = xcb_query_tree_children_length(reply);
	// 	children = xcb_query_tree_children(reply);
	// 	cookies = calloc(count, sizeof(xcb_get_property_cookie_t));
	// 	for(int i = 0; i < count; ++i) {
	// 		cookies[i] = xcb_get_property(connection, 0, children[i], XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 4);
	// 	}
	//
	// 	int j = 0;
	// 	for(int i = 0; i < count; ++i) {
	// 		name_reply = xcb_get_property_reply(connection, cookies[i], NULL);
	// 		len = xcb_get_property_value_length(name_reply);
	// 		if(len == 0) continue;
	// 		strncpy(name, (char*)xcb_get_property_value(name_reply), len);
	// 		name[len] = '\0';
	// 		drawText(screen, menu_bar, 30+100*j, MENUBAR_HEIGHT - 10, name);
	// 		j++;
	// 		free(name_reply);
	// 	}
	// 	free(reply);
	// }

	// xcb_get_property_reply_t * reply;
	// xcb_get_property_cookie_t cookie = xcb_get_property(connection, 0, e->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 4);
	//
	// if ((reply = xcb_get_property_reply(connection, cookie, NULL))) {
  //       int len = xcb_get_property_value_length(reply);
		// new_window.name = malloc((len+1) * sizeof(char));
		// strncpy(new_window.name, xcb_get_property_value(reply), len);
		// new_window.name[len] = '\0';
		// new_window.name_len = len;
   //      if (len == 0) {
   //          dprintf(fd, "TODO\n");
   //      } else {
			// dprintf(fd, "WM_NAME is %.*s\n", len, (char*)xcb_get_property_value(reply));
			// dprintf(fd, "%i\n", len);
		// }
		// dprintf(fd, "%.*s\n", 3, (char*)xcb_get_property_value(reply));
		// dprintf(fd, "%i\n", len);
    // }


	for(int i = 0; i < window_count; ++i) {
		drawText(screen, menu_bar, 30+100*i, MENUBAR_HEIGHT - 10, windows[i].name);
	}
	xcb_flush(connection);
}

int main(int argc, char *argv[]) {

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

	// drawText (screen, menu_bar, 10, MENUBAR_HEIGHT - 10, "testtext123" );

	while(1) {
		xcb_generic_event_t *ev = xcb_wait_for_event(connection);
		if ((ev->response_type & ~0x80) == XCB_KEY_PRESS) {
			handle_key_press(ev);
		}
		if ((ev->response_type & ~0x80) == XCB_MAP_REQUEST) {
			handle_map_request(ev);
		}
		if ((ev->response_type & ~0x80) == XCB_EXPOSE) {
			draw_menubar();
		}
		if ((ev->response_type & ~0x80) == XCB_DESTROY_NOTIFY) {
			handle_destory_notify(ev);
		}
		if ((ev->response_type & ~0x80) == XCB_UNMAP_NOTIFY) {
			handle_unmap_notify(ev);
		}
		if ((ev->response_type & ~0x80) == XCB_CLIENT_MESSAGE) {
			// xcb_client_message_event_t * e = (xcb_client_message_event_t*)ev;
			// xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name(connection, e->type);
			// xcb_get_atom_name_reply_t * reply = xcb_get_atom_name_reply(connection, cookie, NULL);
			// int len = xcb_get_atom_name_name_length(reply);
			//
			// char * atom_name = malloc((len+1) * sizeof(char));
			// strncpy(atom_name, xcb_get_atom_name_name(reply), len);
			// atom_name[len] = '\0';
			//
			// dprintf(fd, "client message event: %s\n", atom_name);

			// xcb_client_message_event_t * event = (xcb_client_message_event_t*)ev;
			// for(int i = 0; i < window_count; ++i) {
			// 	if(windows[i].window_id == event->window) {
			// 		dprintf(fd, "%s\n", windows[i].name);
			// 	}
			// }
		}
		free(ev);
		xcb_flush(connection);
	}

	close(fd);
	xcb_disconnect(connection);

	return 0;
}
