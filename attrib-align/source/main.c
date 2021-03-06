#include <3ds.h>
#include <citro3d.h>
#include <string.h>
#include <stdio.h>
#include "vshader_shbin.h"

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define CLEAR_COLOR 0x68B0D8FF

#define DISPLAY_TRANSFER_FLAGS \
  (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
  GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
  GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))


static DVLB_s* vshader_dvlb;
static shaderProgram_s program;
static int uLoc_projection;
static C3D_Mtx projection;

static void* vbo_data;
size_t vbo_size = 1 * 1024; // 1 KiB should be plenty..

static unsigned int base_offset = 0;
static unsigned int passed_base_offset = 0;

static int stride_offset = 0;
static int passed_stride_offset = 0;

static void sceneInit(void)
{
  // Load the vertex shader, create a shader program and bind it
  vshader_dvlb = DVLB_ParseFile((u32*)vshader_shbin, vshader_shbin_size);
  shaderProgramInit(&program);
  shaderProgramSetVsh(&program, &vshader_dvlb->DVLE[0]);
  C3D_BindProgram(&program);

  // Get the location of the uniforms
  uLoc_projection = shaderInstanceGetUniformLocation(program.vertexShader, "projection");

  // Compute the projection matrix
  Mtx_OrthoTilt(&projection, 0.0, 400.0, 0.0, 240.0, 0.0, 1.0);

  // Create the VBO (vertex buffer object)
  vbo_data = linearAlloc(vbo_size);

  // Configure the first fragment shading substage to just pass through the vertex color
  // See https://www.opengl.org/sdk/docs/man2/xhtml/glTexEnv.xml for more insight
  C3D_TexEnv* env = C3D_GetTexEnv(0);
  C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
  C3D_TexEnvOp(env, C3D_Both, 0, 0, 0);
  C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}


void draw(size_t stride, unsigned int attrib_count, unsigned int permutation, int pad_type, int pad_count, int type) {

  // Configure buffers
  C3D_BufInfo* bufInfo = C3D_GetBufInfo();
  BufInfo_Init(bufInfo);
  BufInfo_Add(bufInfo, (const void*)((uintptr_t)vbo_data + passed_base_offset), stride, attrib_count, permutation);

  // Configure attributes for use with the vertex shader
  C3D_AttrInfo* attrInfo = C3D_GetAttrInfo();
  AttrInfo_Init(attrInfo);
  AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 2); // v0=position
  AttrInfo_AddLoader(attrInfo, 1, pad_type, pad_count); // v1=pad
  AttrInfo_AddLoader(attrInfo, 2, type, 3); // v2=color

  // Update the uniforms
  C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, uLoc_projection, &projection);

  // Draw the VBO
  C3D_DrawArrays(GPU_TRIANGLES, 0, 3);
}

static size_t safe_copy(u8* dest, const u8* src, size_t size) {
  size_t written = 0;
  while(size--) {
    *dest++ = *src++;
    written++;
  }
  return written;
}

static int sceneRender(unsigned int mode)
{

#define DRAW(stride, attrib_count, permutation, pad_gpu_type, pad_count, pad_skip, gpu_type, type, one, zero) \
  { \
    const float x[] = {200.0f, 100.0f, 300.0f}; \
    const float y[] = {200.0f,  40.0f,  40.0f}; \
    const type gpu_one = (one); \
    const type gpu_zero = (zero); \
    memset(vbo_data, 0x00, vbo_size); \
    for(unsigned int i = 0; i < 3; i++) { \
      uintptr_t base_buffer = (uintptr_t)vbo_data + base_offset + ((stride) + stride_offset) * i; \
      /* Write position */ \
      base_buffer += safe_copy((u8*)base_buffer, (const u8*)&x[i], sizeof(float)); \
      base_buffer += safe_copy((u8*)base_buffer, (const u8*)&y[i], sizeof(float)); \
      /* Add expected padding */ \
      base_buffer += (pad_skip); \
      /* Add color (which is what we are trying to have in the right spot + visualize) */ \
      for(unsigned int j = 0; j < 3; j++) { \
        base_buffer += safe_copy((u8*)base_buffer, (const u8*)((j == i) ? &gpu_one : &gpu_zero), sizeof(type)); \
      } \
    } \
    draw(stride + passed_stride_offset, attrib_count, permutation, pad_gpu_type, pad_count, gpu_type); \
  }

  if (mode == 0) {
    DRAW(0x18,
         3, 0x210,
         GPU_UNSIGNED_BYTE, 4, 4,
         GPU_FLOAT, float, 1.0f, 0.0f);
  } else if (mode == 1) {
    DRAW(0x18,
         3, 0x210,
         GPU_UNSIGNED_BYTE, 3, 4,
         GPU_FLOAT, float, 1.0f, 0.0f);
  } else if (mode == 2) {
    DRAW(0x1C,
         4, 0x21C0, // a0, 4 byte pad, a1, a2
         GPU_UNSIGNED_BYTE, 4, 8,
         GPU_FLOAT, float, 1.0f, 0.0f);  
  } else if (mode == 3) {
    DRAW(0x1C,
         4, 0x2C10, // a0, a1, 4 byte pad, a2

         //unaligned:
         // 0-7:         a0
         // 8,9,10:      a1 pad
         // 11,12,13,14: 4 byte pad
         // 15+: a2.x
         // = 7 byte padding

         //aligned:
         // 0-7:         a0
         // 8,9,10:      a1 pad
         // 11,12,13,14: 4 byte pad
         // *align*
         // 16+: a2.x
         // = 8 byte padding

         GPU_UNSIGNED_BYTE, 3, 8,
         GPU_FLOAT, float, 1.0f, 0.0f);      
  } else if (mode == 4) {
    DRAW(0x1C,
         5, 0x21C10, // a0, a1, 4 byte pad, a1, a2

         //unaligned:
         // 0-7:         a0
         // 8,9,10:      a1 pad
         // 11,12,13,14: 4 byte pad
         // 15,16,17:    a1 pad
         // 18+: a2.x
         // = 10 byte padding

         //aligned:
         // 0-7:         a0
         // 8,9,10:      a1 pad
         // 11,12,13,14: 4 byte pad
         // 15,16,17:    a1 pad
         // *align*
         // 20+: a2.x
         // = 12 byte padding

         GPU_UNSIGNED_BYTE, 3, 12,
         GPU_SHORT, int16_t, 1, 0);
  } else if (mode == 5) {
    DRAW(0x20,
         4, 0x2D10,
         GPU_UNSIGNED_BYTE, 3, 12,
         GPU_FLOAT, float, 1.0f, 0.0f);
  } else if (mode == 6) {

    // Checks wether padding attributes will be aligned to 2 byte, or 4 byte offsets

    DRAW(0x1C,
         5, 0x2C110, // a0, a1, a1, 4 byte pad, a2

         //unaligned:
         // 0-7:         a0
         // 8,9,10:      a1 pad
         // 11,12,13:    a1 pad
         // 14,15,16,17: 4 byte pad
         // 18+: a2.x
         // = 10 byte padding

         //aligned:
         // 0-7:         a0
         // 8,9,10:      a1 pad
         // 11,12,13:    a1 pad
         // *align*
         // 16,17,18,19: 4 byte pad
         // 20+: a2.x
         // = 12 byte padding

         GPU_UNSIGNED_BYTE, 3, 12,
         GPU_SHORT, int16_t, 1, 0);
  } else {
    /* Reached the end.. */
    printf("Mode %d not found!\nUsing mode 0\n", mode);
    return sceneRender(0);
  }

  return mode;
}

