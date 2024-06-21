

/*
    This example shows how to use nothing but stb_truetype along with GLAD2
    libraries and nothing else required to render text.

    GLFW3 is used to make the window. Nothing is dependant on GLFW3 library.
    So SDL2 could be used and just replace GLFW3 functions if desired.

    This has been tested to work with GCC 13.2.

    Minimal OpenGL Version REQUIRED is 4.3. Most PCs today can handle this.
	APPLE MAC - OSX Cannot !
*/

#include <stdio.h>  // FILE

#define GLAD_GL_IMPLEMENTATION
#include "libs/glad2/include/glad/gl.h"
#include "libs/glfw-master/include/GLFW/glfw3.h"  // -lgdi32   define _GLFW_WIN32

#define STB_TRUETYPE_IMPLEMENTATION
#include "libs/stb-master/stb_truetype.h"

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
unsigned int LoadEmbeddedShaders(const char* vertex_shader_text, const char* fragment_shader_text);
void* loadFile(const char* filename);

uint32_t text_atlas_width = 512, text_atlas_height = 512;
unsigned int textTexture = 0;
int window_width  = 1280;
int window_height = 720;
int rect_buffer_filled = 0;
GLFWwindow* window = NULL;

typedef struct {
	const char* text;
	const char* textEnd;
	uint32_t    textCodepoint;
} text_s;

typedef struct
{
    uint8_t r, g, b, a;
} rgba_s;

typedef struct
{
    int16_t left, top, right, bottom;
} rect_s;

typedef struct
{
    rect_s  pos;
    rect_s  tex_coords;
    rgba_s  color;
    float   subpixel_shift;
} rect_instance_s;

typedef struct
{
    int     filled;
    rect_s  tex_coords;
    int     glyph_index;
    int     distance_from_baseline_to_top_px;
} text_atlas_item_s;

text_atlas_item_s text_atlas_items[127] = {};
rect_instance_s rect_buffer[255];

text_s character(text_s textCharacter)
{
	textCharacter.textCodepoint = 0;
	if (textCharacter.text == textCharacter.textEnd)
		return textCharacter;

	uint8_t byte = *textCharacter.text;
	if (byte == 0)
		return textCharacter;
	textCharacter.text++;

	int leading_ones = __builtin_clz(~byte << 24);

	if (leading_ones != 1)
    {
		int data_bits_in_first_byte = 8 - 1 - leading_ones;
		textCharacter.textCodepoint = byte & ~(0xFFFFFFFF << data_bits_in_first_byte);

		ssize_t additional_bytes = leading_ones - 1;

		if (textCharacter.text + additional_bytes <= textCharacter.textEnd) {
			for(ssize_t i = 0; i < additional_bytes; i++) {
				byte = *textCharacter.text;

				if ( (byte & 0xC0) == 0x80 ) {
					textCharacter.textCodepoint <<= 6;
					textCharacter.textCodepoint |= byte & 0x3F;
				} else {
					textCharacter.textCodepoint = 0xFFFD;
					break;
				}

				textCharacter.text++;
			}
		} else {
			textCharacter.textCodepoint = 0xFFFD;
			textCharacter.text = textCharacter.textEnd;
		}
	} else {
		while ( (*(textCharacter.text) & 0xC0) == 0x80 && textCharacter.text < textCharacter.textEnd )
			textCharacter.text++;
		textCharacter.textCodepoint = 0xFFFD;
	}

	return textCharacter;
}

