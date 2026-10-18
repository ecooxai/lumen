/* WebGL 1/2 implemented on the native WebGPU device (N.gpu*). GL draws are recorded into one command list
   per frame (render passes stay open across draws), uniforms are staged in a per-frame dynamic buffer. */
const LumenWebGL = (() => {
    const K = {}; const defs = 'DEPTH_BUFFER_BIT=100 STENCIL_BUFFER_BIT=400 COLOR_BUFFER_BIT=4000 POINTS=0 LINES=1 LINE_LOOP=2 LINE_STRIP=3 TRIANGLES=4 TRIANGLE_STRIP=5 TRIANGLE_FAN=6 ZERO=0 ONE=1 SRC_COLOR=300 ONE_MINUS_SRC_COLOR=301 SRC_ALPHA=302 ONE_MINUS_SRC_ALPHA=303 DST_ALPHA=304 ONE_MINUS_DST_ALPHA=305 DST_COLOR=306 ONE_MINUS_DST_COLOR=307 SRC_ALPHA_SATURATE=308 FUNC_ADD=8006 BLEND_EQUATION=8009 BLEND_EQUATION_RGB=8009 BLEND_EQUATION_ALPHA=883D FUNC_SUBTRACT=800A FUNC_REVERSE_SUBTRACT=800B BLEND_DST_RGB=80C8 BLEND_SRC_RGB=80C9 BLEND_DST_ALPHA=80CA BLEND_SRC_ALPHA=80CB CONSTANT_COLOR=8001 ONE_MINUS_CONSTANT_COLOR=8002 CONSTANT_ALPHA=8003 ONE_MINUS_CONSTANT_ALPHA=8004 BLEND_COLOR=8005 ARRAY_BUFFER=8892 ELEMENT_ARRAY_BUFFER=8893 ARRAY_BUFFER_BINDING=8894 ELEMENT_ARRAY_BUFFER_BINDING=8895 STREAM_DRAW=88E0 STATIC_DRAW=88E4 DYNAMIC_DRAW=88E8 BUFFER_SIZE=8764 BUFFER_USAGE=8765 CURRENT_VERTEX_ATTRIB=8626 FRONT=404 BACK=405 FRONT_AND_BACK=408 CULL_FACE=B44 BLEND=BE2 DITHER=BD0 STENCIL_TEST=B90 DEPTH_TEST=B71 SCISSOR_TEST=C11 POLYGON_OFFSET_FILL=8037 SAMPLE_ALPHA_TO_COVERAGE=809E SAMPLE_COVERAGE=80A0 NO_ERROR=0 INVALID_ENUM=500 INVALID_VALUE=501 INVALID_OPERATION=502 OUT_OF_MEMORY=505 CW=900 CCW=901 LINE_WIDTH=B21 ALIASED_POINT_SIZE_RANGE=846D ALIASED_LINE_WIDTH_RANGE=846E CULL_FACE_MODE=B45 FRONT_FACE=B46 DEPTH_RANGE=B70 DEPTH_WRITEMASK=B72 DEPTH_CLEAR_VALUE=B73 DEPTH_FUNC=B74 STENCIL_CLEAR_VALUE=B91 STENCIL_FUNC=B92 STENCIL_FAIL=B94 STENCIL_PASS_DEPTH_FAIL=B95 STENCIL_PASS_DEPTH_PASS=B96 STENCIL_REF=B97 STENCIL_VALUE_MASK=B93 STENCIL_WRITEMASK=B98 STENCIL_BACK_FUNC=8800 STENCIL_BACK_FAIL=8801 STENCIL_BACK_PASS_DEPTH_FAIL=8802 STENCIL_BACK_PASS_DEPTH_PASS=8803 STENCIL_BACK_REF=8CA3 STENCIL_BACK_VALUE_MASK=8CA4 STENCIL_BACK_WRITEMASK=8CA5 VIEWPORT=BA2 SCISSOR_BOX=C10 COLOR_CLEAR_VALUE=C22 COLOR_WRITEMASK=C23 UNPACK_ALIGNMENT=CF5 PACK_ALIGNMENT=D05 MAX_TEXTURE_SIZE=D33 MAX_VIEWPORT_DIMS=D3A SUBPIXEL_BITS=D50 RED_BITS=D52 GREEN_BITS=D53 BLUE_BITS=D54 ALPHA_BITS=D55 DEPTH_BITS=D56 STENCIL_BITS=D57 POLYGON_OFFSET_UNITS=2A00 POLYGON_OFFSET_FACTOR=8038 TEXTURE_BINDING_2D=8069 SAMPLE_BUFFERS=80A8 SAMPLES=80A9 SAMPLE_COVERAGE_VALUE=80AA SAMPLE_COVERAGE_INVERT=80AB COMPRESSED_TEXTURE_FORMATS=86A3 DONT_CARE=1100 FASTEST=1101 NICEST=1102 GENERATE_MIPMAP_HINT=8192 BYTE=1400 UNSIGNED_BYTE=1401 SHORT=1402 UNSIGNED_SHORT=1403 INT=1404 UNSIGNED_INT=1405 FLOAT=1406 DEPTH_COMPONENT=1902 ALPHA=1906 RGB=1907 RGBA=1908 LUMINANCE=1909 LUMINANCE_ALPHA=190A UNSIGNED_SHORT_4_4_4_4=8033 UNSIGNED_SHORT_5_5_5_1=8034 UNSIGNED_SHORT_5_6_5=8363 FRAGMENT_SHADER=8B30 VERTEX_SHADER=8B31 MAX_VERTEX_ATTRIBS=8869 MAX_VERTEX_UNIFORM_VECTORS=8DFB MAX_VARYING_VECTORS=8DFC MAX_COMBINED_TEXTURE_IMAGE_UNITS=8B4D MAX_VERTEX_TEXTURE_IMAGE_UNITS=8B4C MAX_TEXTURE_IMAGE_UNITS=8872 MAX_FRAGMENT_UNIFORM_VECTORS=8DFD SHADER_TYPE=8B4F DELETE_STATUS=8B80 LINK_STATUS=8B82 VALIDATE_STATUS=8B83 ATTACHED_SHADERS=8B85 ACTIVE_UNIFORMS=8B86 ACTIVE_ATTRIBUTES=8B89 SHADING_LANGUAGE_VERSION=8B8C CURRENT_PROGRAM=8B8D NEVER=200 LESS=201 EQUAL=202 LEQUAL=203 GREATER=204 NOTEQUAL=205 GEQUAL=206 ALWAYS=207 KEEP=1E00 REPLACE=1E01 INCR=1E02 DECR=1E03 INVERT=150A INCR_WRAP=8507 DECR_WRAP=8508 VENDOR=1F00 RENDERER=1F01 VERSION=1F02 NEAREST=2600 LINEAR=2601 NEAREST_MIPMAP_NEAREST=2700 LINEAR_MIPMAP_NEAREST=2701 NEAREST_MIPMAP_LINEAR=2702 LINEAR_MIPMAP_LINEAR=2703 TEXTURE_MAG_FILTER=2800 TEXTURE_MIN_FILTER=2801 TEXTURE_WRAP_S=2802 TEXTURE_WRAP_T=2803 TEXTURE_2D=DE1 TEXTURE=1702 TEXTURE_CUBE_MAP=8513 TEXTURE_BINDING_CUBE_MAP=8514 TEXTURE_CUBE_MAP_POSITIVE_X=8515 TEXTURE_CUBE_MAP_NEGATIVE_X=8516 TEXTURE_CUBE_MAP_POSITIVE_Y=8517 TEXTURE_CUBE_MAP_NEGATIVE_Y=8518 TEXTURE_CUBE_MAP_POSITIVE_Z=8519 TEXTURE_CUBE_MAP_NEGATIVE_Z=851A MAX_CUBE_MAP_TEXTURE_SIZE=851C ACTIVE_TEXTURE=84E0 REPEAT=2901 CLAMP_TO_EDGE=812F MIRRORED_REPEAT=8370 FLOAT_VEC2=8B50 FLOAT_VEC3=8B51 FLOAT_VEC4=8B52 INT_VEC2=8B53 INT_VEC3=8B54 INT_VEC4=8B55 BOOL=8B56 BOOL_VEC2=8B57 BOOL_VEC3=8B58 BOOL_VEC4=8B59 FLOAT_MAT2=8B5A FLOAT_MAT3=8B5B FLOAT_MAT4=8B5C SAMPLER_2D=8B5E SAMPLER_CUBE=8B60 VERTEX_ATTRIB_ARRAY_ENABLED=8622 VERTEX_ATTRIB_ARRAY_SIZE=8623 VERTEX_ATTRIB_ARRAY_STRIDE=8624 VERTEX_ATTRIB_ARRAY_TYPE=8625 VERTEX_ATTRIB_ARRAY_NORMALIZED=886A VERTEX_ATTRIB_ARRAY_POINTER=8645 VERTEX_ATTRIB_ARRAY_BUFFER_BINDING=889F IMPLEMENTATION_COLOR_READ_TYPE=8B9A IMPLEMENTATION_COLOR_READ_FORMAT=8B9B COMPILE_STATUS=8B81 LOW_FLOAT=8DF0 MEDIUM_FLOAT=8DF1 HIGH_FLOAT=8DF2 LOW_INT=8DF3 MEDIUM_INT=8DF4 HIGH_INT=8DF5 FRAMEBUFFER=8D40 RENDERBUFFER=8D41 RGBA4=8056 RGB5_A1=8057 RGB565=8D62 DEPTH_COMPONENT16=81A5 STENCIL_INDEX8=8D48 DEPTH_STENCIL=84F9 RENDERBUFFER_WIDTH=8D42 RENDERBUFFER_HEIGHT=8D43 RENDERBUFFER_INTERNAL_FORMAT=8D44 FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE=8CD0 FRAMEBUFFER_ATTACHMENT_OBJECT_NAME=8CD1 FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL=8CD2 FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE=8CD3 COLOR_ATTACHMENT0=8CE0 DEPTH_ATTACHMENT=8D00 STENCIL_ATTACHMENT=8D20 DEPTH_STENCIL_ATTACHMENT=821A NONE=0 FRAMEBUFFER_COMPLETE=8CD5 FRAMEBUFFER_INCOMPLETE_ATTACHMENT=8CD6 FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT=8CD7 FRAMEBUFFER_INCOMPLETE_DIMENSIONS=8CD9 FRAMEBUFFER_UNSUPPORTED=8CDD FRAMEBUFFER_BINDING=8CA6 RENDERBUFFER_BINDING=8CA7 MAX_RENDERBUFFER_SIZE=84E8 INVALID_FRAMEBUFFER_OPERATION=506 UNPACK_FLIP_Y_WEBGL=9240 UNPACK_PREMULTIPLY_ALPHA_WEBGL=9241 CONTEXT_LOST_WEBGL=9242 UNPACK_COLORSPACE_CONVERSION_WEBGL=9243 BROWSER_DEFAULT_WEBGL=9244';
    const defs2 = 'READ_BUFFER=C02 UNPACK_ROW_LENGTH=CF2 UNPACK_SKIP_ROWS=CF3 UNPACK_SKIP_PIXELS=CF4 PACK_ROW_LENGTH=D02 PACK_SKIP_ROWS=D03 PACK_SKIP_PIXELS=D04 COLOR=1800 DEPTH=1801 STENCIL=1802 RED=1903 RGB8=8051 RGBA8=8058 RGB10_A2=8059 TEXTURE_BINDING_3D=806A UNPACK_SKIP_IMAGES=806D UNPACK_IMAGE_HEIGHT=806E TEXTURE_3D=806F TEXTURE_WRAP_R=8072 MAX_3D_TEXTURE_SIZE=8073 UNSIGNED_INT_2_10_10_10_REV=8368 MAX_ELEMENTS_VERTICES=80E8 MAX_ELEMENTS_INDICES=80E9 TEXTURE_MIN_LOD=813A TEXTURE_MAX_LOD=813B TEXTURE_BASE_LEVEL=813C TEXTURE_MAX_LEVEL=813D MIN=8007 MAX=8008 DEPTH_COMPONENT24=81A6 MAX_TEXTURE_LOD_BIAS=84FD TEXTURE_COMPARE_MODE=884C TEXTURE_COMPARE_FUNC=884D CURRENT_QUERY=8865 QUERY_RESULT=8866 QUERY_RESULT_AVAILABLE=8867 STREAM_READ=88E1 STREAM_COPY=88E2 STATIC_READ=88E5 STATIC_COPY=88E6 DYNAMIC_READ=88E9 DYNAMIC_COPY=88EA MAX_DRAW_BUFFERS=8824 DRAW_BUFFER0=8825 MAX_FRAGMENT_UNIFORM_COMPONENTS=8B49 MAX_VERTEX_UNIFORM_COMPONENTS=8B4A SAMPLER_3D=8B5F SAMPLER_2D_SHADOW=8B62 FRAGMENT_SHADER_DERIVATIVE_HINT=8B8B PIXEL_PACK_BUFFER=88EB PIXEL_UNPACK_BUFFER=88EC PIXEL_PACK_BUFFER_BINDING=88ED PIXEL_UNPACK_BUFFER_BINDING=88EF FLOAT_MAT2x3=8B65 FLOAT_MAT2x4=8B66 FLOAT_MAT3x2=8B67 FLOAT_MAT3x4=8B68 FLOAT_MAT4x2=8B69 FLOAT_MAT4x3=8B6A SRGB=8C40 SRGB8=8C41 SRGB8_ALPHA8=8C43 COMPARE_REF_TO_TEXTURE=884E RGBA32F=8814 RGB32F=8815 RGBA16F=881A RGB16F=881B VERTEX_ATTRIB_ARRAY_INTEGER=88FD MAX_ARRAY_TEXTURE_LAYERS=88FF MIN_PROGRAM_TEXEL_OFFSET=8904 MAX_PROGRAM_TEXEL_OFFSET=8905 MAX_VARYING_COMPONENTS=8B4B TEXTURE_2D_ARRAY=8C1A TEXTURE_BINDING_2D_ARRAY=8C1D R11F_G11F_B10F=8C3A UNSIGNED_INT_10F_11F_11F_REV=8C3B RGB9_E5=8C3D UNSIGNED_INT_5_9_9_9_REV=8C3E TRANSFORM_FEEDBACK_BUFFER_MODE=8C7F TRANSFORM_FEEDBACK_VARYINGS=8C83 RASTERIZER_DISCARD=8C89 INTERLEAVED_ATTRIBS=8C8C SEPARATE_ATTRIBS=8C8D TRANSFORM_FEEDBACK_BUFFER=8C8E RGBA32UI=8D70 RGB32UI=8D71 RGBA16UI=8D76 RGB16UI=8D77 RGBA8UI=8D7C RGB8UI=8D7D RGBA32I=8D82 RGB32I=8D83 RGBA16I=8D88 RGB16I=8D89 RGBA8I=8D8E RGB8I=8D8F RED_INTEGER=8D94 RGB_INTEGER=8D98 RGBA_INTEGER=8D99 SAMPLER_2D_ARRAY=8DC1 SAMPLER_2D_ARRAY_SHADOW=8DC4 SAMPLER_CUBE_SHADOW=8DC5 UNSIGNED_INT_VEC2=8DC6 UNSIGNED_INT_VEC3=8DC7 UNSIGNED_INT_VEC4=8DC8 INT_SAMPLER_2D=8DCA INT_SAMPLER_3D=8DCB INT_SAMPLER_CUBE=8DCC INT_SAMPLER_2D_ARRAY=8DCF UNSIGNED_INT_SAMPLER_2D=8DD2 UNSIGNED_INT_SAMPLER_3D=8DD3 UNSIGNED_INT_SAMPLER_CUBE=8DD4 UNSIGNED_INT_SAMPLER_2D_ARRAY=8DD7 DEPTH_COMPONENT32F=8CAC DEPTH32F_STENCIL8=8CAD FLOAT_32_UNSIGNED_INT_24_8_REV=8DAD FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING=8210 FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE=8211 FRAMEBUFFER_DEFAULT=8218 UNSIGNED_INT_24_8=84FA DEPTH24_STENCIL8=88F0 UNSIGNED_NORMALIZED=8C17 DRAW_FRAMEBUFFER_BINDING=8CA6 READ_FRAMEBUFFER=8CA8 DRAW_FRAMEBUFFER=8CA9 READ_FRAMEBUFFER_BINDING=8CAA RENDERBUFFER_SAMPLES=8CAB FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER=8CD4 MAX_COLOR_ATTACHMENTS=8CDF FRAMEBUFFER_INCOMPLETE_MULTISAMPLE=8D56 MAX_SAMPLES=8D57 HALF_FLOAT=140B RG=8227 RG_INTEGER=8228 R8=8229 RG8=822B R16F=822D R32F=822E RG16F=822F RG32F=8230 R8I=8231 R8UI=8232 R16I=8233 R16UI=8234 R32I=8235 R32UI=8236 RG8I=8237 RG8UI=8238 RG16I=8239 RG16UI=823A RG32I=823B RG32UI=823C VERTEX_ARRAY_BINDING=85B5 R8_SNORM=8F94 RG8_SNORM=8F95 RGB8_SNORM=8F96 RGBA8_SNORM=8F97 SIGNED_NORMALIZED=8F9C COPY_READ_BUFFER=8F36 COPY_WRITE_BUFFER=8F37 COPY_READ_BUFFER_BINDING=8F36 COPY_WRITE_BUFFER_BINDING=8F37 UNIFORM_BUFFER=8A11 UNIFORM_BUFFER_BINDING=8A28 UNIFORM_BUFFER_START=8A29 UNIFORM_BUFFER_SIZE=8A2A MAX_VERTEX_UNIFORM_BLOCKS=8A2B MAX_FRAGMENT_UNIFORM_BLOCKS=8A2D MAX_COMBINED_UNIFORM_BLOCKS=8A2E MAX_UNIFORM_BUFFER_BINDINGS=8A2F MAX_UNIFORM_BLOCK_SIZE=8A30 MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS=8A31 MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS=8A33 UNIFORM_BUFFER_OFFSET_ALIGNMENT=8A34 ACTIVE_UNIFORM_BLOCKS=8A36 UNIFORM_TYPE=8A37 UNIFORM_SIZE=8A38 UNIFORM_BLOCK_INDEX=8A3A UNIFORM_OFFSET=8A3B UNIFORM_ARRAY_STRIDE=8A3C UNIFORM_MATRIX_STRIDE=8A3D UNIFORM_IS_ROW_MAJOR=8A3E UNIFORM_BLOCK_BINDING=8A3F UNIFORM_BLOCK_DATA_SIZE=8A40 UNIFORM_BLOCK_ACTIVE_UNIFORMS=8A42 UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES=8A43 UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER=8A44 UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER=8A46 INVALID_INDEX=FFFFFFFF MAX_VERTEX_OUTPUT_COMPONENTS=9122 MAX_FRAGMENT_INPUT_COMPONENTS=9125 MAX_SERVER_WAIT_TIMEOUT=9111 OBJECT_TYPE=9112 SYNC_CONDITION=9113 SYNC_STATUS=9114 SYNC_FLAGS=9115 SYNC_FENCE=9116 SYNC_GPU_COMMANDS_COMPLETE=9117 UNSIGNALED=9118 SIGNALED=9119 ALREADY_SIGNALED=911A TIMEOUT_EXPIRED=911B CONDITION_SATISFIED=911C WAIT_FAILED=911D SYNC_FLUSH_COMMANDS_BIT=1 VERTEX_ATTRIB_ARRAY_DIVISOR=88FE ANY_SAMPLES_PASSED=8C2F ANY_SAMPLES_PASSED_CONSERVATIVE=8D6A SAMPLER_BINDING=8919 RGB10_A2UI=906F INT_2_10_10_10_REV=8D9F TRANSFORM_FEEDBACK=8E22 TRANSFORM_FEEDBACK_PAUSED=8E23 TRANSFORM_FEEDBACK_ACTIVE=8E24 TRANSFORM_FEEDBACK_BINDING=8E25 TEXTURE_IMMUTABLE_FORMAT=912F MAX_ELEMENT_INDEX=8D6B TEXTURE_IMMUTABLE_LEVELS=82DF MAX_CLIENT_WAIT_TIMEOUT_WEBGL=9247';
    const K1 = {}, K2 = {};
    for (const [d, o] of [[defs, K1], [defs2, K2]]) for (const kv of d.split(' ')) { const [k, v] = kv.split('='); o[k] = parseInt(v, 16); }
    for (let i = 0; i < 32; i++) K1['TEXTURE' + i] = 0x84C0 + i;
    for (let i = 1; i < 16; i++) { K2['COLOR_ATTACHMENT' + i] = 0x8CE0 + i; K2['DRAW_BUFFER' + i] = 0x8825 + i; }
    K2.TIMEOUT_IGNORED = -1; Object.assign(K, K1, K2);
    const mkc = n => { const c = globalThis[n] || ({ [n]: class { } })[n]; globalThis[n] = c; return c; };
    const GL1 = mkc('WebGLRenderingContext'), GL2 = mkc('WebGL2RenderingContext');
    for (const [k, v] of Object.entries(K1)) { GL1[k] = GL1.prototype[k] = GL2[k] = GL2.prototype[k] = v; }
    for (const [k, v] of Object.entries(K2)) GL2[k] = GL2.prototype[k] = v;
    const O = {}; for (const n of ['Buffer', 'Texture', 'Program', 'Shader', 'Framebuffer', 'Renderbuffer', 'UniformLocation', 'ActiveInfo', 'ShaderPrecisionFormat', 'VertexArrayObject', 'Query', 'Sampler', 'Sync', 'TransformFeedback']) O[n] = mkc('WebGL' + n);
    mkc('WebGLContextEvent');
    const TYPE = { float: 0x1406, vec2: 0x8B50, vec3: 0x8B51, vec4: 0x8B52, int: 0x1404, ivec2: 0x8B53, ivec3: 0x8B54, ivec4: 0x8B55, bool: 0x8B56, bvec2: 0x8B57, bvec3: 0x8B58, bvec4: 0x8B59, mat2: 0x8B5A, mat3: 0x8B5B, mat4: 0x8B5C, uint: 0x1405, uvec2: 0x8DC6, uvec3: 0x8DC7, uvec4: 0x8DC8,
        sampler2D: 0x8B5E, samplerCube: 0x8B60, sampler3D: 0x8B5F, sampler2DShadow: 0x8B62, sampler2DArray: 0x8DC1, sampler2DArrayShadow: 0x8DC4, samplerCubeShadow: 0x8DC5, isampler2D: 0x8DCA, isampler3D: 0x8DCB, isamplerCube: 0x8DCC, isampler2DArray: 0x8DCF, usampler2D: 0x8DD2, usampler3D: 0x8DD3, usamplerCube: 0x8DD4, usampler2DArray: 0x8DD7,
        mat2x3: 0x8B65, mat2x4: 0x8B66, mat3x2: 0x8B67, mat3x4: 0x8B68, mat4x2: 0x8B69, mat4x3: 0x8B6A, mat2x2: 0x8B5A, mat3x3: 0x8B5B, mat4x4: 0x8B5C };
    const BF = { 0: 'zero', 1: 'one', 0x300: 'src', 0x301: 'one-minus-src', 0x302: 'src-alpha', 0x303: 'one-minus-src-alpha', 0x304: 'dst-alpha', 0x305: 'one-minus-dst-alpha', 0x306: 'dst', 0x307: 'one-minus-dst', 0x308: 'src-alpha-saturated', 0x8001: 'constant', 0x8002: 'one-minus-constant', 0x8003: 'constant', 0x8004: 'one-minus-constant' };
    const BE = { 0x8006: 'add', 0x800A: 'subtract', 0x800B: 'reverse-subtract', 0x8007: 'min', 0x8008: 'max' };
    const CMPF = { 0x200: 'never', 0x201: 'less', 0x202: 'equal', 0x203: 'less-equal', 0x204: 'greater', 0x205: 'not-equal', 0x206: 'greater-equal', 0x207: 'always' };
    const SOP = { 0: 'zero', 0x1E00: 'keep', 0x1E01: 'replace', 0x1E02: 'increment-clamp', 0x1E03: 'decrement-clamp', 0x150A: 'invert', 0x8507: 'increment-wrap', 0x8508: 'decrement-wrap' };
    const WRAP = { 0x2901: 'repeat', 0x812F: 'clamp-to-edge', 0x8370: 'mirror-repeat' };
    const TU = { SRC: 1, DST: 2, BIND: 4, RT: 16 }, BU = { MAPR: 1, SRC: 4, DST: 8, INDEX: 16, VERTEX: 32, UNIFORM: 64 };
    const isDepth = f => /^depth|^stencil/.test(f), hasStencil = f => /stencil/.test(f);
    const RBFMT = { 0x8056: 'rgba8unorm', 0x8057: 'rgba8unorm', 0x8D62: 'rgba8unorm', 0x8058: 'rgba8unorm', 0x8051: 'rgba8unorm', 0x1908: 'rgba8unorm', 0x8C43: 'rgba8unorm-srgb', 0x81A5: 'depth16unorm', 0x81A6: 'depth24plus', 0x8CAC: 'depth32float', 0x8D48: 'stencil8', 0x84F9: 'depth24plus-stencil8', 0x88F0: 'depth24plus-stencil8', 0x8CAD: 'depth24plus-stencil8',
        0x881A: 'rgba16float', 0x8814: 'rgba32float', 0x8229: 'r8unorm', 0x822B: 'rg8unorm', 0x822D: 'r16float', 0x822E: 'r32float', 0x822F: 'rg16float', 0x8230: 'rg32float', 0x8C3A: 'rgba16float', 0x8059: 'rgb10a2unorm', 0x8D7C: 'rgba8uint', 0x8236: 'r32uint', 0x8235: 'r32sint', 0x8D70: 'rgba32uint' };
    const kindOf = f => /uint$/.test(f) ? 'u' : /sint$/.test(f) ? 'i' : isDepth(f) ? 'd' : 'f';
    /* returns {fmt, ch (source channels), src: 'u8'|'f32'|'f16'|'packed', out components} */
    function texFmt(ifmt, format, type) {
        const F = 0x1406, HF = type === 0x140B || type === 0x8D61, U8 = type === 0x1401;
        const chans = { 0x1908: 4, 0x1907: 3, 0x1909: 1, 0x1906: 1, 0x190A: 2, 0x1903: 1, 0x8227: 2, 0x8D99: 4, 0x8D94: 1, 0x8228: 2, 0x8D98: 3, 0x8C40: 3, 0x8C42: 4 }[format] || 4;
        if (format === 0x1902 || format === 0x84F9 || isDepth(RBFMT[ifmt] || '')) return { fmt: RBFMT[ifmt] && isDepth(RBFMT[ifmt]) ? RBFMT[ifmt] : format === 0x84F9 ? 'depth24plus-stencil8' : type === F ? 'depth32float' : type === 0x1403 ? 'depth16unorm' : 'depth24plus', depth: true };
        const lum = format === 0x1909 || format === 0x1906 || format === 0x190A;
        if (type === F) { const f = ({ 1: 'r32float', 2: 'rg32float' })[lum ? 0 : chans] || 'rgba32float'; return { fmt: f, ch: chans, fc: f === 'rgba32float' ? 4 : chans, lum: lum ? format : 0, bpc: 4 }; }
        if (HF) { const f = ({ 1: 'r16float', 2: 'rg16float' })[lum ? 0 : chans] || 'rgba16float'; return { fmt: f, ch: chans, fc: f === 'rgba16float' ? 4 : chans, lum: lum ? format : 0, bpc: 2 }; }
        if (type === 0x8363 || type === 0x8033 || type === 0x8034) return { fmt: 'rgba8unorm', packed: type, ch: 1, fc: 4, bpc: 2 };
        if (format === 0x8D94 || format === 0x8228 || format === 0x8D99) { const s = /I$/.test(Object.keys(K).find(k => K[k] === ifmt) || ''); const f = type === 0x1405 ? (chans === 1 ? 'r32uint' : 'rgba32uint') : type === 0x1404 ? 'r32sint' : 'rgba8uint'; return { fmt: f, ch: chans, fc: chans === 1 ? 1 : 4, bpc: type === 0x1401 ? 1 : 4, int: true, s }; }
        const srgb = ifmt === 0x8C43 || ifmt === 0x8C41 || format === 0x8C40 || format === 0x8C42;
        const f = srgb ? 'rgba8unorm-srgb' : !lum && chans === 1 && (ifmt === 0x8229 || format === 0x1903) ? 'r8unorm' : !lum && chans === 2 ? 'rg8unorm' : 'rgba8unorm';
        return { fmt: f, ch: chans, fc: f === 'r8unorm' ? 1 : f === 'rg8unorm' ? 2 : 4, lum: lum ? format : 0, bpc: 1 };
    }
    let dummyCanvas;
    function imagePixels(src) {
        if (src && src.data && src.width != null && !(src instanceof Element)) return { w: src.width, h: src.height, px: new Uint8Array(src.data.buffer, src.data.byteOffset, src.data.byteLength) };
        const w = src.naturalWidth || src.videoWidth || src.width || 0, h = src.naturalHeight || src.videoHeight || src.height || 0;
        if (!w || !h) return { w: 0, h: 0, px: new Uint8Array(0) };
        dummyCanvas = dummyCanvas || document.createElement('canvas'); dummyCanvas.width = w; dummyCanvas.height = h;
        const x = dummyCanvas.getContext('2d'); x.clearRect(0, 0, w, h); x.drawImage(src, 0, 0, w, h);
        const d = x.getImageData(0, 0, w, h).data; return { w, h, px: new Uint8Array(d.buffer, d.byteOffset, d.byteLength) };
    }
    const VSTEP = { 0x1406: [4, 'float32'], 0x1401: [1, 'u8'], 0x1400: [1, 's8'], 0x1403: [2, 'u16'], 0x1402: [2, 's16'], 0x1405: [4, 'uint32'], 0x1404: [4, 'sint32'], 0x140B: [2, 'float16'], 0x8D61: [2, 'float16'] };
    function vfmt(type, size, norm, integer) {
        const [b, base] = VSTEP[type] || [4, 'float32']; const n = size === 3 && b < 4 ? 4 : size;
        const sfx = n === 1 ? '' : 'x' + n;
        if (base === 'float32' || base === 'float16') return [base + sfx, 'f', b * size];
        if (base === 'uint32' || base === 'sint32') return [(norm ? 'float32' : base) + sfx, integer || !norm ? (base[0] === 'u' ? 'u' : 'i') : 'f', b * size];
        const bits = base.slice(1), sg = base[0] === 's';
        return [(norm ? (sg ? 'snorm' : 'unorm') : (sg ? 'sint' : 'uint')) + bits + sfx, norm ? 'f' : sg ? 'i' : 'u', b * size];
    }
    return function create(canvas, isGL2, attrs) {
        const devName = N.gpuInit(); if (devName == null) return null;
        attrs = Object.assign({ alpha: true, depth: true, stencil: false, antialias: true, premultipliedAlpha: true, preserveDrawingBuffer: false, powerPreference: 'default', failIfMajorPerformanceCaveat: false, desynchronized: false }, attrs);
        const gl = Object.create((isGL2 ? GL2 : GL1).prototype);
        let err = 0; const E = e => { if (!err) err = e; };
        let cmds = [], pass = null, trash = [], presentQ = false, dynId = 0, dynCap = 0, dyn = new Uint8Array(1 << 16), cur = 0, ids = 1, stateVer = 1, stateKey = '';
        const st = { clearColor: [0, 0, 0, 0], clearDepth: 1, clearStencil: 0, viewport: [0, 0, canvas.width, canvas.height], scissor: [0, 0, canvas.width, canvas.height], en: new Set([0x0BD0]),
            blendSrc: [1, 1], blendDst: [0, 0], blendEq: [0x8006, 0x8006], blendColor: [0, 0, 0, 0], colorMask: [true, true, true, true], depthMask: true, depthFunc: 0x201, depthRange: [0, 1], cullFace: 0x405, frontFace: 0x901,
            stencilFunc: [[0x207, 0, 0xff], [0x207, 0, 0xff]], stencilOp: [[0x1E00, 0x1E00, 0x1E00], [0x1E00, 0x1E00, 0x1E00]], stencilMask: [0xff, 0xff], polyOff: [0, 0], lineWidth: 1,
            unpackAlign: 4, packAlign: 4, flipY: false, premul: false, rowLen: 0, active: 0, program: null, arrayBuf: null, fb: null, readFb: null, rb: null, ubo: [], copyR: null, copyW: null };
        const units = Array.from({ length: 32 }, () => ({})), samplerObjs = new Array(32).fill(null);
        const curAttr = new Float32Array(64); for (let i = 0; i < 16; i++) curAttr[i * 4 + 3] = 1; const curKind = new Array(16).fill('f');
        const mkVao = () => { const v = Object.assign(new O.VertexArrayObject(), { attribs: Array.from({ length: 16 }, () => ({ en: false, buf: null, size: 4, type: 0x1406, norm: false, stride: 0, off: 0, div: 0, int: false })), elem: null, ver: 1, id: ids++ }); return v; };
        const defVao = mkVao(); let vao = defVao;
        const bump = () => { stateVer++; };
        /* ---- default framebuffer ---- */
        const def = { w: 0, h: 0, color: null, depth: null, id: 0 };
        function ensureDefault() {
            const w = Math.max(1, canvas.width | 0), h = Math.max(1, canvas.height | 0);
            if (def.w === w && def.h === h && def.color) return;
            for (const t of [def.color, def.depth]) if (t) trash.push(t.t, t.v);
            def.w = w; def.h = h; def.ver = (def.ver || 0) + 1;
            const t = N.gpuTexture(w, h, 1, 'rgba8unorm', TU.RT | TU.SRC | TU.DST | TU.BIND, 1, 1, '2d'); def.color = { t, v: N.gpuView(t, {}), fmt: 'rgba8unorm', w, h };
            if (attrs.depth || attrs.stencil) { const d = N.gpuTexture(w, h, 1, 'depth24plus-stencil8', TU.RT, 1, 1, '2d'); def.depth = { t: d, v: N.gpuView(d, {}), fmt: 'depth24plus-stencil8' }; } else def.depth = null;
            def.clearPending = { color: [0, 0, 0, 0], depth: 1, stencil: 0 };
        }
        ensureDefault();
        Object.defineProperties(gl, { canvas: { value: canvas }, drawingBufferWidth: { get: () => def.w }, drawingBufferHeight: { get: () => def.h }, drawingBufferColorSpace: { value: 'srgb', writable: true }, unpackColorSpace: { value: 'srgb', writable: true } });
        /* ---- textures ---- */
        function texView(tx, level, layer) { const k = level + ':' + layer; let v = tx.att.get(k); if (!v) { v = N.gpuView(tx.t, { dimension: '2d', baseMipLevel: level, mipLevelCount: 1, baseArrayLayer: layer, arrayLayerCount: 1 }); tx.att.set(k, v); } return v; }
        function allocTex(tx, w, h, d, fmt, levels) {
            if (tx.t && tx.w === w && tx.h === h && tx.d === d && tx.fmt === fmt && (levels == null || tx.levels === levels)) return;
            if (tx.t) { trash.push(tx.t, tx.view, ...tx.att.values()); }
            const dim = tx.target === 0x806F ? '3d' : '2d', layers = tx.target === 0x8513 ? 6 : d;
            const mips = levels || (isDepth(fmt) || kindOf(fmt) !== 'f' ? 1 : Math.floor(Math.log2(Math.max(w, h, dim === '3d' ? d : 1))) + 1);
            Object.assign(tx, { w, h, d, fmt, levels: mips, kind: kindOf(fmt), att: new Map(), mipsOK: false, ver: (tx.ver || 0) + 1 });
            tx.t = N.gpuTexture(w, h, layers, fmt, TU.SRC | TU.DST | TU.BIND | (fmt.includes('32') && !fmt.endsWith('float') ? 0 : TU.RT), mips, 1, dim);
            if (!tx.t && (tx.t = N.gpuTexture(w, h, layers, fmt, TU.SRC | TU.DST | TU.BIND, mips, 1, dim))) {}
            tx.view = N.gpuView(tx.t, { dimension: tx.target === 0x8513 ? 'cube' : tx.target === 0x8C1A ? '2d-array' : dim, aspect: isDepth(fmt) ? 'depth-only' : 'all' });
        }
        const texTarget = t => t >= 0x8515 && t <= 0x851A ? 0x8513 : t;
        function boundTex(target) { return units[st.active][texTarget(target)] || null; }
        function needFlushForWrite() { if (cmds.length) flush(); }
        function convertPixels(f, data, w, h, d) {
            const bytesIn = f.packed ? 2 : f.ch * f.bpc, rowIn = st.rowLen ? st.rowLen * bytesIn : w * bytesIn, stride = Math.ceil(rowIn / st.unpackAlign) * st.unpackAlign;
            const src = new Uint8Array(data.buffer, data.byteOffset, data.byteLength), outBpp = f.fc * f.bpc, out = new Uint8Array(w * h * d * outBpp);
            const rows = h * d;
            for (let r = 0; r < rows; r++) {
                const sy = st.flipY ? (Math.floor(r / h) * h + (h - 1 - r % h)) : r, so = sy * stride, oo = r * w * outBpp;
                if (so >= src.length) break;
                if (!f.lum && !f.packed && f.ch === f.fc) { out.set(src.subarray(so, so + w * outBpp), oo); continue; }
                if (f.packed) { const v16 = new Uint16Array(src.buffer.slice(src.byteOffset + so, src.byteOffset + so + w * 2)); for (let x = 0; x < w; x++) { const v = v16[x], o = oo + x * 4;
                    if (f.packed === 0x8363) { out[o] = (v >> 11) * 255 / 31; out[o + 1] = ((v >> 5) & 63) * 255 / 63; out[o + 2] = (v & 31) * 255 / 31; out[o + 3] = 255; }
                    else if (f.packed === 0x8033) { out[o] = (v >> 12) * 17; out[o + 1] = ((v >> 8) & 15) * 17; out[o + 2] = ((v >> 4) & 15) * 17; out[o + 3] = (v & 15) * 17; }
                    else { out[o] = (v >> 11) * 255 / 31; out[o + 1] = ((v >> 6) & 31) * 255 / 31; out[o + 2] = ((v >> 1) & 31) * 255 / 31; out[o + 3] = v & 1 ? 255 : 0; } } continue; }
                const B = f.bpc, one = B === 1 ? [255] : B === 2 ? [0, 0x3C] : [0, 0, 0x80, 0x3F];
                for (let x = 0; x < w; x++) { const si = so + x * f.ch * B, o = oo + x * outBpp;
                    const ch = c => src.subarray(si + c * B, si + c * B + B);
                    let comps;
                    if (f.lum === 0x1909) comps = [ch(0), ch(0), ch(0), one]; else if (f.lum === 0x1906) comps = [new Uint8Array(B), new Uint8Array(B), new Uint8Array(B), ch(0)]; else if (f.lum === 0x190A) comps = [ch(0), ch(0), ch(0), ch(1)];
                    else { comps = []; for (let c = 0; c < f.fc; c++) comps.push(c < f.ch ? ch(c) : c === 3 ? one : new Uint8Array(B)); }
                    for (let c = 0; c < f.fc; c++) out.set(comps[c], o + c * B); }
            }
            if (st.premul && f.fmt.startsWith('rgba8')) for (let i = 0; i < out.length; i += 4) { const a = out[i + 3]; if (a !== 255) { out[i] = out[i] * a / 255; out[i + 1] = out[i + 1] * a / 255; out[i + 2] = out[i + 2] * a / 255; } }
            return out;
        }
        function upload(target, level, ifmt, w, h, d, format, type, src, xo, yo, zo, sub) {
            const tx = boundTex(target); if (!tx) return E(0x502);
            let px = null, f = texFmt(ifmt, format, type);
            if (src != null && !ArrayBuffer.isView(src) && typeof src === 'object') { const im = imagePixels(src); w = im.w; h = im.h; d = 1; px = im.px;
                if (st.flipY) { const r = w * 4, t = new Uint8Array(px.length); for (let y = 0; y < h; y++) t.set(px.subarray(y * r, y * r + r), (h - 1 - y) * r); px = t; }
                if (st.premul) { px = new Uint8Array(px); for (let i = 0; i < px.length; i += 4) { const a = px[i + 3]; if (a !== 255) { px[i] = px[i] * a / 255; px[i + 1] = px[i + 1] * a / 255; px[i + 2] = px[i + 2] * a / 255; } } }
                if (f.bpc !== 1 || f.fc !== 4 || f.lum) { const fl = f.bpc === 4 ? new Float32Array(px.length) : null; if (fl) { for (let i = 0; i < px.length; i++) fl[i] = px[i] / 255; px = new Uint8Array(fl.buffer); f = { fmt: 'rgba32float', fc: 4, bpc: 4 }; } else f = { fmt: f.fmt.includes('srgb') ? 'rgba8unorm-srgb' : 'rgba8unorm', fc: 4, bpc: 1 }; } }
            else if (src) px = convertPixels(f, src, w, h, d);
            const layer = target >= 0x8515 && target <= 0x851A ? target - 0x8515 : 0;
            if (!sub) { if (level === 0) allocTex(tx, w, h, tx.target === 0x8513 ? 1 : d, f.fmt); else if (!tx.t) return; }
            if (!tx.t) return;
            if (level >= tx.levels || f.depth) return;
            if (px && px.length) { needFlushForWrite(); N.gpuWriteTexture(tx.t, level, xo || 0, yo || 0, (zo || 0) + layer, px, 0, w * f.fc * f.bpc, h, w, h, d || 1); }
            if (level > 0) tx.defLevels = (tx.defLevels || 1) + 1; if (tx.defLevels >= tx.levels) tx.mipsOK = true;
            tx.ver++;
        }
        /* ---- mipmap generation via blit ---- */
        const blit = { pipes: new Map(), samp: 0 };
        function blitPipe(fmt) {
            let p = blit.pipes.get(fmt); if (p) return p;
            const m = N.gpuShader(`struct O { @builtin(position) p: vec4f, @location(0) uv: vec2f };
@group(0) @binding(0) var t: texture_2d<f32>; @group(0) @binding(1) var s: sampler;
@vertex fn vs(@builtin(vertex_index) i: u32) -> O { var q = array(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3)); var o: O; o.p = vec4f(q[i], 0, 1); o.uv = q[i] * vec2f(.5, -.5) + .5; return o; }
@fragment fn fs(o: O) -> @location(0) vec4f { return textureSample(t, s, o.uv); }`, 0);
            const pipe = N.gpuRP({ layout: 0, vertex: { module: m, entryPoint: 'vs', buffers: [] }, fragment: { module: m, entryPoint: 'fs', targets: [{ format: fmt }] }, primitive: { topology: 'triangle-list' } });
            p = { pipe, bgl: N.gpuPipeBGL(pipe, 0) }; blit.pipes.set(fmt, p);
            if (!blit.samp) blit.samp = N.gpuSampler({ magFilter: 'linear', minFilter: 'linear' });
            return p;
        }
        function genMips(tx) {
            if (!tx || !tx.t || tx.levels < 2 || tx.kind !== 'f' || /32float/.test(tx.fmt) && !N.gpuFeature(14)) return;
            endPass(); const p = blitPipe(tx.fmt), layers = tx.target === 0x8513 ? 6 : 1;
            for (let l = 0; l < layers; l++) for (let i = 1; i < tx.levels; i++) {
                const src = N.gpuView(tx.t, { dimension: '2d', baseMipLevel: i - 1, mipLevelCount: 1, baseArrayLayer: l, arrayLayerCount: 1 }), dst = texView(tx, i, l);
                const bg = N.gpuBG(p.bgl, [{ binding: 0, view: src }, { binding: 1, sampler: blit.samp }]);
                cmds.push(['rp', [{ view: dst, loadOp: 'clear', storeOp: 'store', clearValue: [0, 0, 0, 0] }], null], ['pipe', p.pipe], ['bg', 0, bg, []], ['draw', 3, 1, 0, 0], ['end']);
                trash.push(src, bg);
            }
            tx.mipsOK = true;
        }
        /* ---- samplers ---- */
        const sampCache = new Map();
        function samplerFor(tx, so, filterable, cmp) {
            const P = so ? so.p : tx.p, mipF = P.min >= 0x2700, ok = mipF && tx.mipsOK;
            let mag = P.mag === 0x2600 ? 'nearest' : 'linear', min = P.min === 0x2600 || P.min === 0x2700 || P.min === 0x2702 ? 'nearest' : 'linear', mm = P.min === 0x2702 || P.min === 0x2703 ? 'linear' : 'nearest';
            if (!filterable) mag = min = mm = 'nearest';
            const an = filterable && mag === 'linear' && min === 'linear' && mm === 'linear' ? Math.max(1, Math.min(16, P.aniso | 0)) : 1;
            const d = { addressModeU: WRAP[P.ws], addressModeV: WRAP[P.wt], addressModeW: WRAP[P.wr], magFilter: mag, minFilter: min, mipmapFilter: mm, lodMinClamp: ok ? Math.max(0, P.minLod) : 0, lodMaxClamp: ok ? Math.min(32, P.maxLod) : 0, maxAnisotropy: an };
            if (cmp) d.compare = CMPF[P.cmpFunc] || 'less-equal';
            const k = JSON.stringify(d); let s = sampCache.get(k); if (!s) { s = N.gpuSampler(d); sampCache.set(k, s); } return s;
        }
        const defParams = () => ({ min: 0x2702, mag: 0x2601, ws: 0x2901, wt: 0x2901, wr: 0x2901, cmpMode: 0, cmpFunc: 0x203, minLod: -1000, maxLod: 1000, aniso: 1, base: 0, max: 1000 });
        const dummies = {};
        function dummy(dim, kind) {
            const k = dim + kind; if (dummies[k]) return dummies[k];
            const fmt = kind === 'd' ? 'depth32float' : kind === 'u' ? 'rgba8uint' : kind === 'i' ? 'r32sint' : 'rgba8unorm', layers = dim === 'cube' ? 6 : 1;
            const t = N.gpuTexture(1, 1, layers, fmt, TU.BIND | TU.DST | (kind === 'd' ? TU.RT : 0), 1, 1, dim === '3d' ? '3d' : '2d');
            if (kind === 'f') for (let l = 0; l < layers; l++) N.gpuWriteTexture(t, 0, 0, 0, l, new Uint8Array([0, 0, 0, 255]), 0, 4, 1, 1, 1, 1);
            return dummies[k] = { t, view: N.gpuView(t, { dimension: dim }), p: defParams(), kind, fmt, mipsOK: false, id: -1, ver: 0 };
        }
        /* ---- render pass management ---- */
        function target(fb) {
            if (!fb) { ensureDefault(); return { key: 'd' + def.ver, w: def.w, h: def.h, colors: [{ view: def.color.v, fmt: 'rgba8unorm', on: true }], ds: def.depth ? { view: def.depth.v, fmt: def.depth.fmt } : null, clr: def }; }
            const cs = [], bufs = fb.drawBufs || [0x8CE0]; let w = 0, h = 0, key = 'f' + fb.id;
            for (let i = 0; i < 8; i++) { const a = fb.att[0x8CE0 + i]; if (!a) continue; const o = a.obj, on = bufs[i] === 0x8CE0 + i;
                if (!o.t) continue; const view = a.rb ? o.v : texView(o, a.level, a.layer); w = w || (a.rb ? o.w : Math.max(1, o.w >> a.level)); h = h || (a.rb ? o.h : Math.max(1, o.h >> a.level));
                cs[i] = { view, fmt: o.fmt, on }; key += ':' + i + view + (on ? 1 : 0); }
            for (let i = 0; i < cs.length; i++) if (!cs[i]) cs[i] = null;
            let ds = null; const da = fb.att[0x821A] || fb.att[0x8D00] || fb.att[0x8D20];
            if (da && da.obj.t) { const o = da.obj; ds = { view: da.rb ? o.v : texView(o, da.level, da.layer), fmt: o.fmt }; key += 'z' + ds.view; w = w || o.w; h = h || o.h; }
            return { key, w, h, colors: cs, ds, clr: fb };
        }
        let passTarget = null, ps = {};
        function endPass() { if (pass) { cmds.push(['end']); pass = null; } }
        function beginPass(T) {
            endPass(); const c = T.clr.clearPending; T.clr.clearPending = null;
            const ca = T.colors.map(x => x && { view: x.view, loadOp: c && c.color ? 'clear' : 'load', storeOp: 'store', clearValue: c && c.color || [0, 0, 0, 0] });
            const ds = T.ds ? { view: T.ds.view, depthLoadOp: hasStencil(T.ds.fmt) && T.ds.fmt === 'stencil8' ? undefined : c && c.depth != null ? 'clear' : 'load', depthStoreOp: 'store', depthClearValue: c && c.depth != null ? c.depth : 1 } : null;
            if (ds && T.ds.fmt === 'stencil8') delete ds.depthLoadOp;
            if (ds && hasStencil(T.ds.fmt)) { ds.stencilLoadOp = c && c.stencil != null ? 'clear' : 'load'; ds.stencilStoreOp = 'store'; ds.stencilClearValue = c && c.stencil || 0; }
            cmds.push(['rp', ca, ds]); pass = T; passTarget = T.key; ps = {};
        }
        function schedulePresent() { if (!presentQ) { presentQ = true; queueMicrotask(present); } }
        function flush() {
            endPass();
            if (cur && dynId) N.gpuWriteBuffer(dynId, 0, dyn.subarray(0, (cur + 3) & ~3));
            if (cmds.length) N.gpuExec(cmds);
            cmds = []; cur = 0; for (const p of progs) p.lastOff = -1; constOff = -1;
            if (trash.length) { for (const id of trash) if (id) N.gpuDrop(id); trash = []; }
        }
        function present() { presentQ = false; if (lost) return; const T = def.clearPending; flush(); if (T && T.color) { beginPass(target(null)); flush(); } N.gpuPresent(def.color.t, canvas, !attrs.alpha, true); }
        function dynAlloc(n, align) {
            let off = Math.ceil(cur / align) * align;
            if (off + n > dyn.length) { if (off + n <= 1 << 22) { const nd = new Uint8Array(Math.max(dyn.length * 2, off + n)); nd.set(dyn.subarray(0, cur)); dyn = nd; } else { flush(); off = 0; } }
            if (!dynId || dynCap < dyn.length) { if (dynId) { if (cmds.length) { const keep = cur; flush(); off = 0; void keep; } trash.push(dynId); } dynCap = dyn.length; dynId = N.gpuBuffer(dynCap, BU.UNIFORM | BU.VERTEX | BU.INDEX | BU.DST, false); for (const p of progs) p.bgCache.clear(); }
            cur = off + n; return off;
        }
        /* ---- programs ---- */
        const progs = new Set(); let constOff = -1;
        function linkProgram(p) {
            p.linked = false; p.log = '';
            try {
                const vs = p.shaders.find(s => s.type === 0x8B31), fs = p.shaders.find(s => s.type === 0x8B30);
                if (!vs || !fs) throw new Error('missing shader');
                const L = LGLSL.link(vs.src, fs.src);
                const locs = {}, used = new Set(); let next = 0;
                const width = a => { const P = LGLSL.PRIM[a.type]; return P && P[1] > 1 ? P[1] : 1; };
                for (const a of L.attrs) { const b = p.bound.has(a.name) ? p.bound.get(a.name) : a.loc >= 0 ? a.loc : -1; if (b >= 0) { locs[a.name] = b; for (let k = 0; k < width(a); k++) used.add(b + k); } }
                for (const a of L.attrs) if (!(a.name in locs)) { const w = width(a); for (;;) { let ok = true; for (let k = 0; k < w; k++) if (used.has(next + k)) ok = false; if (ok) break; next++; } locs[a.name] = next; for (let k = 0; k < w; k++) used.add(next + k); next += w; }
                N.gpuErr();
                const fsm = N.gpuShader(L.fs, 2); let e = N.gpuErr(); if (e || !fsm) throw new Error('fragment: ' + (e || 'compile failed') + '\n' + L.fs);
                Object.assign(p, { L, locs, fsm, vsm: new Map(), pipes: new Map(), bgls: new Map(), bgCache: new Map(), lastOff: -1, linked: true, dirty: true });
                const v0 = vertexModule(p, {}); if (!v0) throw new Error(p.log);
                p.ubo = new ArrayBuffer(Math.max(16, L.uboSize)); p.uf = new Float32Array(p.ubo); p.ui = new Int32Array(p.ubo); p.uu = new Uint32Array(p.ubo); p.u8 = new Uint8Array(p.ubo);
                p.units = new Map(); for (const s of L.samplers) p.units.set(s.name, new Int32Array(s.arr || 1));
                p.uniformList = [...L.leafs, ...L.samplers.map(s => ({ name: s.arr ? s.name + '[0]' : s.name, base: s.name, type: s.type, size: s.arr || 1, sampler: s }))];
                p.attrList = L.attrs.map(a => ({ name: a.name, type: a.type, size: 1 }));
                p.blockBind = L.blocks.map(() => 0);
                progs.add(p);
            } catch (ex) { p.linked = false; p.log = String(ex.message || ex); if (globalThis.LUMEN_GL_DEBUG) console.log('webgl link: ' + p.log); }
        }
        function vertexModule(p, kinds) {
            const key = p.L.attrs.map(a => kinds[a.name] || 'f').join('');
            let m = p.vsm.get(key); if (m) return m;
            const code = p.L.vertex(p.locs, kinds); N.gpuErr(); m = N.gpuShader(code, 1); const e = N.gpuErr();
            if (e || !m) { p.log = 'vertex: ' + (e || 'compile failed') + '\n' + code; if (globalThis.LUMEN_GL_DEBUG) console.log('webgl: ' + p.log); return 0; }
            p.vsm.set(key, m); return m;
        }
        function bglFor(p, classes) {
            let b = p.bgls.get(classes); if (b) return b;
            const vis = 3, es = []; if (p.L.uboSize) es.push({ binding: 0, visibility: vis, buffer: { type: 'uniform', hasDynamicOffset: 1 } });
            let i = 0; for (const s of p.L.samplers) { const [dim] = LGLSL.SAMPLERS[s.type]; for (const bnd of s.binds) { const c = classes[i++];
                es.push({ binding: bnd, visibility: vis, texture: { sampleType: c === 'd' ? 'depth' : c === 'u' ? 'uint' : c === 'i' ? 'sint' : c === 'n' ? 'unfilterable-float' : 'float', viewDimension: dim } });
                es.push({ binding: bnd + 1, visibility: vis, sampler: { type: c === 'd' ? 'comparison' : c === 'f' ? 'filtering' : 'non-filtering' } }); } }
            for (const bl of p.L.blocks) es.push({ binding: bl.binding, visibility: vis, buffer: { type: 'uniform' } });
            const bgl = N.gpuBGL(es); b = { bgl, pl: N.gpuPL([bgl]) }; p.bgls.set(classes, b); return b;
        }
        /* ---- draw ---- */
        function stateSig() {
            if (stateKey && stateKey[0] === stateVer) return stateKey[1];
            const en = st.en; const s = [en.has(0x0BE2) ? st.blendSrc.join() + st.blendDst + st.blendEq : '-', st.colorMask.map(Number).join(''), en.has(0x0B71) ? st.depthFunc : 'n', st.depthMask ? 1 : 0, en.has(0x0B44) ? st.cullFace : 'n', st.frontFace,
                en.has(0x0B90) ? JSON.stringify([st.stencilFunc, st.stencilOp, st.stencilMask]) : 'n', en.has(0x8037) ? st.polyOff.join() : 'n'].join('|');
            stateKey = [stateVer, s]; return s;
        }
        const TOPO = { 0: 'point-list', 1: 'line-list', 2: 'line-strip', 3: 'line-strip', 4: 'triangle-list', 5: 'triangle-strip', 6: 'triangle-list' };
        function makePipe(p, vsm, plan, mode, T, classes, stripFmt) {
            const en = st.en, blendOn = en.has(0x0BE2);
            const blend = blendOn ? { color: { srcFactor: BF[st.blendSrc[0]], dstFactor: BF[st.blendDst[0]], operation: BE[st.blendEq[0]] }, alpha: { srcFactor: BF[st.blendSrc[1]], dstFactor: BF[st.blendDst[1]], operation: BE[st.blendEq[1]] } } : null;
            if (blend) for (const c of [blend.color, blend.alpha]) if (c.operation === 'min' || c.operation === 'max') c.srcFactor = c.dstFactor = 'one';
            const wm = (st.colorMask[0] ? 1 : 0) | (st.colorMask[1] ? 2 : 0) | (st.colorMask[2] ? 4 : 0) | (st.colorMask[3] ? 8 : 0);
            const outs = new Set(p.L.fouts.map(o => o.loc));
            const targets = T.colors.map((c, i) => c ? { format: c.fmt, writeMask: c.on && outs.has(i) ? wm : 0, blend: blend && !/int$|32float/.test(c.fmt) ? blend : undefined } : null);
            const desc = { layout: bglFor(p, classes).pl, vertex: { module: vsm, entryPoint: 'main', buffers: plan.layouts }, fragment: { module: p.fsm, entryPoint: 'main', targets }, primitive: { topology: TOPO[mode], stripIndexFormat: stripFmt || undefined, frontFace: st.frontFace === 0x901 ? 'cw' : 'ccw', cullMode: en.has(0x0B44) ? (st.cullFace === 0x404 ? 'front' : st.cullFace === 0x405 ? 'back' : 'none') : 'none' }, multisample: { count: 1 } };
            if (T.ds) { const dt = en.has(0x0B71), stOn = en.has(0x0B90) && hasStencil(T.ds.fmt);
                const face = i => ({ compare: stOn ? CMPF[st.stencilFunc[i][0]] : 'always', failOp: stOn ? SOP[st.stencilOp[i][0]] : 'keep', depthFailOp: stOn ? SOP[st.stencilOp[i][1]] : 'keep', passOp: stOn ? SOP[st.stencilOp[i][2]] : 'keep' });
                desc.depthStencil = { format: T.ds.fmt, depthWriteEnabled: dt && st.depthMask && T.ds.fmt !== 'stencil8' ? 1 : 0, depthCompare: dt ? CMPF[st.depthFunc] : 'always', stencilFront: face(0), stencilBack: face(1), stencilReadMask: stOn ? st.stencilFunc[0][2] : 0xff, stencilWriteMask: stOn ? st.stencilMask[0] : 0,
                    depthBias: en.has(0x8037) ? Math.round(st.polyOff[1]) : 0, depthBiasSlopeScale: en.has(0x8037) ? st.polyOff[0] : 0 }; }
            N.gpuErr(); const id = N.gpuRP(desc); const e = N.gpuErr(); if (e && globalThis.LUMEN_GL_DEBUG) console.log('webgl pipeline: ' + e);
            return e ? 0 : id;
        }
        function vertexPlan(p) {
            const groups = [], consts = [], kinds = {}; let sig = '';
            for (const a of p.L.attrs) {
                const P = LGLSL.PRIM[a.type] || [4, 1, 'f'], cols = P[1] > 1 ? P[1] : 1;
                for (let c = 0; c < cols; c++) { const loc = p.locs[a.name] + c, A = vao.attribs[loc];
                    if (A && A.en && A.buf) { const [fmt, kind, bytes] = vfmt(A.type, A.size, A.norm, A.int); if (cols === 1) kinds[a.name] = kind;
                        const stride = A.stride || bytes; let g = groups.find(g => g.buf === A.buf && g.stride === stride && g.div === A.div && A.off >= g.base && A.off - g.base + bytes <= stride);
                        if (!g) { g = { buf: A.buf, stride, div: A.div, base: A.off, attrs: [] }; groups.push(g); }
                        g.attrs.push({ format: fmt, offset: A.off - g.base, shaderLocation: loc }); }
                    else { const k = cols === 1 ? curKind[loc] : 'f'; if (cols === 1 && k !== 'f') kinds[a.name] = k; consts.push({ loc, k }); } }
            }
            for (const g of groups) { const mn = Math.min(...g.attrs.map(x => x.offset)); if (mn > 0) { g.base += mn; for (const x of g.attrs) x.offset -= mn; } }
            const layouts = groups.map(g => ({ arrayStride: g.stride, stepMode: g.div ? 'instance' : 'vertex', attributes: g.attrs }));
            if (consts.length) layouts.push({ arrayStride: 0, stepMode: 'instance', attributes: consts.map((c, i) => ({ format: c.k === 'u' ? 'uint32x4' : c.k === 'i' ? 'sint32x4' : 'float32x4', offset: i * 16, shaderLocation: c.loc })) });
            sig = JSON.stringify(layouts);
            return { groups, consts, layouts, sig, kinds };
        }
        let lastPipe = 0;
        function draw(mode, first, count, inst, idx) {
            const p = st.program; if (!p || !p.linked) return E(0x502); if (count <= 0 || inst <= 0) return;
            if (st.en.has(0x0B44) && st.cullFace === 0x408 && mode >= 4) return;
            const T = target(st.fb); if (!T.w || !T.h) return;
            const plan = vertexPlan(p), vsm = vertexModule(p, plan.kinds); if (!vsm) return E(0x502);
            /* textures */
            let classes = '', bgKey = '', texEntries = [];
            for (const s of p.L.samplers) { const [dim, sk] = LGLSL.SAMPLERS[s.type], tgt = dim === 'cube' ? 0x8513 : dim === '3d' ? 0x806F : dim === '2d-array' ? 0x8C1A : 0x0DE1, un = p.units.get(s.name);
                s.binds.forEach((b, k) => { const u = un[k] | 0; let tx = units[u] && units[u][tgt]; if (!tx || !tx.t) tx = dummy(dim, sk === 'd' ? 'd' : sk === 'i' ? 'i' : sk === 'u' ? 'u' : 'f');
                    let c = sk === 'd' ? 'd' : sk === 'i' || sk === 'u' ? sk : tx.kind === 'd' || (/32float/.test(tx.fmt) && !N.gpuFeature(14)) ? 'n' : 'f';
                    if (sk === 'd' && tx.kind !== 'd') { tx = dummy(dim, 'd'); }
                    const smp = samplerFor(tx, samplerObjs[u], c === 'f' || c === 'd', c === 'd'); classes += c; bgKey += tx.view + ',' + smp + ';';
                    texEntries.push({ binding: b, view: tx.view }, { binding: b + 1, sampler: smp }); }); }
            for (let i = 0; i < p.L.blocks.length; i++) { const bl = p.L.blocks[i], ub = st.ubo[p.blockBind[i]]; const id = ub && ub.buf && ub.buf.id || dummyUbo(bl.S.layout().sz); bgKey += 'u' + id + '@' + (ub ? ub.off : 0); texEntries.push({ binding: bl.binding, buffer: id, offset: ub ? ub.off : 0, size: ub && ub.size > 0 ? ub.size : bl.S.layout().sz }); }
            const topoMode = mode;
            let indexInfo = null, fan = mode === 6, loop = mode === 2;
            if (idx) { const eb = vao.elem; if (!eb || !eb.id) return E(0x502); indexInfo = { buf: eb, type: idx.type, off: idx.off }; }
            const stripFmt = idx && (mode === 3 || mode === 5) ? (idx.type === 0x1405 ? 'uint32' : 'uint16') : '';
            const pkey = plan.sig + '|' + mode + stripFmt + '|' + stateSig() + '|' + T.colors.map(c => c ? c.fmt + c.on : '').join() + (T.ds ? T.ds.fmt : '') + '|' + classes + '|' + Object.values(plan.kinds).join('');
            let pipe = p.pipes.get(pkey); if (pipe === undefined) { pipe = makePipe(p, vsm, plan, topoMode, T, classes, stripFmt); p.pipes.set(pkey, pipe); }
            if (!pipe) return;
            /* uniforms + constant attributes into the dynamic buffer */
            let uoff = 0; if (p.L.uboSize) { if (p.dirty || p.lastOff < 0) { uoff = dynAlloc(p.L.uboSize, 256); dyn.set(p.u8.subarray(0, p.L.uboSize), uoff); p.lastOff = uoff; p.dirty = false; } else uoff = p.lastOff; }
            let coff = -1; if (plan.consts.length) { coff = dynAlloc(plan.consts.length * 16, 16); const f = new Float32Array(dyn.buffer, coff, plan.consts.length * 4); plan.consts.forEach((c, i) => f.set(curAttr.subarray(c.loc * 4, c.loc * 4 + 4), i * 4)); }
            let ioff = -1, icount = count, ifmt = 16;
            if (fan || loop || (idx && idx.type === 0x1401)) {
                let src; if (idx) { const eb = vao.elem, bpe = idx.type === 0x1405 ? 4 : idx.type === 0x1403 ? 2 : 1; const A = bpe === 4 ? Uint32Array : bpe === 2 ? Uint16Array : Uint8Array; src = new A(eb.data.buffer, eb.data.byteOffset + idx.off, count); }
                else { src = new Uint32Array(count); for (let i = 0; i < count; i++) src[i] = first + i; }
                let out; if (fan) { const n = Math.max(0, count - 2); out = new Uint32Array(n * 3); for (let i = 0; i < n; i++) { out[i * 3] = src[0]; out[i * 3 + 1] = src[i + 1]; out[i * 3 + 2] = src[i + 2]; } }
                else if (loop) { out = new Uint32Array(count + 1); out.set(src); out[count] = src[0]; } else out = Uint32Array.from(src);
                ioff = dynAlloc(out.byteLength, 4); dyn.set(new Uint8Array(out.buffer), ioff); icount = out.length; ifmt = 32;
            }
            if (pass && passTarget === T.key && !T.clr.clearPending) {} else beginPass(T);
            /* dynamic state */
            const [vx, vy, vw, vh] = st.viewport; const vk = st.viewport.join() + st.depthRange;
            if (ps.vp !== vk) { const x0 = Math.max(0, vx), y0 = Math.max(0, vy), x1 = Math.min(T.w, vx + vw), y1 = Math.min(T.h, vy + vh); if (x1 <= x0 || y1 <= y0) return;
                cmds.push(['vp', vx, vy, vw, vh, st.depthRange[0], st.depthRange[1]]); ps.vp = vk; }
            const sk = st.en.has(0x0C11) ? st.scissor.join() : 'full';
            if (ps.sc !== sk) { let [x, y, w, h] = st.en.has(0x0C11) ? st.scissor : [0, 0, T.w, T.h]; const x0 = Math.min(T.w, Math.max(0, x)), y0 = Math.min(T.h, Math.max(0, y)); w = Math.max(0, Math.min(T.w, x + w) - x0); h = Math.max(0, Math.min(T.h, y + h) - y0); cmds.push(['sc', x0, y0, w, h]); ps.sc = sk; if (!w || !h) ps.empty = true; else ps.empty = false; }
            if (ps.empty) return;
            if (st.en.has(0x0BE2) && ps.bc !== st.blendColor.join()) { cmds.push(['blend', ...st.blendColor]); ps.bc = st.blendColor.join(); }
            if (st.en.has(0x0B90) && ps.sr !== st.stencilFunc[0][1]) { cmds.push(['sref', st.stencilFunc[0][1]]); ps.sr = st.stencilFunc[0][1]; }
            if (ps.pipe !== pipe) { cmds.push(['pipe', pipe]); ps.pipe = pipe; }
            const classesB = bglFor(p, classes);
            const bk = classes + bgKey + dynId; let bg = p.bgCache.get(bk);
            if (!bg) { if (p.bgCache.size > 256) { for (const v of p.bgCache.values()) trash.push(v); p.bgCache.clear(); } const es = p.L.uboSize ? [{ binding: 0, buffer: dynId, offset: 0, size: p.L.uboSize }, ...texEntries] : texEntries; bg = N.gpuBG(classesB.bgl, es); p.bgCache.set(bk, bg); }
            const bgs = bg + ':' + uoff; if (ps.bg !== bgs) { cmds.push(['bg', 0, bg, p.L.uboSize ? [uoff] : []]); ps.bg = bgs; }
            plan.groups.forEach((g, i) => { const k = g.buf.id + '@' + g.base; if (ps['v' + i] !== k) { cmds.push(['vb', i, g.buf.id, g.base, -1]); ps['v' + i] = k; } });
            if (plan.consts.length) { const i = plan.groups.length; cmds.push(['vb', i, dynId, coff, plan.consts.length * 16]); ps['v' + i] = ''; }
            if (ioff >= 0) { cmds.push(['ib', dynId, 32, ioff, icount * 4], ['drawi', icount, inst, 0, 0, 0]); ps.ib = ''; }
            else if (idx) { const eb = vao.elem, bpe = idx.type === 0x1405 ? 4 : 2, k = eb.id + ':' + bpe; if (ps.ib !== k) { cmds.push(['ib', eb.id, bpe * 8, 0, -1]); ps.ib = k; } cmds.push(['drawi', count, inst, idx.off / bpe, 0, 0]); }
            else cmds.push(['draw', count, inst, first, 0]);
            if (!st.fb) schedulePresent();
            if (cmds.length > 20000) flush();
        }
        const ubos = new Map(); function dummyUbo(sz) { let b = ubos.get(sz); if (!b) { b = N.gpuBuffer(Math.max(16, sz), BU.UNIFORM | BU.DST, false); ubos.set(sz, b); } return b; }
        /* ---- API ---- */
        let lost = false;
        const extNames = isGL2 ? ['EXT_color_buffer_float', 'EXT_color_buffer_half_float', 'EXT_float_blend', 'EXT_texture_filter_anisotropic', 'OES_texture_float_linear', 'WEBGL_debug_renderer_info', 'WEBGL_lose_context', 'KHR_parallel_shader_compile', 'WEBGL_multi_draw']
            : ['ANGLE_instanced_arrays', 'EXT_blend_minmax', 'EXT_color_buffer_half_float', 'EXT_frag_depth', 'EXT_shader_texture_lod', 'EXT_sRGB', 'EXT_texture_filter_anisotropic', 'OES_element_index_uint', 'OES_fbo_render_mipmap', 'OES_standard_derivatives', 'OES_texture_float', 'OES_texture_float_linear', 'OES_texture_half_float', 'OES_texture_half_float_linear', 'OES_vertex_array_object', 'WEBGL_color_buffer_float', 'WEBGL_debug_renderer_info', 'WEBGL_depth_texture', 'WEBGL_draw_buffers', 'WEBGL_lose_context', 'KHR_parallel_shader_compile', 'WEBGL_multi_draw'];
        const exts = {};
        const api = {
            getContextAttributes: () => ({ ...attrs, antialias: false }), isContextLost: () => lost, getSupportedExtensions: () => extNames.slice(),
            getExtension(n) { if (!extNames.includes(n)) return null; if (exts[n]) return exts[n]; let e = {};
                if (n === 'ANGLE_instanced_arrays') e = { VERTEX_ATTRIB_ARRAY_DIVISOR_ANGLE: 0x88FE, drawArraysInstancedANGLE: (m, f, c, i) => api.drawArraysInstanced(m, f, c, i), drawElementsInstancedANGLE: (m, c, t, o, i) => api.drawElementsInstanced(m, c, t, o, i), vertexAttribDivisorANGLE: (i, d) => api.vertexAttribDivisor(i, d) };
                else if (n === 'OES_vertex_array_object') e = { VERTEX_ARRAY_BINDING_OES: 0x85B5, createVertexArrayOES: () => api.createVertexArray(), deleteVertexArrayOES: v => api.deleteVertexArray(v), isVertexArrayOES: v => api.isVertexArray(v), bindVertexArrayOES: v => api.bindVertexArray(v) };
                else if (n === 'WEBGL_draw_buffers') { e = { drawBuffersWEBGL: b => api.drawBuffers(b), MAX_DRAW_BUFFERS_WEBGL: 0x8824, MAX_COLOR_ATTACHMENTS_WEBGL: 0x8CDF }; for (let i = 0; i < 16; i++) { e['COLOR_ATTACHMENT' + i + '_WEBGL'] = 0x8CE0 + i; e['DRAW_BUFFER' + i + '_WEBGL'] = 0x8825 + i; } }
                else if (n === 'WEBGL_debug_renderer_info') e = { UNMASKED_VENDOR_WEBGL: 0x9245, UNMASKED_RENDERER_WEBGL: 0x9246 };
                else if (n === 'EXT_texture_filter_anisotropic') e = { TEXTURE_MAX_ANISOTROPY_EXT: 0x84FE, MAX_TEXTURE_MAX_ANISOTROPY_EXT: 0x84FF };
                else if (n === 'WEBGL_lose_context') e = { loseContext() { lost = true; canvas.dispatchEvent(new Event('webglcontextlost')); }, restoreContext() { lost = false; canvas.dispatchEvent(new Event('webglcontextrestored')); } };
                else if (n === 'OES_texture_half_float') e = { HALF_FLOAT_OES: 0x8D61 };
                else if (n === 'WEBGL_depth_texture') e = { UNSIGNED_INT_24_8_WEBGL: 0x84FA };
                else if (n === 'EXT_blend_minmax') e = { MIN_EXT: 0x8007, MAX_EXT: 0x8008 };
                else if (n === 'EXT_sRGB') e = { SRGB_EXT: 0x8C40, SRGB_ALPHA_EXT: 0x8C42, SRGB8_ALPHA8_EXT: 0x8C43, FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING_EXT: 0x8210 };
                else if (n === 'OES_standard_derivatives') e = { FRAGMENT_SHADER_DERIVATIVE_HINT_OES: 0x8B8B };
                else if (n === 'KHR_parallel_shader_compile') e = { COMPLETION_STATUS_KHR: 0x91B1, maxShaderCompilerThreadsKHR() {} };
                else if (n === 'WEBGL_multi_draw') e = { multiDrawArraysWEBGL(m, fl, fo, cl, co, n) { for (let i = 0; i < n; i++) api.drawArrays(m, fl[fo + i], cl[co + i]); }, multiDrawElementsWEBGL(m, cl, co, t, ol, oo, n) { for (let i = 0; i < n; i++) api.drawElements(m, cl[co + i], t, ol[oo + i]); } };
                return exts[n] = e; },
            getError() { const e = err; err = 0; return e; },
            flush() { flush(); }, finish() { flush(); },
            getShaderPrecisionFormat: (s, t) => Object.assign(new O.ShaderPrecisionFormat(), t >= 0x8DF3 ? { rangeMin: 31, rangeMax: 30, precision: 0 } : { rangeMin: 127, rangeMax: 127, precision: 23 }),
            enable(c) { st.en.add(c); bump(); }, disable(c) { st.en.delete(c); bump(); }, isEnabled: c => st.en.has(c),
            viewport(x, y, w, h) { st.viewport = [x | 0, y | 0, w | 0, h | 0]; }, scissor(x, y, w, h) { st.scissor = [x | 0, y | 0, w | 0, h | 0]; },
            clearColor(r, g, b, a) { st.clearColor = [r, g, b, a]; }, clearDepth(d) { st.clearDepth = d; }, clearStencil(s) { st.clearStencil = s; },
            clear(m) { const T = target(st.fb); const fbo = T.clr; const c = fbo.clearPending || {};
                if (m & 0x4000) c.color = st.clearColor.slice(); if (m & 0x100 && T.ds) c.depth = st.clearDepth; if (m & 0x400 && T.ds) c.stencil = st.clearStencil;
                if (pass && passTarget === T.key) endPass(); fbo.clearPending = c; if (!st.fb) schedulePresent(); },
            colorMask(r, g, b, a) { st.colorMask = [!!r, !!g, !!b, !!a]; bump(); }, depthMask(f) { st.depthMask = !!f; bump(); }, depthFunc(f) { st.depthFunc = f; bump(); }, depthRange(n, f) { st.depthRange = [n, f]; },
            blendFunc(s, d) { st.blendSrc = [s, s]; st.blendDst = [d, d]; bump(); }, blendFuncSeparate(sr, dr, sa, da) { st.blendSrc = [sr, sa]; st.blendDst = [dr, da]; bump(); },
            blendEquation(e) { st.blendEq = [e, e]; bump(); }, blendEquationSeparate(a, b) { st.blendEq = [a, b]; bump(); }, blendColor(r, g, b, a) { st.blendColor = [r, g, b, a]; },
            cullFace(m) { st.cullFace = m; bump(); }, frontFace(m) { st.frontFace = m; bump(); }, lineWidth(w) { st.lineWidth = w; }, polygonOffset(f, u) { st.polyOff = [f, u]; bump(); }, hint() {}, sampleCoverage() {},
            stencilFunc(f, r, m) { st.stencilFunc = [[f, r, m], [f, r, m]]; bump(); }, stencilFuncSeparate(face, f, r, m) { if (face !== 0x405) st.stencilFunc[0] = [f, r, m]; if (face !== 0x404) st.stencilFunc[1] = [f, r, m]; bump(); },
            stencilOp(a, b, c) { st.stencilOp = [[a, b, c], [a, b, c]]; bump(); }, stencilOpSeparate(face, a, b, c) { if (face !== 0x405) st.stencilOp[0] = [a, b, c]; if (face !== 0x404) st.stencilOp[1] = [a, b, c]; bump(); },
            stencilMask(m) { st.stencilMask = [m, m]; bump(); }, stencilMaskSeparate(face, m) { if (face !== 0x405) st.stencilMask[0] = m; if (face !== 0x404) st.stencilMask[1] = m; bump(); },
            pixelStorei(p, v) { if (p === 0x0CF5) st.unpackAlign = v; else if (p === 0x0D05) st.packAlign = v; else if (p === 0x9240) st.flipY = !!v; else if (p === 0x9241) st.premul = !!v; else if (p === 0x0CF2) st.rowLen = v; },
            /* buffers */
            createBuffer: () => Object.assign(new O.Buffer(), { id: 0, size: 0, data: new Uint8Array(0), ver: 0 }),
            deleteBuffer(b) { if (b && b.id) { trash.push(b.id); b.id = 0; } },
            isBuffer: b => b instanceof O.Buffer,
            bindBuffer(t, b) { if (t === 0x8892) st.arrayBuf = b; else if (t === 0x8893) vao.elem = b; else if (t === 0x8A11) st.uboGeneric = b; else if (t === 0x8F36) st.copyR = b; else if (t === 0x8F37) st.copyW = b; else st['b' + t] = b; },
            bufferData(t, d, usage, so, len) { const b = bufTarget(t); if (!b) return E(0x502);
                let bytes; if (typeof d === 'number') bytes = new Uint8Array(d); else if (ArrayBuffer.isView(d)) { const bpe = d.BYTES_PER_ELEMENT || 1; bytes = new Uint8Array(d.buffer, d.byteOffset + (so || 0) * bpe, len ? len * bpe : d.byteLength - (so || 0) * bpe); } else if (d) bytes = new Uint8Array(d); else bytes = new Uint8Array(0);
                const size = Math.max(4, (bytes.length + 3) & ~3);
                if (!b.id || b.cap !== size) { if (b.id) trash.push(b.id); b.id = N.gpuBuffer(size, BU.VERTEX | BU.INDEX | BU.UNIFORM | BU.DST | BU.SRC, false); b.cap = size; }
                else if (cmds.length) flush();
                b.data = new Uint8Array(size); b.data.set(bytes); b.size = bytes.length; b.usage = usage; b.ver++;
                if (bytes.length) N.gpuWriteBuffer(b.id, 0, b.data); },
            bufferSubData(t, off, d, so, len) { const b = bufTarget(t); if (!b || !b.id) return E(0x502);
                const bpe = d.BYTES_PER_ELEMENT || 1; const bytes = ArrayBuffer.isView(d) ? new Uint8Array(d.buffer, d.byteOffset + (so || 0) * bpe, len ? len * bpe : d.byteLength - (so || 0) * bpe) : new Uint8Array(d);
                if (off + bytes.length > b.data.length) return E(0x501);
                if (cmds.length) flush();
                b.data.set(bytes, off); const a0 = off & ~3, a1 = Math.min(b.data.length, (off + bytes.length + 3) & ~3); N.gpuWriteBuffer(b.id, a0, b.data.subarray(a0, a1)); b.ver++; },
            getBufferParameter(t, p) { const b = bufTarget(t); return !b ? null : p === 0x8764 ? b.size : b.usage; },
            /* vertex attribs */
            enableVertexAttribArray(i) { vao.attribs[i].en = true; vao.ver++; }, disableVertexAttribArray(i) { vao.attribs[i].en = false; vao.ver++; },
            vertexAttribPointer(i, size, type, norm, stride, off) { Object.assign(vao.attribs[i], { buf: st.arrayBuf, size, type, norm: !!norm, stride, off, int: false }); vao.ver++; },
            vertexAttribIPointer(i, size, type, stride, off) { Object.assign(vao.attribs[i], { buf: st.arrayBuf, size, type, norm: false, stride, off, int: true }); vao.ver++; },
            vertexAttribDivisor(i, d) { vao.attribs[i].div = d; vao.ver++; },
            vertexAttrib1f(i, x) { curAttr.set([x, 0, 0, 1], i * 4); curKind[i] = 'f'; }, vertexAttrib2f(i, x, y) { curAttr.set([x, y, 0, 1], i * 4); curKind[i] = 'f'; }, vertexAttrib3f(i, x, y, z) { curAttr.set([x, y, z, 1], i * 4); curKind[i] = 'f'; }, vertexAttrib4f(i, x, y, z, w) { curAttr.set([x, y, z, w], i * 4); curKind[i] = 'f'; },
            vertexAttrib1fv(i, v) { api.vertexAttrib1f(i, v[0]); }, vertexAttrib2fv(i, v) { api.vertexAttrib2f(i, v[0], v[1]); }, vertexAttrib3fv(i, v) { api.vertexAttrib3f(i, v[0], v[1], v[2]); }, vertexAttrib4fv(i, v) { api.vertexAttrib4f(i, v[0], v[1], v[2], v[3]); },
            vertexAttribI4i(i, x, y, z, w) { new Int32Array(curAttr.buffer, i * 16, 4).set([x, y, z, w]); curKind[i] = 'i'; }, vertexAttribI4ui(i, x, y, z, w) { new Uint32Array(curAttr.buffer, i * 16, 4).set([x, y, z, w]); curKind[i] = 'u'; },
            getVertexAttrib(i, p) { const A = vao.attribs[i]; return p === 0x8622 ? A.en : p === 0x8623 ? A.size : p === 0x8624 ? A.stride : p === 0x8625 ? A.type : p === 0x886A ? A.norm : p === 0x889F ? A.buf : p === 0x88FE ? A.div : p === 0x88FD ? A.int : p === 0x8626 ? curAttr.slice(i * 4, i * 4 + 4) : null; },
            getVertexAttribOffset: i => vao.attribs[i].off,
            createVertexArray: () => mkVao(), deleteVertexArray(v) { if (vao === v) vao = defVao; }, isVertexArray: v => v instanceof O.VertexArrayObject, bindVertexArray(v) { vao = v || defVao; },
            /* shaders/programs */
            createShader: t => Object.assign(new O.Shader(), { type: t, src: '', ok: false, log: '' }), deleteShader() {}, isShader: s => s instanceof O.Shader,
            shaderSource(s, src) { s.src = String(src); }, getShaderSource: s => s.src,
            compileShader(s) { try { LGLSL.parse(s.src, s.type === 0x8B31 ? 'v' : 'f'); s.ok = true; s.log = ''; } catch (e) { s.ok = false; s.log = 'ERROR: ' + e.message; } },
            getShaderParameter: (s, p) => p === 0x8B81 ? s.ok : p === 0x8B4F ? s.type : p === 0x8B80 ? false : null, getShaderInfoLog: s => s.log,
            createProgram: () => Object.assign(new O.Program(), { id: ids++, shaders: [], bound: new Map(), linked: false, log: '' }), deleteProgram(p) { if (p) progs.delete(p); }, isProgram: p => p instanceof O.Program,
            attachShader(p, s) { if (!p.shaders.includes(s)) p.shaders.push(s); }, detachShader(p, s) { p.shaders = p.shaders.filter(x => x !== s); }, getAttachedShaders: p => p.shaders.slice(),
            bindAttribLocation(p, i, n) { p.bound.set(n, i); }, linkProgram(p) { linkProgram(p); }, validateProgram() {},
            getProgramParameter(p, q) { return q === 0x8B82 ? p.linked : q === 0x8B83 ? p.linked : q === 0x8B80 ? false : q === 0x8B85 ? p.shaders.length : q === 0x8B86 ? (p.uniformList || []).length : q === 0x8B89 ? (p.attrList || []).length : q === 0x8A36 ? (p.L ? p.L.blocks.length : 0) : q === 0x91B1 ? true : q === 0x8C7F ? 0x8C8C : q === 0x8C83 ? 0 : null; },
            getProgramInfoLog: p => p.log, useProgram(p) { st.program = p; },
            getAttribLocation: (p, n) => p.linked && n in p.locs ? p.locs[n] : -1,
            getActiveAttrib(p, i) { const a = p.attrList && p.attrList[i]; return a ? Object.assign(new O.ActiveInfo(), { name: a.name, size: 1, type: TYPE[a.type] || 0x1406 }) : null; },
            getActiveUniform(p, i) { const u = p.uniformList && p.uniformList[i]; return u ? Object.assign(new O.ActiveInfo(), { name: u.name, size: u.size, type: TYPE[u.type] || 0x1406 }) : null; },
            getUniformLocation(p, name) { if (!p || !p.linked) return null; const m = /^(.*?)(?:\[(\d+)\])?$/.exec(name), base = m[1], k = m[2] ? +m[2] : 0;
                const u = p.uniformList.find(u => u.name === name || u.base === name || (u.size > 1 && u.base === base)); if (!u || k >= u.size) return null;
                return Object.assign(new O.UniformLocation(), { p, u, k, off: u.sampler ? 0 : u.off + k * u.stride }); },
            getUniform(p, l) { const P = LGLSL.PRIM[l.u.type]; if (l.u.sampler) return p.units.get(l.u.sampler.name)[l.k]; const [n, c, kd] = P, a = kd === 'f' ? p.uf : kd === 'u' ? p.uu : p.ui, o = l.off / 4;
                if (n * c === 1) return kd === 'b' ? !!a[o] : a[o]; const r = kd === 'f' ? new Float32Array(n * c) : new Int32Array(n * c); for (let j = 0; j < c; j++) for (let i = 0; i < n; i++) r[j * n + i] = a[o + j * 4 + i]; return r; },
            getUniformBlockIndex(p, n) { const i = p.L ? p.L.blocks.findIndex(b => b.name === n) : -1; return i < 0 ? 0xFFFFFFFF : i; },
            uniformBlockBinding(p, i, b) { p.blockBind[i] = b; },
            getActiveUniformBlockName: (p, i) => p.L.blocks[i].name,
            getActiveUniformBlockParameter(p, i, q) { const b = p.L.blocks[i]; return q === 0x8A40 ? b.S.layout().sz : q === 0x8A3F ? p.blockBind[i] : q === 0x8A42 ? b.S.members.length : q === 0x8A43 ? new Uint32Array(b.S.members.map((_, k) => k)) : true; },
            getUniformIndices(p, names) { const r = []; for (const n of names) { let idx = 0xFFFFFFFF; p.L.blocks.forEach(b => b.S.members.forEach((m, k) => { if (m.name === n || m.name + '[0]' === n) idx = k; })); r.push(idx); } return r; },
            getActiveUniforms(p, idxs, q) { const ms = p.L.blocks.flatMap(b => b.S.members); return idxs.map(i => { const m = ms[i]; return !m ? 0 : q === 0x8A3B ? m.off : q === 0x8A3C ? (m.l ? m.l.stride : 0) : q === 0x8A3D ? 16 : q === 0x8A38 ? m.arr || 1 : q === 0x8A37 ? TYPE[m.type] : q === 0x8A3A ? 0 : false; }); },
            bindBufferBase(t, i, b) { if (t === 0x8A11) { st.ubo[i] = b ? { buf: b, off: 0, size: -1 } : null; st.uboGeneric = b; } },
            bindBufferRange(t, i, b, off, size) { if (t === 0x8A11) { st.ubo[i] = b ? { buf: b, off, size } : null; st.uboGeneric = b; } },
            /* textures */
            createTexture: () => Object.assign(new O.Texture(), { id: ids++, t: 0, p: defParams(), target: 0, att: new Map(), w: 0, h: 0, d: 0 }),
            deleteTexture(t) { if (t && t.t) { trash.push(t.t, t.view, ...t.att.values()); t.t = 0; } }, isTexture: t => t instanceof O.Texture,
            activeTexture(u) { st.active = u - 0x84C0; },
            bindTexture(t, tx) { if (tx && !tx.target) tx.target = t; units[st.active][t] = tx; },
            texParameteri(t, p, v) { const tx = boundTex(t); if (!tx) return; const P = tx.p; if (p === 0x2801) P.min = v; else if (p === 0x2800) P.mag = v; else if (p === 0x2802) P.ws = v; else if (p === 0x2803) P.wt = v; else if (p === 0x8072) P.wr = v; else if (p === 0x884C) P.cmpMode = v; else if (p === 0x884D) P.cmpFunc = v; else if (p === 0x84FE) P.aniso = v; else if (p === 0x813A) P.minLod = v; else if (p === 0x813B) P.maxLod = v; },
            texParameterf(t, p, v) { api.texParameteri(t, p, v); },
            getTexParameter(t, p) { const tx = boundTex(t); if (!tx) return null; const P = tx.p; return { 0x2801: P.min, 0x2800: P.mag, 0x2802: P.ws, 0x2803: P.wt, 0x8072: P.wr, 0x884C: P.cmpMode, 0x884D: P.cmpFunc, 0x84FE: P.aniso }[p] ?? null; },
            texImage2D(t, lv, ifmt, a, b, c, d, e, f, g) {
                if (arguments.length === 6) return upload(t, lv, ifmt, 0, 0, 1, a, b, c, 0, 0, 0, false);
                const src = f, so = g; let data = src; if (ArrayBuffer.isView(src) && so) data = new src.constructor(src.buffer, src.byteOffset + so * src.BYTES_PER_ELEMENT);
                if (src && typeof src === 'object' && !ArrayBuffer.isView(src) && typeof src.byteLength !== 'number') return upload(t, lv, ifmt, a, b, 1, d, e, src, 0, 0, 0, false);
                upload(t, lv, ifmt, a, b, 1, d, e, data || null, 0, 0, 0, false); },
            texSubImage2D(t, lv, x, y, a, b, c, d, e, f) {
                const tx = boundTex(t); if (!tx || !tx.t) return E(0x502); const ifmt = { rgba8unorm: 0x8058, 'rgba8unorm-srgb': 0x8C43 }[tx.fmt] || 0;
                if (arguments.length === 7) return upload(t, lv, ifmt, 0, 0, 1, a, b, c, x, y, 0, true);
                upload(t, lv, ifmt, a, b, 1, c, d, e, x, y, 0, true); },
            texImage3D(t, lv, ifmt, w, h, d, bd, format, type, src) { const tx = boundTex(t); if (!tx) return E(0x502); const f = texFmt(ifmt, format, type); if (lv === 0) allocTex(tx, w, h, d, f.fmt, 1); if (src && ArrayBuffer.isView(src)) upload(t, lv, ifmt, w, h, d, format, type, src, 0, 0, 0, true); },
            texSubImage3D(t, lv, x, y, z, w, h, d, format, type, src) { const tx = boundTex(t); if (!tx || !tx.t) return E(0x502); if (src && ArrayBuffer.isView(src)) upload(t, lv, 0x8058, w, h, d, format, type, src, x, y, z, true); },
            texStorage2D(t, levels, ifmt, w, h) { const tx = boundTex(t); if (!tx) return E(0x502); const f = RBFMT[ifmt] || texFmt(ifmt, 0x1908, 0x1401).fmt; allocTex(tx, w, h, 1, f, levels); tx.immutable = true; },
            texStorage3D(t, levels, ifmt, w, h, d) { const tx = boundTex(t); if (!tx) return E(0x502); allocTex(tx, w, h, d, RBFMT[ifmt] || 'rgba8unorm', levels); },
            generateMipmap(t) { genMips(boundTex(t)); },
            copyTexImage2D(t, lv, ifmt, x, y, w, h) { const tx = boundTex(t); if (!tx) return E(0x502); if (lv === 0) allocTex(tx, w, h, 1, 'rgba8unorm'); api.copyTexSubImage2D(t, lv, 0, 0, x, y, w, h); },
            copyTexSubImage2D(t, lv, xo, yo, x, y, w, h) { const tx = boundTex(t), T = target(st.readFb || st.fb); if (!tx || !tx.t) return E(0x502); const src = readSource(); if (!src) return;
                endPass(); const layer = t >= 0x8515 && t <= 0x851A ? t - 0x8515 : 0; cmds.push(['t2t', src.t, src.level, x, y, src.layer, tx.t, lv, xo, yo, layer, Math.min(w, T.w - x), Math.min(h, T.h - y), 1]); },
            compressedTexImage2D() { E(0x500); }, compressedTexSubImage2D() { E(0x500); },
            createSampler: () => Object.assign(new O.Sampler(), { p: defParams() }), deleteSampler() {}, isSampler: s => s instanceof O.Sampler, bindSampler(u, s) { samplerObjs[u] = s; },
            samplerParameteri(s, p, v) { const P = s.p; if (p === 0x2801) P.min = v; else if (p === 0x2800) P.mag = v; else if (p === 0x2802) P.ws = v; else if (p === 0x2803) P.wt = v; else if (p === 0x8072) P.wr = v; else if (p === 0x884C) P.cmpMode = v; else if (p === 0x884D) P.cmpFunc = v; else if (p === 0x813A) P.minLod = v; else if (p === 0x813B) P.maxLod = v; },
            samplerParameterf(s, p, v) { api.samplerParameteri(s, p, v); },
            /* framebuffers */
            createFramebuffer: () => Object.assign(new O.Framebuffer(), { id: ids++, att: {}, drawBufs: [0x8CE0] }), deleteFramebuffer(f) { if (st.fb === f) st.fb = null; }, isFramebuffer: f => f instanceof O.Framebuffer,
            bindFramebuffer(t, f) { if (t === 0x8CA8) st.readFb = f; else { st.fb = f; if (t === 0x8D40) st.readFb = f; } },
            framebufferTexture2D(t, at, tt, tx, lv) { const f = t === 0x8CA8 ? st.readFb : st.fb; if (!f) return E(0x502); if (!tx) delete f.att[at]; else f.att[at] = { obj: tx, level: lv, layer: tt >= 0x8515 && tt <= 0x851A ? tt - 0x8515 : 0 }; if (at === 0x821A) delete f.att[0x8D00]; },
            framebufferTextureLayer(t, at, tx, lv, layer) { const f = st.fb; if (!f) return E(0x502); if (!tx) delete f.att[at]; else f.att[at] = { obj: tx, level: lv, layer }; },
            framebufferRenderbuffer(t, at, rt, rb) { const f = t === 0x8CA8 ? st.readFb : st.fb; if (!f) return E(0x502); if (!rb) delete f.att[at]; else f.att[at] = { obj: rb, rb: true, level: 0, layer: 0 }; },
            checkFramebufferStatus(t) { const f = t === 0x8CA8 ? st.readFb : st.fb; if (!f) return 0x8CD5; const a = Object.values(f.att); if (!a.length) return 0x8CD7; return a.every(x => x.obj.t) ? 0x8CD5 : 0x8CD6; },
            getFramebufferAttachmentParameter(t, at, p) { const f = st.fb; const a = f && f.att[at]; if (p === 0x8CD0) return a ? (a.rb ? 0x8D41 : 0x1702) : 0; if (p === 0x8CD1) return a ? a.obj : null; if (p === 0x8CD2) return a ? a.level : 0; if (p === 0x8210) return a && /srgb/.test(a.obj.fmt) ? 0x8C40 : 0x2601; if (p === 0x8211) return 0x8C17; return 8; },
            drawBuffers(b) { if (st.fb) st.fb.drawBufs = Array.from(b); }, readBuffer() {},
            invalidateFramebuffer() {}, invalidateSubFramebuffer() {},
            createRenderbuffer: () => Object.assign(new O.Renderbuffer(), { id: ids++, t: 0, w: 0, h: 0 }), deleteRenderbuffer(r) { if (r && r.t) { trash.push(r.t, r.v); r.t = 0; } }, isRenderbuffer: r => r instanceof O.Renderbuffer,
            bindRenderbuffer(t, r) { st.rb = r; },
            renderbufferStorage(t, f, w, h) { const r = st.rb; if (!r) return E(0x502); if (r.t) trash.push(r.t, r.v); r.fmt = RBFMT[f] || 'rgba8unorm'; r.w = w; r.h = h; r.ifmt = f; r.t = N.gpuTexture(Math.max(1, w), Math.max(1, h), 1, r.fmt, TU.RT | TU.SRC | TU.BIND, 1, 1, '2d'); r.v = N.gpuView(r.t, {}); },
            renderbufferStorageMultisample(t, s, f, w, h) { api.renderbufferStorage(t, f, w, h); },
            getRenderbufferParameter(t, p) { const r = st.rb; return !r ? null : p === 0x8D42 ? r.w : p === 0x8D43 ? r.h : p === 0x8D44 ? r.ifmt : p === 0x8CAB ? 0 : 8; },
            blitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask) { const src = readSource(), D = target(st.fb); if (!src || !(mask & 0x4000) || !D.colors[0]) return;
                const w = Math.min(sx1 - sx0, dx1 - dx0), h = Math.min(sy1 - sy0, dy1 - dy0); if (w <= 0 || h <= 0) return; const dt = st.fb ? (st.fb.att[0x8CE0] || {}).obj : def.color; if (!dt || !dt.t) return; const dl = st.fb ? st.fb.att[0x8CE0].level : 0;
                endPass(); cmds.push(['t2t', src.t, src.level, sx0, sy0, src.layer, dt.t, dl, dx0, dy0, 0, w, h, 1]); if (!st.fb) schedulePresent(); },
            readPixels(x, y, w, h, format, type, dst, dOff) { if (!dst || !ArrayBuffer.isView(dst)) return; const src = readSource(); if (!src) return E(0x506);
                const bpp = /32float/.test(src.fmt) ? 16 : /16float/.test(src.fmt) ? 8 : 4, bpr = Math.ceil(w * bpp / 256) * 256;
                const x0 = Math.max(0, x), y0 = Math.max(0, y), x1 = Math.min(src.w, x + w), y1 = Math.min(src.h, y + h); if (x1 <= x0 || y1 <= y0) return;
                flush(); const cw = x1 - x0, ch = y1 - y0, sb = N.gpuBuffer(bpr * ch, BU.MAPR | BU.DST, false);
                N.gpuExec([['t2b', src.t, src.level, x0, y0, src.layer, sb, 0, bpr, ch, 0, cw, ch, 1]]); const ab = N.gpuMapRead(sb, 0, bpr * ch); N.gpuDrop(sb); if (!ab) return;
                const s8 = new Uint8Array(ab), out = new Uint8Array(dst.buffer, dst.byteOffset + (dOff || 0) * (dst.BYTES_PER_ELEMENT || 1)), outBpp = type === 0x1406 ? 16 : 4, rowOut = Math.ceil(w * outBpp / st.packAlign) * st.packAlign;
                for (let r = 0; r < ch; r++) { const so = r * bpr, oo = (y0 - y + r) * rowOut + (x0 - x) * outBpp;
                    if (outBpp === bpp) out.set(s8.subarray(so, so + cw * bpp), oo);
                    else if (bpp === 16 && outBpp === 4) { const f = new Float32Array(ab, so, cw * 4); for (let i = 0; i < cw * 4; i++) out[oo + i] = Math.max(0, Math.min(255, Math.round(f[i] * 255))); }
                    else if (bpp === 4 && outBpp === 16) { const f = new Float32Array(out.buffer, out.byteOffset + oo, cw * 4); for (let i = 0; i < cw * 4; i++) f[i] = s8[so + i] / 255; } } },
            /* draws */
            drawArrays(m, f, c) { draw(m, f, c, 1, null); }, drawElements(m, c, t, o) { draw(m, 0, c, 1, { type: t, off: o }); },
            drawArraysInstanced(m, f, c, i) { draw(m, f, c, i, null); }, drawElementsInstanced(m, c, t, o, i) { draw(m, 0, c, i, { type: t, off: o }); },
            drawRangeElements(m, s, e, c, t, o) { draw(m, 0, c, 1, { type: t, off: o }); },
            copyBufferSubData(rt, wt, ro, wo, n) { const r = bufTarget(rt), w = bufTarget(wt); if (!r || !w) return E(0x502); endPass(); w.data.set(r.data.subarray(ro, ro + n), wo); cmds.push(['b2b', r.id, ro, w.id, wo, n]); },
            getBufferSubData(t, off, dst, dOff, len) { const b = bufTarget(t); if (!b) return E(0x502); const bpe = dst.BYTES_PER_ELEMENT || 1, n = len ? len * bpe : dst.byteLength - (dOff || 0) * bpe; new Uint8Array(dst.buffer, dst.byteOffset + (dOff || 0) * bpe, n).set(b.data.subarray(off, off + n)); },
            createQuery: () => new O.Query(), deleteQuery() {}, beginQuery() {}, endQuery() {}, getQuery: () => null, getQueryParameter: (q, p) => p === 0x8867 ? true : 0, isQuery: q => q instanceof O.Query,
            fenceSync: () => new O.Sync(), clientWaitSync: () => 0x911A, waitSync() {}, deleteSync() {}, getSyncParameter: () => 0x9119, isSync: s => s instanceof O.Sync,
            createTransformFeedback: () => new O.TransformFeedback(), bindTransformFeedback() {}, deleteTransformFeedback() {}, beginTransformFeedback() {}, endTransformFeedback() {}, transformFeedbackVaryings() {}, pauseTransformFeedback() {}, resumeTransformFeedback() {},
            clearBufferfv(b, i, v) { const T = target(st.fb), c = T.clr.clearPending || {}; if (b === 0x1800) c.color = Array.from(v).slice(0, 4); else if (b === 0x1801) c.depth = v[0]; if (pass && passTarget === T.key) endPass(); T.clr.clearPending = c; if (!st.fb) schedulePresent(); },
            clearBufferiv(b, i, v) { if (b === 0x1802) { const T = target(st.fb), c = T.clr.clearPending || {}; c.stencil = v[0]; if (pass && passTarget === T.key) endPass(); T.clr.clearPending = c; } },
            clearBufferuiv() {}, clearBufferfi(b, i, d, s) { const T = target(st.fb), c = T.clr.clearPending || {}; c.depth = d; c.stencil = s; if (pass && passTarget === T.key) endPass(); T.clr.clearPending = c; },
            getInternalformatParameter: () => new Int32Array([4]),
            getParameter(p) {
                switch (p) {
                    case 0x1F00: return 'WebKit'; case 0x1F01: return 'WebKit WebGL'; case 0x9245: return 'Lumen'; case 0x9246: return 'Lumen WebGPU (' + devName + ')';
                    case 0x1F02: return isGL2 ? 'WebGL 2.0 (OpenGL ES 3.0 Lumen)' : 'WebGL 1.0 (OpenGL ES 2.0 Lumen)'; case 0x8B8C: return isGL2 ? 'WebGL GLSL ES 3.00 (OpenGL ES GLSL ES 3.0 Lumen)' : 'WebGL GLSL ES 1.0 (OpenGL ES GLSL ES 1.0 Lumen)';
                    case 0x0D33: case 0x851C: case 0x84E8: return 8192; case 0x8073: return 2048; case 0x88FF: return 256; case 0x8869: return 16; case 0x8DFB: case 0x8DFD: return 1024; case 0x8DFC: return 15;
                    case 0x8872: case 0x8B4C: return 16; case 0x8B4D: return 32; case 0x0D3A: return new Int32Array([8192, 8192]); case 0x846D: case 0x846E: return new Float32Array([1, 1]);
                    case 0x8D57: return 4; case 0x8824: case 0x8CDF: return 8; case 0x8A2F: return 11; case 0x8A30: return 65536; case 0x8A34: return 256; case 0x8A2B: case 0x8A2D: return 11; case 0x8A2E: return 22;
                    case 0x84FD: return 2; case 0x8D6B: return 4294967295; case 0x8B4A: case 0x8B49: return 4096; case 0x8B4B: return 60; case 0x9122: return 64; case 0x9125: return 60; case 0x80E8: case 0x80E9: return 1 << 20;
                    case 0x8905: return 7; case 0x8904: return -8; case 0x9111: return 0; case 0x84FF: return 16; case 0x0D50: return 4; case 0x8A31: case 0x8A33: return 65536;
                    case 0x0BA2: return new Int32Array(st.viewport); case 0x0C10: return new Int32Array(st.scissor); case 0x0C22: return new Float32Array(st.clearColor); case 0x0B73: return st.clearDepth; case 0x0B91: return st.clearStencil;
                    case 0x0C23: return st.colorMask.slice(); case 0x0B72: return st.depthMask; case 0x0B74: return st.depthFunc; case 0x0B70: return new Float32Array(st.depthRange);
                    case 0x0BE2: case 0x0B71: case 0x0B44: case 0x0C11: case 0x0B90: case 0x8037: case 0x0BD0: case 0x809E: case 0x80A0: case 0x8C89: return st.en.has(p);
                    case 0x0B45: return st.cullFace; case 0x0B46: return st.frontFace; case 0x8894: return st.arrayBuf; case 0x8895: return vao.elem; case 0x8B8D: return st.program; case 0x8CA6: return st.fb; case 0x8CAA: return st.readFb; case 0x8CA7: return st.rb;
                    case 0x8069: return units[st.active][0x0DE1] || null; case 0x8514: return units[st.active][0x8513] || null; case 0x806A: return units[st.active][0x806F] || null; case 0x8C1D: return units[st.active][0x8C1A] || null; case 0x84E0: return 0x84C0 + st.active;
                    case 0x0CF5: return st.unpackAlign; case 0x0D05: return st.packAlign; case 0x9240: return st.flipY; case 0x9241: return st.premul; case 0x9243: return 0x9244; case 0x0CF2: return st.rowLen;
                    case 0x0D52: case 0x0D53: case 0x0D54: case 0x0D55: return 8; case 0x0D56: return attrs.depth ? 24 : 0; case 0x0D57: return attrs.stencil ? 8 : 0; case 0x80A9: return 0; case 0x80A8: return 0;
                    case 0x80C9: return st.blendSrc[0]; case 0x80C8: return st.blendDst[0]; case 0x80CB: return st.blendSrc[1]; case 0x80CA: return st.blendDst[1]; case 0x8009: return st.blendEq[0]; case 0x883D: return st.blendEq[1]; case 0x8005: return new Float32Array(st.blendColor);
                    case 0x0B21: return st.lineWidth; case 0x8038: return st.polyOff[0]; case 0x2A00: return st.polyOff[1]; case 0x8B9B: return 0x1908; case 0x8B9A: return 0x1401; case 0x85B5: return vao === defVao ? null : vao; case 0x86A3: return new Uint32Array(0);
                    case 0x0B92: return st.stencilFunc[0][0]; case 0x0B97: return st.stencilFunc[0][1]; case 0x0B93: return st.stencilFunc[0][2]; case 0x0B98: return st.stencilMask[0]; case 0x8800: return st.stencilFunc[1][0]; case 0x8CA3: return st.stencilFunc[1][1]; case 0x8CA4: return st.stencilFunc[1][2]; case 0x8CA5: return st.stencilMask[1];
                    case 0x0B94: return st.stencilOp[0][0]; case 0x0B95: return st.stencilOp[0][1]; case 0x0B96: return st.stencilOp[0][2]; case 0x8801: return st.stencilOp[1][0]; case 0x8802: return st.stencilOp[1][1]; case 0x8803: return st.stencilOp[1][2]; case 0x8192: return 0x1100;
                    case 0x8A28: return st.uboGeneric || null; case 0x0C02: return 0x8CE0;
                }
                if (p >= 0x8825 && p <= 0x8834) { const i = p - 0x8825; return st.fb ? (st.fb.drawBufs[i] || 0) : i === 0 ? 0x405 : 0; }
                E(0x500); return null;
            },
            uniform1f: (l, x) => setU(l, [x]), uniform2f: (l, x, y) => setU(l, [x, y]), uniform3f: (l, x, y, z) => setU(l, [x, y, z]), uniform4f: (l, x, y, z, w) => setU(l, [x, y, z, w]),
            uniform1i: (l, x) => setU(l, [x]), uniform2i: (l, x, y) => setU(l, [x, y]), uniform3i: (l, x, y, z) => setU(l, [x, y, z]), uniform4i: (l, x, y, z, w) => setU(l, [x, y, z, w]),
            uniform1ui: (l, x) => setU(l, [x]), uniform2ui: (l, x, y) => setU(l, [x, y]), uniform3ui: (l, x, y, z) => setU(l, [x, y, z]), uniform4ui: (l, x, y, z, w) => setU(l, [x, y, z, w]),
        };
        for (const n of ['1fv', '2fv', '3fv', '4fv', '1iv', '2iv', '3iv', '4iv', '1uiv', '2uiv', '3uiv', '4uiv']) api['uniform' + n] = (l, v, so, sl) => setU(l, sl ? Array.prototype.slice.call(v, so || 0, (so || 0) + sl) : so ? Array.prototype.slice.call(v, so) : v);
        for (const n of ['2', '3', '4', '2x3', '2x4', '3x2', '3x4', '4x2', '4x3']) api['uniformMatrix' + n + 'fv'] = (l, tr, v, so, sl) => setU(l, sl ? Array.prototype.slice.call(v, so || 0, (so || 0) + sl) : so ? Array.prototype.slice.call(v, so) : v);
        function setU(l, v) {
            if (!l) return; const p = l.p; if (p !== st.program) return E(0x502);
            const u = l.u; if (u.sampler) { const a = p.units.get(u.sampler.name); for (let i = 0; i + l.k < a.length && i < v.length; i++) a[l.k + i] = v[i]; return; }
            const [n, c, kd] = LGLSL.PRIM[u.type], a = kd === 'f' ? p.uf : kd === 'u' ? p.uu : p.ui, per = n * c, cnt = Math.min(u.size - l.k, Math.floor(v.length / per)), st4 = u.stride ? u.stride / 4 : 0;
            for (let e = 0; e < cnt; e++) { const base = l.off / 4 + e * st4, vb = e * per;
                if (c === 1) for (let i = 0; i < n; i++) a[base + i] = kd === 'b' ? (v[vb + i] ? 1 : 0) : v[vb + i];
                else for (let j = 0; j < c; j++) for (let i = 0; i < n; i++) a[base + j * 4 + i] = v[vb + j * n + i]; }
            p.dirty = true;
        }
        function bufTarget(t) { return t === 0x8892 ? st.arrayBuf : t === 0x8893 ? vao.elem : t === 0x8A11 ? st.uboGeneric : t === 0x8F36 ? st.copyR : t === 0x8F37 ? st.copyW : st['b' + t]; }
        function readSource() { const f = st.readFb; if (!f) { ensureDefault(); if (def.clearPending && def.clearPending.color) { beginPass(target(null)); } return { t: def.color.t, level: 0, layer: 0, fmt: 'rgba8unorm', w: def.w, h: def.h }; }
            const a = f.att[0x8CE0]; if (!a || !a.obj.t) return null; if (f.clearPending) { beginPass(target(f)); } const o = a.obj; return { t: o.t, level: a.level, layer: a.layer, fmt: o.fmt, w: a.rb ? o.w : Math.max(1, o.w >> a.level), h: a.rb ? o.h : Math.max(1, o.h >> a.level) }; }
        Object.assign(gl, api);
        return gl;
    };
})();
