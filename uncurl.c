// uncurl - public domain - written by Anders Kaare Straadt 2024

// configuration:
#ifndef MOUSE_WHEEL_SENSITIVITY
#define MOUSE_WHEEL_SENSITIVITY (1.017)
#endif
#ifndef MOUSE_BUTTON_SELECT
#define MOUSE_BUTTON_SELECT (1) // LMB
#endif
#ifndef MOUSE_BUTTON_PAN
#define MOUSE_BUTTON_PAN (3) // RMB
#endif

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>

#include <SDL.h>

#define ARRAY_LENGTH(xs) (sizeof(xs)/sizeof(xs[0]))
#define N_COMP (3) // RGB, not really configurable, but convenient define nevertheless

uint8_t* read_entire_file(const char* path, size_t* out_size)
{
	FILE* in;
	if (strcmp(path, "-") == 0) {
		in = stdin;
	} else {
		in = fopen(path, "rb");
		if (in == NULL) {
			fprintf(stderr, "%s: could not open\n", path);
			exit(EXIT_FAILURE);
		}
	}

	const size_t chunk_size = (1<<20);
	uint8_t* data = NULL;
	size_t n=0, cap=0;
	for (;;) {
		const size_t req_cap = n + chunk_size;
		if (cap < req_cap) {
			data = realloc(data, req_cap);
			cap = req_cap;
		}
		assert(data != NULL);
		const size_t n_read = fread(data+n, 1, chunk_size, in);
		n += n_read;
		if (n_read < chunk_size) break;
	}
	if (out_size != NULL) *out_size = n;
	return data;
}

__attribute__ ((noreturn))
static void SDL2FATAL(void)
{
	fprintf(stderr, "SDL2: %s\n", SDL_GetError());
	exit(EXIT_FAILURE);
}

#define EMIT_CURVE_TYPES \
	X(hilbert)

enum curve_type {
	#define X(NAME) CURVE_TYPE_ ## NAME,
	EMIT_CURVE_TYPES
	#undef X
};

static int starts_with(const char* s, const char* prefix, const char** out_tail)
{
	const size_t np = strlen(prefix);
	if (strlen(s) >= np && memcmp(s, prefix, np) == 0) {
		if (out_tail != NULL) *out_tail = s+np;
		return 1;
	} else {
		return 0;
	}
}

static int window_width, window_height;
static double pan_x = 0.0;
static double pan_y = 0.0;
static double scale = 1.0;

static void map_screen_to_local(double sx, double sy, double* out_lx, double* out_ly)
{
	const double mid_x = (double)window_width * 0.5;
	const double mid_y = (double)window_height * 0.5;
	double lx = (sx - mid_x - pan_x) / scale;
	double ly = (sy - mid_y - pan_y) / scale;
	if (out_lx) *out_lx = lx;
	if (out_ly) *out_ly = ly;
}

struct lindenmayer_system_stack_entry {
	int rule_index;
	int pc;
};

struct lindenmayer_system_rule {
	const char* ops;
	int length;
};

struct lindenmayer_system {
	int n_rules;
	struct lindenmayer_system_rule rules[1<<8];
	int depth;
	struct lindenmayer_system_stack_entry stack[1<<8];
	int stack_height;
	int state;
	int x,y,direction;
};

static void lindenmayer_system_init(struct lindenmayer_system* lsys, int n_rules, const char** rules, int depth)
{
	assert(n_rules <= ARRAY_LENGTH(lsys->rules));
	memset(lsys, 0, sizeof *lsys);
	lsys->depth = depth;
	lsys->n_rules = n_rules;
	for (int i = 0; i < n_rules; i++) {
		struct lindenmayer_system_rule* rule = &lsys->rules[i];
		rule->ops = strdup(rules[i]);
		rule->length = strlen(rule->ops);
	}
}