// This function has a bug that won't allow more then one font to be used correctly.
// TODO : FIX IT !
void AddText(const char* text, float font_size_pt, float pos_x, float pos_y, rgba_s text_color, stbtt_fontinfo font_info)
{
    float font_size_px = font_size_pt * 1.333333;
    float font_scale = stbtt_ScaleForMappingEmToPixels(&font_info, font_size_px);

    int font_ascent = 0, font_descent = 0, font_line_gap = 0;
    stbtt_GetFontVMetrics(&font_info, &font_ascent, &font_descent, &font_line_gap);
    float line_height = (font_ascent - font_descent + font_line_gap) * font_scale;
    float baseline = font_ascent * font_scale;

    float current_x = pos_x;
    float current_y = pos_y + round(baseline);

    uint32_t prev_codepoint = 0;

    text_s textStr = character((text_s){.text = text, .textEnd = (const char*)UINTPTR_MAX, .textCodepoint = 0});

    for(; textStr.textCodepoint != 0; textStr = character(textStr))
    {
        uint32_t codepoint = textStr.textCodepoint;
        if (prev_codepoint)
            current_x += stbtt_GetCodepointKernAdvance(&font_info, prev_codepoint, codepoint) * font_scale;
        prev_codepoint = codepoint;

        if (codepoint == '\n') {
            current_x = pos_x;
            current_y += round(line_height);
        } else {
            int horizontal_filter_padding = 1, subpixel_positioning_left_padding = 1;
            assert(codepoint <= 127);
            text_atlas_item_s text_atlas_item = text_atlas_items[codepoint];
            if(text_atlas_item.filled) {
            } else {
                int glyph_index = stbtt_FindGlyphIndex(&font_info, codepoint);

                int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
                stbtt_GetGlyphBitmapBox(&font_info, glyph_index, font_scale, font_scale, &x0, &y0, &x1, &y1);
                int glyph_width_px = x1 - x0, glyph_height_px = y1 - y0;
                int distance_from_baseline_to_top_px = -y0;

                if (glyph_width_px > 0 && glyph_height_px > 0) {
                    int padded_glyph_width_px  = subpixel_positioning_left_padding + horizontal_filter_padding + glyph_width_px + horizontal_filter_padding;
                    int padded_glyph_height_px = glyph_height_px;

                    int atlas_item_width = 32, atlas_item_height = 32;
                    int atlas_item_x = (codepoint % (text_atlas_width  / atlas_item_width )) * atlas_item_width;
                    int atlas_item_y = (codepoint / (text_atlas_height / atlas_item_height)) * atlas_item_height;
                    assert(padded_glyph_width_px <= atlas_item_width && padded_glyph_height_px <= atlas_item_height);

                    int horizontal_resolution = 3;
                    int bitmap_stride     = atlas_item_width * horizontal_resolution;
                    int bitmap_size       = bitmap_stride * atlas_item_height;
                    uint8_t* glyph_bitmap = calloc(1, bitmap_size);

                    int glyph_offset_x = (subpixel_positioning_left_padding + horizontal_filter_padding) * horizontal_resolution;

                    stbtt_MakeGlyphBitmap(&font_info,
                        glyph_bitmap + glyph_offset_x,
                        atlas_item_width * horizontal_resolution, atlas_item_height, bitmap_stride,
                        font_scale * horizontal_resolution, font_scale,
                        glyph_index
                    );

                    uint8_t* atlas_item_bitmap = calloc(1, bitmap_size);

                    uint8_t filter_weights[5] = { 0x08, 0x4D, 0x56, 0x4D, 0x08 };
                    for (int y = 0; y < padded_glyph_height_px; y++)
                    {
                        int x_end = padded_glyph_width_px * horizontal_resolution - 1;
                        for (int x = 4; x < x_end; x++)
                        {
                            int sum = 0, filter_weight_index = 0, kernel_x_end = (x == x_end - 1) ? x + 1 : x + 2;
                            for (int kernel_x = x - 2; kernel_x <= kernel_x_end; kernel_x++)
                            {
                                assert(kernel_x >= 0 && kernel_x < x_end + 1);
                                assert(y        >= 0 && y        < padded_glyph_height_px);
                                int offset = kernel_x + y*bitmap_stride;
                                assert(offset >= 0 && offset < bitmap_size);
                                sum += glyph_bitmap[offset] * filter_weights[filter_weight_index++];
                            }

                            sum = sum / 255;
                            atlas_item_bitmap[x + y*bitmap_stride] = (sum > 255) ? 255 : sum;
                        }
                    }
                    free(glyph_bitmap);

                    glTextureSubImage2D(textTexture, 0, atlas_item_x, atlas_item_y, atlas_item_width, atlas_item_height, GL_RGB, GL_UNSIGNED_BYTE, atlas_item_bitmap);
                    free(atlas_item_bitmap);

                    text_atlas_item.tex_coords.left   = atlas_item_x;
                    text_atlas_item.tex_coords.top    = atlas_item_y;
                    text_atlas_item.tex_coords.right  = atlas_item_x + padded_glyph_width_px;
                    text_atlas_item.tex_coords.bottom = atlas_item_y + padded_glyph_height_px;
                } else {
                    text_atlas_item.tex_coords.left   = -1;
                    text_atlas_item.tex_coords.top    = -1;
                    text_atlas_item.tex_coords.right  = -1;
                    text_atlas_item.tex_coords.bottom = -1;
                }

                text_atlas_item.glyph_index                      = glyph_index;
                text_atlas_item.distance_from_baseline_to_top_px = distance_from_baseline_to_top_px;
                text_atlas_item.filled                           = 1;
                text_atlas_items[codepoint] = text_atlas_item;
            }

            int glyph_advance_width = 0, glyph_left_side_bearing = 0;
            stbtt_GetGlyphHMetrics(&font_info, text_atlas_item.glyph_index, &glyph_advance_width, &glyph_left_side_bearing);

            if (text_atlas_item.tex_coords.left != -1) {
                float glyph_pos_x = current_x + (glyph_left_side_bearing * font_scale);
                float glyph_pos_x_px = 0;
                float glyph_pos_x_subpixel_shift = modff(glyph_pos_x, &glyph_pos_x_px);
                float glyph_pos_y_px = current_y - text_atlas_item.distance_from_baseline_to_top_px;
                int glyph_width_with_horiz_filter_padding = text_atlas_item.tex_coords.right  - text_atlas_item.tex_coords.left;
                int glyph_height                          = text_atlas_item.tex_coords.bottom - text_atlas_item.tex_coords.top;

                rect_buffer[rect_buffer_filled++] = (rect_instance_s)
                {
                    .pos.left   = glyph_pos_x_px - (subpixel_positioning_left_padding + horizontal_filter_padding),
                    .pos.right  = glyph_pos_x_px - (subpixel_positioning_left_padding + horizontal_filter_padding) + glyph_width_with_horiz_filter_padding,
                    .pos.top    = glyph_pos_y_px,
                    .pos.bottom = glyph_pos_y_px + glyph_height,
                    .subpixel_shift = glyph_pos_x_subpixel_shift,
                    .tex_coords     = text_atlas_item.tex_coords,
                    .color          = text_color
                };
            }

            current_x += glyph_advance_width * font_scale;
        }
    }
}