static void sceneExit(void)
{
  // Free the VBO
  linearFree(vbo_data);

  // Free the shader program
  shaderProgramFree(&program);
  DVLB_Free(vshader_dvlb);
}

int main()
{
  // Initialize graphics
  gfxInitDefault();

  consoleInit(GFX_BOTTOM, NULL);

  C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);

  // Initialize the render target
  C3D_RenderTarget* target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
  C3D_RenderTargetSetClear(target, C3D_CLEAR_ALL, CLEAR_COLOR, 0);
  C3D_RenderTargetSetOutput(target, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);

  // Initialize the scene
  sceneInit();

  // Main loop
  while (aptMainLoop())
  {
    hidScanInput();

    // Respond to user input
    u32 kDown = hidKeysDown();
    if (kDown & KEY_SELECT) break;

    static unsigned int mode = 0;
    {
      bool is_pressed = kDown & KEY_START;
      static bool was_pressed = true;
      if (was_pressed != is_pressed && is_pressed) {
        mode++;
        printf("Switched to mode %d!\n", mode);
      }
      was_pressed = is_pressed;
    }

    int inc = 1;
    if (kDown & KEY_Y) { inc *= 2; }
    if (kDown & KEY_X) { inc *= 4; }

    {
      bool is_pressed = kDown & KEY_A;
      static bool was_pressed = true;
      if (was_pressed != is_pressed && is_pressed) {
        base_offset += inc;
        base_offset %= 16;
        printf("Switched to base offset %d!\n", base_offset);
      }
      was_pressed = is_pressed;
    }

    {
      bool is_pressed = kDown & KEY_B;
      static bool was_pressed = true;
      if (was_pressed != is_pressed && is_pressed) {
        passed_base_offset += inc;
        passed_base_offset %= 16;
        printf("Switched to PASSED base offset %d!\n", passed_base_offset);
      }
      was_pressed = is_pressed;
    }

#if 0

    // This will freeze the PICA!
    // = stride has to match the vertex size!

    {
      bool is_pressed = kDown & KEY_UP;
      static bool was_pressed = true;
      if (was_pressed != is_pressed && is_pressed) {
        passed_stride_offset += inc;
        printf("Switched to PASSED stride offset %d!\n", passed_stride_offset);
      }
      was_pressed = is_pressed;
    }

    {
      bool is_pressed = kDown & KEY_DOWN;
      static bool was_pressed = true;
      if (was_pressed != is_pressed && is_pressed) {
        passed_stride_offset -= inc;
        printf("Switched to PASSED stride offset %d!\n", passed_stride_offset);
      }
      was_pressed = is_pressed;
    }

#endif

    {
      bool is_pressed = kDown & KEY_LEFT;
      static bool was_pressed = true;
      if (was_pressed != is_pressed && is_pressed) {
        stride_offset -= inc;
        printf("Switched to stride offset %d!\n", stride_offset);
      }
      was_pressed = is_pressed;
    }

    {
      bool is_pressed = kDown & KEY_RIGHT;
      static bool was_pressed = true;
      if (was_pressed != is_pressed && is_pressed) {
        stride_offset += inc;
        printf("Switched to stride offset %d!\n", stride_offset);
      }
      was_pressed = is_pressed;
    }

    // Render the scene
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
      C3D_FrameDrawOn(target);
      mode = sceneRender(mode);
    C3D_FrameEnd(0);
  }

  // Deinitialize the scene
  sceneExit();

  // Deinitialize graphics
  C3D_Fini();
  gfxExit();
  return 0;
}