static int lindenmayer_system_next_coord(struct lindenmayer_system* lsys, int* out_x, int* out_y)
{
	if (lsys->state >= 2) return 0;
	if (lsys->state == 0) {
		// it's awkward to emit either the first or last point
		if (out_x) *out_x = 0;
		if (out_y) *out_y = 0;
		lsys->stack_height = 1;
		lsys->stack[0].rule_index = 0;
		lsys->stack[0].pc = 0;
		lsys->state = 1;
		return 1;
	}
	assert(lsys->state == 1);
	for (;;) {
		if (lsys->stack_height == 0) {
			lsys->state = 2;
			return 0;
		}
		struct lindenmayer_system_stack_entry* e = &lsys->stack[lsys->stack_height-1];
		assert(0 <= e->rule_index && e->rule_index < lsys->n_rules);
		struct lindenmayer_system_rule* rule = &lsys->rules[e->rule_index];
		if (e->pc >= rule->length) {
			lsys->stack_height--;
			if (lsys->stack_height > 0) {
				lsys->stack[lsys->stack_height-1].pc++;
			}
			continue;
		}
		assert(e->pc < rule->length);
		char op = rule->ops[e->pc];
		int new_rule_index = -1;
		int did_emit = 0;
		switch (op) {
		case '^': {
			switch (lsys->direction) {
			case 0: lsys->x++; break;
			case 1: lsys->y++; break;
			case 2: lsys->x--; break;
			case 3: lsys->y--; break;
			default: assert(!"bad state");
			}
			if (out_x) *out_x = lsys->x;
			if (out_y) *out_y = lsys->y;
			did_emit = 1;
		}	break;
		case '+': {
			lsys->direction = (lsys->direction + 1) & 3;
		}	break;
		case '-': {
			lsys->direction = (lsys->direction + 3) & 3;
		}	break;
		default: {
			assert('0' <= op && op <= '9');
			new_rule_index = op - '0';
		}	break;
		}
		if (new_rule_index >= 0 && lsys->stack_height < lsys->depth) {
			assert(lsys->stack_height < ARRAY_LENGTH(lsys->stack));
			lsys->stack[lsys->stack_height].rule_index = new_rule_index;
			lsys->stack[lsys->stack_height].pc = 0;
			lsys->stack_height++;
		} else {
			assert(lsys->stack_height > 0);
			lsys->stack[lsys->stack_height-1].pc++;
		}
		if (did_emit) return 1;
	}
}