int main(int argc, char** argv)
{
    if(!glfwInit())
    {
        printf("ERROR : EPIC GLFW3 FAIL !\n");
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(window_width, window_height, "ThatText Window", NULL, NULL);
    if(window == NULL)
    {
        printf("ERROR : UNABLE TO CREATE GLFW WINDOW !\n");
        glfwTerminate();
        return -2;
    }

    glfwSwapInterval(1);

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	gladLoadGL(glfwGetProcAddress);  // Loads OpenGL

	printf("OPENGL VERSION : %s\n", glGetString(GL_VERSION));

	const char* vertexTextShader =
			"#version 430 core\n"
			"layout(location = 0) in uvec2 ltrb_index;\n"
			"layout(location = 1) in vec4  rect_ltrb;\n"
			"layout(location = 2) in vec4  rect_tex_ltrb;\n"
			"layout(location = 3) in vec4  rect_color;\n"
			"out vec2  tex_coords;\n"
			"out vec4  color;\n"
			"layout(location = 0) uniform vec2 half_viewport_size;\n"
			"void main()\n"
			"{\n"
			"    color = vec4(rect_color.rgb * rect_color.a, rect_color.a);\n"
			"    vec2 pos   = vec2(rect_ltrb[ltrb_index.x],     rect_ltrb[ltrb_index.y]);\n"
			"    tex_coords = vec2(rect_tex_ltrb[ltrb_index.x], rect_tex_ltrb[ltrb_index.y]);\n"
			"    vec2 axes_flip  = vec2(1, -1);\n"
			"    vec2 pos_in_ndc = (pos / half_viewport_size - 1.0) * axes_flip;\n"
			"    gl_Position = vec4(pos_in_ndc, 0, 1);\n"
			"}\n";


    const char* fragmentTextShader =
			"#version 430 core\n"
			"in  vec2  tex_coords;\n"
			"in  vec4  color;\n"
			"out vec4 fragment_color;\n"
			"out vec4 blend_weights;\n"
			"uniform sampler2DRect glyph_atlas;\n"
			"void main()\n"
			"{\n"
			"    vec3 current  = texelFetch(glyph_atlas, ivec2(tex_coords) + ivec2( 0, 0)).rgb;\n"
			"    vec3 previous = texelFetch(glyph_atlas, ivec2(tex_coords) + ivec2(-1, 0)).rgb;\n"
			"    float r = current.r, g = current.g, b = current.b;\n"
			"    vec3 pixel_coverages = vec3(r, g, b);\n"
			"    fragment_color = color * vec4(pixel_coverages, 1);\n"
			"    blend_weights = vec4(color.a * pixel_coverages, color.a);\n"
			"}\n";

	GLuint textShaderProgram = LoadEmbeddedShaders(vertexTextShader, fragmentTextShader);

	struct { uint16_t ltrb_index_x, ltrb_index_y; } rect_vertices[] = {
		{ 0, 1 }, // left  top
		{ 0, 3 }, // left  bottom
		{ 2, 1 }, // right top
		{ 0, 3 }, // left  bottom
		{ 2, 3 }, // right bottom
		{ 2, 1 }, // right top
	};

	GLuint vbo1 = 0;
	glCreateBuffers(1, &vbo1);
	glNamedBufferStorage(vbo1, sizeof(rect_vertices), rect_vertices, 0);

	GLuint vbo2 = 0;
	glCreateBuffers(1, &vbo2);

	GLuint vao = 0;
	glCreateVertexArrays(1, &vao);
		glVertexArrayVertexBuffer(vao, 0, vbo1,  0, sizeof(rect_vertices[0]));
		glVertexArrayVertexBuffer(vao, 1, vbo2, 0, sizeof(rect_instance_s));
		glVertexArrayBindingDivisor(vao, 1, 1);
		glEnableVertexArrayAttrib( vao, 0);
		glVertexArrayAttribBinding(vao, 0, 0);
		glVertexArrayAttribIFormat(vao, 0, 2, GL_UNSIGNED_SHORT, 0);
		glEnableVertexArrayAttrib( vao, 1);
		glVertexArrayAttribBinding(vao, 1, 1);
		glVertexArrayAttribFormat( vao, 1, 4, GL_SHORT, 0, offsetof(rect_instance_s, pos));
		glEnableVertexArrayAttrib( vao, 2);
		glVertexArrayAttribBinding(vao, 2, 1);
		glVertexArrayAttribFormat( vao, 2, 4, GL_SHORT, 0, offsetof(rect_instance_s, tex_coords));
		glEnableVertexArrayAttrib( vao, 3);
		glVertexArrayAttribBinding(vao, 3, 1);
		glVertexArrayAttribFormat( vao, 3, 4, GL_UNSIGNED_BYTE, 1, offsetof(rect_instance_s, color));
		glEnableVertexArrayAttrib( vao, 4);
		glVertexArrayAttribBinding(vao, 4, 1);
		glVertexArrayAttribFormat( vao, 4, 1, GL_FLOAT, 0, offsetof(rect_instance_s, subpixel_shift));
	glCreateTextures(GL_TEXTURE_RECTANGLE, 1, &textTexture);
	glTextureStorage2D(textTexture, 1, GL_RGB8, text_atlas_width, text_atlas_height);

	//void* font_data1 = loadFile("fonts/DroidSansJapanese.ttf");  // TODO : Get this to work !
	//void* font_data1 = loadFile("fonts/DroidSerif-Regular.ttf"); // TODO : Get multi-fonts working !
	void* font_data1 = loadFile("fonts/DroidSerif-Bold.ttf");
	stbtt_fontinfo font_info1;
	stbtt_InitFont(&font_info1, font_data1, 0);
	int fontSize = 20;  // TODO : Get Multi-FontSize working !

    const char* text1 = "We have TEXT with the stb_truetype & GLAD2 libraries in OpenGL 4.3 !\n\n NO MORE BUGGY FREETYPE LIBRARY !";
    const char* text2 = "I still need to get Multi-Fonts, Multi-FontSize, as well as non-english fonts all working !\n\n\nNOTE : APPLE DOES NOT SUPPORT OPENGL ANY LONGER,\nSO THIS WILL NOT WORK ON APPLE !";
    const char* text3 = "\"Much love to my youtube subscribers\" - ThatOSDev";

    rgba_s text_color1 = {218, 255, 255, 255};
    rgba_s text_color2 = {155, 255, 155, 255};
    rgba_s text_color3 = {255, 155, 155, 255};

    AddText(text1, fontSize, 10, 30,  text_color1, font_info1);
    AddText(text2, fontSize, 30, 200, text_color2, font_info1);
    AddText(text3, fontSize, 70, 400, text_color3, font_info1);

    free(font_data1);  // We are done with the Font. Delete it from memory.

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC1_COLOR);

    glClearColor(0.15, 0.15, 0.2, 1.0);

	while(!glfwWindowShouldClose(window))
    {
        glClear(GL_COLOR_BUFFER_BIT);

        glNamedBufferData(vbo2, rect_buffer_filled * sizeof(rect_instance_s), rect_buffer, GL_STATIC_DRAW);

        glBindVertexArray(vao);
            glUseProgram(textShaderProgram);
                glProgramUniform2f(textShaderProgram, 0, window_width / 2.0f, window_height / 2.0f);
                glBindTextureUnit(0, textTexture);
                glDrawArraysInstanced(GL_TRIANGLES, 0, 6, rect_buffer_filled);
            glUseProgram(0);
        glBindVertexArray(0);

        glInvalidateBufferData(vbo2);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glDeleteVertexArrays(1, &vao);
	glDeleteBuffers(1, &vbo1);
	glDeleteBuffers(1, &vbo2);
	glDeleteProgram(textShaderProgram);
	glDeleteTextures(1, &textTexture);

    // GLFW3 seems to delay when trying to destroy window while in Debug-Mode.
    // However, Release-Mode is fine.
    if(window)
    {
        glfwDestroyWindow(window);
        printf("WINDOW DESTROYED\n");
    }

    glfwTerminate();
    printf("GLFW TERMINATED\n");

	return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    window_width  = width;
    window_height = height;

    glViewport(0, 0, window_width, window_height);
}

unsigned int LoadEmbeddedShaders(const char* vertex_shader_text, const char* fragment_shader_text)
{
    GLuint vertex_shader, fragment_shader, program;

    int success;
    char infoLog[512];

    vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex_shader, 1, &vertex_shader_text, NULL);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &success);
    if(!success)
    {
        printf("Vertex Shader Error\n");
        glGetShaderInfoLog(vertex_shader, 512, NULL, infoLog);
        perror(infoLog);
    }

    fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment_shader, 1, &fragment_shader_text, NULL);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &success);
    if(!success)
    {
        printf("Fragment Shader Error\n");
        glGetShaderInfoLog(fragment_shader, 512, NULL, infoLog);
        perror(infoLog);
    }

    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if(!success)
    {
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        perror(infoLog);
    }

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    return program;
}

void* loadFile(const char* filename)
{
	FILE* fPtr = fopen(filename, "rb");
	if (fPtr == NULL)
    {
        printf("ERROR : UNABLE TO LOAD FILE : %s\n", filename);
        glfwDestroyWindow(window);
        glfwTerminate();
        exit(1);
    }

	fseek(fPtr, 0, SEEK_END);
	long filesize = ftell(fPtr);
	fseek(fPtr, 0, SEEK_SET);
	char* data = malloc(filesize + 1);
	fread(data, 1, filesize, fPtr);
	fclose(fPtr);

	data[filesize] = '\0';

	return (void*)data;
}