int main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <input path> [option]...\n", argv[0]);
		assert((N_COMP == 3) && "usage text is lying now?");
		fprintf(stderr, "Input data must be an RGB byte stream, 3 bytes per pixel (R0,G0,B0,R1,G1,...)\n");
		fprintf(stderr, "Options:\n");
		fprintf(stderr, "  exit            Exit program on click\n");
		fprintf(stderr, "  write:<PATH>    Write 1D coordinate to file on click\n");
		fprintf(stderr, "  clipboard       Write 1D coordinate to clipboard on click\n");
		// NOTE insert+fix usage if I ever get more than one curve type
		//fprintf(stderr, "  curve:<TYPE>    Select curve type (default: hilbert)\n");
		fprintf(stderr, "HINT: you can add any number of click action options.\n");
		fprintf(stderr, "HINT: \"-\" works as path for both input (stdin) and output (stdout)\n");
		fprintf(stderr, "HINT: you can pan+zoom with RMB+mouse wheel\n");
		exit(EXIT_FAILURE);
	}

	// parse options
	int exit_on_click = 0;
	int copy_to_clipboard_on_click = 0;
	enum curve_type curve_type = CURVE_TYPE_hilbert;
	const char* output_paths[256];
	int n_output_paths = 0;
	for (int i = 2; i < argc; i++) {
		const char* option = argv[i];
		const char* tail = NULL;
		if (strcmp("exit", option) == 0) {
			exit_on_click = 1;
		} else if (strcmp("clipboard", option) == 0) {
			copy_to_clipboard_on_click = 1;
		} else if (starts_with(option, "write:", &tail)) {
			assert((n_output_paths < ARRAY_LENGTH(output_paths)) && "my that's a lot of outputs!");
			output_paths[n_output_paths++] = strdup(tail);
		} else if (starts_with(option, "curve:", &tail)) {
			int found = 0;
			#define X(NAME) \
				if (!found && strcmp(#NAME, tail) == 0) { \
					found=1; \
					curve_type = CURVE_TYPE_ ## NAME; \
				}
			EMIT_CURVE_TYPES
			#undef X
			if (!found) {
				fprintf(stderr, "Invalid curve type: %s\n", tail);
				exit(EXIT_FAILURE);
			}
		} else {
			fprintf(stderr, "Invalid option: %s\n", option);
			exit(EXIT_FAILURE);
		}
	}

	size_t raw_input_data_size;
	uint8_t* data = read_entire_file(argv[1], &raw_input_data_size);
	assert(data != NULL);
	if ((raw_input_data_size % N_COMP) != 0) {
		fprintf(stderr, "%s: number of bytes must be a multiple of %d\n", argv[1], N_COMP);
		exit(EXIT_FAILURE);
	}
	const size_t input_length = raw_input_data_size / N_COMP;

	if (SDL_Init(SDL_INIT_VIDEO) != 0) SDL2FATAL();

	SDL_Window* window = SDL_CreateWindow(
		"uncurl",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		1024, 1024,
		SDL_WINDOW_RESIZABLE);
	SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (renderer == NULL) SDL2FATAL();

	// figure out an image size that fits all the data; basically
	// 1<<ceil(log2(sqrt(n))) but without floating point math
	int width_log2 = 0;
	while ((1 << (2*width_log2)) < input_length) width_log2++;
	const int width = 1<<width_log2;
	const int n_pixels = 1<<(2*width_log2);

	uint8_t* image = calloc(n_pixels, N_COMP);
	size_t* reverse = calloc(n_pixels, sizeof reverse[0]);
	memset(reverse, -1, n_pixels*sizeof(reverse[0]));

	// draw curve
	struct lindenmayer_system lsys;
	switch (curve_type) {
	case CURVE_TYPE_hilbert: {
		const char* rules[] = {
			"+1^-0^0-^1+",
			"-0^+1^1+^0-",
		};
		lindenmayer_system_init(&lsys, ARRAY_LENGTH(rules), rules, width_log2);
	}	break;
	default: assert(!"unreachable");
	}
	{
		int px, py;
		uint8_t* rp = data;
		int point_index = 0;
		while (lindenmayer_system_next_coord(&lsys, &px, &py)) {
			assert(0 <= px && px < width);
			assert(0 <= py && py < width);
			const int image_index = (py << width_log2) + px;
			assert(0 <= image_index && image_index < n_pixels);
			uint8_t* wp = &image[image_index*N_COMP];
			for (int c=0; c<N_COMP; c++) *(wp++) = *(rp++);
			reverse[image_index] = point_index++;
		}
	}

	SDL_Texture* texture;
	{
		assert((N_COMP == 3) && "hardcoded pixel format needs N_COMP==3");
		const Uint32 desired_format = SDL_PIXELFORMAT_RGB24;
		const int desired_access = SDL_TEXTUREACCESS_STATIC;
		texture = SDL_CreateTexture(renderer, desired_format, desired_access, width, width);
		if (texture == NULL) SDL2FATAL();
		// sanity check (don't know if this is necessary)
		Uint32 actual_format;
		int actual_access, actual_width, actual_height;
		if (SDL_QueryTexture(texture, &actual_format, &actual_access, &actual_width, &actual_height) < 0) SDL2FATAL();
		assert(actual_format == desired_format);
		assert(actual_access == desired_access);
		assert(actual_width == width);
		assert(actual_height == width); // width==height
	}
	SDL_UpdateTexture(texture, NULL, image, N_COMP*width);

	int is_exiting = 0;
	int is_panning = 0;
	while (!is_exiting) {
		SDL_GetWindowSize(window, &window_width, &window_height);

		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			if (ev.type == SDL_QUIT) {
				is_exiting = 1;
			} else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE) {
				is_exiting = 1;
			} else if (ev.type == SDL_KEYDOWN) {
				if (ev.key.keysym.sym == SDLK_ESCAPE) is_exiting = 1;
			} else if (ev.type == SDL_MOUSEBUTTONDOWN) {
				const int b = ev.button.button;
				if (b == MOUSE_BUTTON_SELECT) {
					const int mx = ev.button.x;
					const int my = ev.button.y;
					double lx,ly;
					map_screen_to_local(mx, my, &lx, &ly);
					lx += width/2;
					ly += width/2;
					if (0 <= lx && lx <= width && 0 <= ly && ly <= width) {
						const int ix = lx;
						const int iy = ly;
						const int ii = (iy << width_log2) + ix;
						if (0 <= ii && ii < n_pixels) {
							const int iii = reverse[ii];
							if (iii >= 0) {
								if (copy_to_clipboard_on_click) {
									char buf[1<<10];
									snprintf(buf, sizeof buf, "%d", iii);
									SDL_SetClipboardText(buf);
								}
								for (int j = 0; j < n_output_paths; j++) {
									const char* path = output_paths[j];
									if (strcmp("-",path) == 0) {
										printf("%d\n", iii);
									} else {
										FILE* out = fopen(path, "w");
										assert(out != NULL);
										fprintf(out, "%d\n", iii);
										fclose(out);
									}
								}
								if (exit_on_click) is_exiting = 1;
							}
						}
					}
				} else if (b == MOUSE_BUTTON_PAN) {
					is_panning = 1;
				}
			} else if (ev.type == SDL_MOUSEBUTTONUP) {
				const int b = ev.button.button;
				if (b == MOUSE_BUTTON_PAN) {
					is_panning = 0;
				}
			} else if (ev.type == SDL_MOUSEMOTION) {
				if (is_panning) {
					pan_x += (double)ev.motion.xrel;
					pan_y += (double)ev.motion.yrel;
				}
			} else if (ev.type == SDL_MOUSEWHEEL) {
				const double mx = ev.wheel.mouseX;
				const double my = ev.wheel.mouseY;
				double plx,ply;
				map_screen_to_local(mx, my, &plx, &ply);
				scale *= pow(MOUSE_WHEEL_SENSITIVITY, ev.wheel.y);
				double lx,ly;
				map_screen_to_local(mx, my, &lx, &ly);
				pan_x += (lx-plx)*scale;
				pan_y += (ly-ply)*scale;
			}
		}

		// There are moiré pattern problems both when zooming in and
		// out. SDL_ScaleModeLinear offers a slight improvement when
		// zooming out but mipmapping is required to solve the problem
		// properly (however, SDL2 has no mipmap support). A "pixel art
		// shader" is required to solve the problem with zooming in; it
		// anti-aliases the edges between texels without blurring the
		// image. I suppose that none of these problems are worth the
		// loss of portability and added complexity.
		SDL_SetTextureScaleMode(texture, scale > 1.0 ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);

		SDL_RenderClear(renderer);
		SDL_Rect dst;
		{
			const int ex = (double)width*0.5*scale;
			const int mid_x = (window_width >> 1) + pan_x;
			const int mid_y = (window_height >> 1) + pan_y;
			dst.x = mid_x-ex;
			dst.y = mid_y-ex;
			dst.w = dst.h = ex*2;
		}
		SDL_RenderCopy(renderer, texture, NULL, &dst);
		SDL_RenderPresent(renderer);
	}

	return EXIT_SUCCESS;
}
/*
===============================================================================
Public Domain (www.unlicense.org)
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or distribute this
software, either in source code form or as a compiled binary, for any purpose,
commercial or non-commercial, and by any means.
In jurisdictions that recognize copyright laws, the author or authors of this
software dedicate any and all copyright interest in the software to the public
domain. We make this dedication for the benefit of the public at large and to
the detriment of our heirs and successors. We intend this dedication to be an
overt act of relinquishment in perpetuity of all present and future rights to
this software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
===============================================================================
*/
