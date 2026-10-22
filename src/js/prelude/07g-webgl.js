// ---- WebGL 1 and 2 over N.gl (src/js/webgl.cc, OpenGL ES 3 in src/gpu/webgl.c) ----
// constant tables generated from the GLES 2/3 headers (name=hex); WebGL 2 adds GL2_NAMES to GL1_NAMES
const GL1_NAMES = 'NONE=0 NO_ERROR=0 POINTS=0 ZERO=0 LINES=1 ONE=1 LINE_LOOP=2 LINE_STRIP=3 TRIANGLES=4 TRIANGLE_STRIP=5 TRIANGLE_FAN=6 DEPTH_BUFFER_BIT=100 NEVER=200 LESS=201 EQUAL=202 LEQUAL=203 GREATER=204 NOTEQUAL=205 GEQUAL=206 ALWAYS=207 SRC_COLOR=300 ONE_MINUS_SRC_COLOR=301 SRC_ALPHA=302 ONE_MINUS_SRC_ALPHA=303 DST_ALPHA=304 ONE_MINUS_DST_ALPHA=305 DST_COLOR=306 ONE_MINUS_DST_COLOR=307 SRC_ALPHA_SATURATE=308 STENCIL_BUFFER_BIT=400 FRONT=404 BACK=405 FRONT_AND_BACK=408 INVALID_ENUM=500 INVALID_VALUE=501 INVALID_OPERATION=502 OUT_OF_MEMORY=505 INVALID_FRAMEBUFFER_OPERATION=506 CW=900 CCW=901 LINE_WIDTH=b21 CULL_FACE=b44 CULL_FACE_MODE=b45 FRONT_FACE=b46 DEPTH_RANGE=b70 DEPTH_TEST=b71 DEPTH_WRITEMASK=b72 DEPTH_CLEAR_VALUE=b73 DEPTH_FUNC=b74 STENCIL_TEST=b90 STENCIL_CLEAR_VALUE=b91 STENCIL_FUNC=b92 STENCIL_VALUE_MASK=b93 STENCIL_FAIL=b94 STENCIL_PASS_DEPTH_FAIL=b95 STENCIL_PASS_DEPTH_PASS=b96 STENCIL_REF=b97 STENCIL_WRITEMASK=b98 VIEWPORT=ba2 DITHER=bd0 BLEND=be2 SCISSOR_BOX=c10 SCISSOR_TEST=c11 COLOR_CLEAR_VALUE=c22 COLOR_WRITEMASK=c23 UNPACK_ALIGNMENT=cf5 PACK_ALIGNMENT=d05 MAX_TEXTURE_SIZE=d33 MAX_VIEWPORT_DIMS=d3a SUBPIXEL_BITS=d50 RED_BITS=d52 GREEN_BITS=d53 BLUE_BITS=d54 ALPHA_BITS=d55 DEPTH_BITS=d56 STENCIL_BITS=d57 TEXTURE_2D=de1 DONT_CARE=1100 FASTEST=1101 NICEST=1102 BYTE=1400 UNSIGNED_BYTE=1401 SHORT=1402 UNSIGNED_SHORT=1403 INT=1404 UNSIGNED_INT=1405 FLOAT=1406 INVERT=150a TEXTURE=1702 DEPTH_COMPONENT=1902 ALPHA=1906 RGB=1907 RGBA=1908 LUMINANCE=1909 LUMINANCE_ALPHA=190a KEEP=1e00 REPLACE=1e01 INCR=1e02 DECR=1e03 VENDOR=1f00 RENDERER=1f01 VERSION=1f02 NEAREST=2600 LINEAR=2601 NEAREST_MIPMAP_NEAREST=2700 LINEAR_MIPMAP_NEAREST=2701 NEAREST_MIPMAP_LINEAR=2702 LINEAR_MIPMAP_LINEAR=2703 TEXTURE_MAG_FILTER=2800 TEXTURE_MIN_FILTER=2801 TEXTURE_WRAP_S=2802 TEXTURE_WRAP_T=2803 REPEAT=2901 POLYGON_OFFSET_UNITS=2a00 COLOR_BUFFER_BIT=4000 CONSTANT_COLOR=8001 ONE_MINUS_CONSTANT_COLOR=8002 CONSTANT_ALPHA=8003 ONE_MINUS_CONSTANT_ALPHA=8004 BLEND_COLOR=8005 FUNC_ADD=8006 BLEND_EQUATION=8009 BLEND_EQUATION_RGB=8009 FUNC_SUBTRACT=800a FUNC_REVERSE_SUBTRACT=800b UNSIGNED_SHORT_4_4_4_4=8033 UNSIGNED_SHORT_5_5_5_1=8034 POLYGON_OFFSET_FILL=8037 POLYGON_OFFSET_FACTOR=8038 RGBA4=8056 RGB5_A1=8057 RGBA8=8058 TEXTURE_BINDING_2D=8069 SAMPLE_ALPHA_TO_COVERAGE=809e SAMPLE_COVERAGE=80a0 SAMPLE_BUFFERS=80a8 SAMPLES=80a9 SAMPLE_COVERAGE_VALUE=80aa SAMPLE_COVERAGE_INVERT=80ab BLEND_DST_RGB=80c8 BLEND_SRC_RGB=80c9 BLEND_DST_ALPHA=80ca BLEND_SRC_ALPHA=80cb CLAMP_TO_EDGE=812f GENERATE_MIPMAP_HINT=8192 DEPTH_COMPONENT16=81a5 DEPTH_STENCIL_ATTACHMENT=821a UNSIGNED_SHORT_5_6_5=8363 MIRRORED_REPEAT=8370 ALIASED_POINT_SIZE_RANGE=846d ALIASED_LINE_WIDTH_RANGE=846e TEXTURE0=84c0 TEXTURE1=84c1 TEXTURE2=84c2 TEXTURE3=84c3 TEXTURE4=84c4 TEXTURE5=84c5 TEXTURE6=84c6 TEXTURE7=84c7 TEXTURE8=84c8 TEXTURE9=84c9 TEXTURE10=84ca TEXTURE11=84cb TEXTURE12=84cc TEXTURE13=84cd TEXTURE14=84ce TEXTURE15=84cf TEXTURE16=84d0 TEXTURE17=84d1 TEXTURE18=84d2 TEXTURE19=84d3 TEXTURE20=84d4 TEXTURE21=84d5 TEXTURE22=84d6 TEXTURE23=84d7 TEXTURE24=84d8 TEXTURE25=84d9 TEXTURE26=84da TEXTURE27=84db TEXTURE28=84dc TEXTURE29=84dd TEXTURE30=84de TEXTURE31=84df ACTIVE_TEXTURE=84e0 MAX_RENDERBUFFER_SIZE=84e8 DEPTH_STENCIL=84f9 INCR_WRAP=8507 DECR_WRAP=8508 TEXTURE_CUBE_MAP=8513 TEXTURE_BINDING_CUBE_MAP=8514 TEXTURE_CUBE_MAP_POSITIVE_X=8515 TEXTURE_CUBE_MAP_NEGATIVE_X=8516 TEXTURE_CUBE_MAP_POSITIVE_Y=8517 TEXTURE_CUBE_MAP_NEGATIVE_Y=8518 TEXTURE_CUBE_MAP_POSITIVE_Z=8519 TEXTURE_CUBE_MAP_NEGATIVE_Z=851a MAX_CUBE_MAP_TEXTURE_SIZE=851c VERTEX_ATTRIB_ARRAY_ENABLED=8622 VERTEX_ATTRIB_ARRAY_SIZE=8623 VERTEX_ATTRIB_ARRAY_STRIDE=8624 VERTEX_ATTRIB_ARRAY_TYPE=8625 CURRENT_VERTEX_ATTRIB=8626 VERTEX_ATTRIB_ARRAY_POINTER=8645 NUM_COMPRESSED_TEXTURE_FORMATS=86a2 COMPRESSED_TEXTURE_FORMATS=86a3 BUFFER_SIZE=8764 BUFFER_USAGE=8765 STENCIL_BACK_FUNC=8800 STENCIL_BACK_FAIL=8801 STENCIL_BACK_PASS_DEPTH_FAIL=8802 STENCIL_BACK_PASS_DEPTH_PASS=8803 BLEND_EQUATION_ALPHA=883d MAX_VERTEX_ATTRIBS=8869 VERTEX_ATTRIB_ARRAY_NORMALIZED=886a MAX_TEXTURE_IMAGE_UNITS=8872 ARRAY_BUFFER=8892 ELEMENT_ARRAY_BUFFER=8893 ARRAY_BUFFER_BINDING=8894 ELEMENT_ARRAY_BUFFER_BINDING=8895 VERTEX_ATTRIB_ARRAY_BUFFER_BINDING=889f STREAM_DRAW=88e0 STATIC_DRAW=88e4 DYNAMIC_DRAW=88e8 FRAGMENT_SHADER=8b30 VERTEX_SHADER=8b31 MAX_VERTEX_TEXTURE_IMAGE_UNITS=8b4c MAX_COMBINED_TEXTURE_IMAGE_UNITS=8b4d SHADER_TYPE=8b4f FLOAT_VEC2=8b50 FLOAT_VEC3=8b51 FLOAT_VEC4=8b52 INT_VEC2=8b53 INT_VEC3=8b54 INT_VEC4=8b55 BOOL=8b56 BOOL_VEC2=8b57 BOOL_VEC3=8b58 BOOL_VEC4=8b59 FLOAT_MAT2=8b5a FLOAT_MAT3=8b5b FLOAT_MAT4=8b5c SAMPLER_2D=8b5e SAMPLER_CUBE=8b60 DELETE_STATUS=8b80 COMPILE_STATUS=8b81 LINK_STATUS=8b82 VALIDATE_STATUS=8b83 INFO_LOG_LENGTH=8b84 ATTACHED_SHADERS=8b85 ACTIVE_UNIFORMS=8b86 ACTIVE_UNIFORM_MAX_LENGTH=8b87 SHADER_SOURCE_LENGTH=8b88 ACTIVE_ATTRIBUTES=8b89 ACTIVE_ATTRIBUTE_MAX_LENGTH=8b8a SHADING_LANGUAGE_VERSION=8b8c CURRENT_PROGRAM=8b8d IMPLEMENTATION_COLOR_READ_TYPE=8b9a IMPLEMENTATION_COLOR_READ_FORMAT=8b9b STENCIL_BACK_REF=8ca3 STENCIL_BACK_VALUE_MASK=8ca4 STENCIL_BACK_WRITEMASK=8ca5 FRAMEBUFFER_BINDING=8ca6 RENDERBUFFER_BINDING=8ca7 FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE=8cd0 FRAMEBUFFER_ATTACHMENT_OBJECT_NAME=8cd1 FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL=8cd2 FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE=8cd3 FRAMEBUFFER_COMPLETE=8cd5 FRAMEBUFFER_INCOMPLETE_ATTACHMENT=8cd6 FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT=8cd7 FRAMEBUFFER_INCOMPLETE_DIMENSIONS=8cd9 FRAMEBUFFER_UNSUPPORTED=8cdd COLOR_ATTACHMENT0=8ce0 DEPTH_ATTACHMENT=8d00 STENCIL_ATTACHMENT=8d20 FRAMEBUFFER=8d40 RENDERBUFFER=8d41 RENDERBUFFER_WIDTH=8d42 RENDERBUFFER_HEIGHT=8d43 RENDERBUFFER_INTERNAL_FORMAT=8d44 STENCIL_INDEX8=8d48 RENDERBUFFER_RED_SIZE=8d50 RENDERBUFFER_GREEN_SIZE=8d51 RENDERBUFFER_BLUE_SIZE=8d52 RENDERBUFFER_ALPHA_SIZE=8d53 RENDERBUFFER_DEPTH_SIZE=8d54 RENDERBUFFER_STENCIL_SIZE=8d55 RGB565=8d62 LOW_FLOAT=8df0 MEDIUM_FLOAT=8df1 HIGH_FLOAT=8df2 LOW_INT=8df3 MEDIUM_INT=8df4 HIGH_INT=8df5 MAX_VERTEX_UNIFORM_VECTORS=8dfb MAX_VARYING_VECTORS=8dfc MAX_FRAGMENT_UNIFORM_VECTORS=8dfd UNPACK_FLIP_Y_WEBGL=9240 UNPACK_PREMULTIPLY_ALPHA_WEBGL=9241 CONTEXT_LOST_WEBGL=9242 UNPACK_COLORSPACE_CONVERSION_WEBGL=9243 BROWSER_DEFAULT_WEBGL=9244';
const GL2_NAMES = 'TIMEOUT_IGNORED=-1 MAP_READ_BIT=1 SYNC_FLUSH_COMMANDS_BIT=1 MAP_WRITE_BIT=2 MAP_INVALIDATE_RANGE_BIT=4 MAP_INVALIDATE_BUFFER_BIT=8 MAP_FLUSH_EXPLICIT_BIT=10 MAP_UNSYNCHRONIZED_BIT=20 READ_BUFFER=c02 UNPACK_ROW_LENGTH=cf2 UNPACK_SKIP_ROWS=cf3 UNPACK_SKIP_PIXELS=cf4 PACK_ROW_LENGTH=d02 PACK_SKIP_ROWS=d03 PACK_SKIP_PIXELS=d04 HALF_FLOAT=140b COLOR=1800 DEPTH=1801 STENCIL=1802 RED=1903 GREEN=1904 BLUE=1905 MIN=8007 MAX=8008 RGB8=8051 RGBA8=8058 RGB10_A2=8059 TEXTURE_BINDING_3D=806a UNPACK_SKIP_IMAGES=806d UNPACK_IMAGE_HEIGHT=806e TEXTURE_3D=806f TEXTURE_WRAP_R=8072 MAX_3D_TEXTURE_SIZE=8073 MAX_ELEMENTS_VERTICES=80e8 MAX_ELEMENTS_INDICES=80e9 TEXTURE_MIN_LOD=813a TEXTURE_MAX_LOD=813b TEXTURE_BASE_LEVEL=813c TEXTURE_MAX_LEVEL=813d DEPTH_COMPONENT24=81a6 FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING=8210 FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE=8211 FRAMEBUFFER_ATTACHMENT_RED_SIZE=8212 FRAMEBUFFER_ATTACHMENT_GREEN_SIZE=8213 FRAMEBUFFER_ATTACHMENT_BLUE_SIZE=8214 FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE=8215 FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE=8216 FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE=8217 FRAMEBUFFER_DEFAULT=8218 FRAMEBUFFER_UNDEFINED=8219 DEPTH_STENCIL_ATTACHMENT=821a RG=8227 RG_INTEGER=8228 R8=8229 RG8=822b R16F=822d R32F=822e RG16F=822f RG32F=8230 R8I=8231 R8UI=8232 R16I=8233 R16UI=8234 R32I=8235 R32UI=8236 RG8I=8237 RG8UI=8238 RG16I=8239 RG16UI=823a RG32I=823b RG32UI=823c TEXTURE_IMMUTABLE_LEVELS=82df UNSIGNED_INT_2_10_10_10_REV=8368 DEPTH_STENCIL=84f9 UNSIGNED_INT_24_8=84fa MAX_TEXTURE_LOD_BIAS=84fd VERTEX_ARRAY_BINDING=85b5 RGBA32F=8814 RGB32F=8815 RGBA16F=881a RGB16F=881b MAX_DRAW_BUFFERS=8824 DRAW_BUFFER0=8825 DRAW_BUFFER1=8826 DRAW_BUFFER2=8827 DRAW_BUFFER3=8828 DRAW_BUFFER4=8829 DRAW_BUFFER5=882a DRAW_BUFFER6=882b DRAW_BUFFER7=882c DRAW_BUFFER8=882d DRAW_BUFFER9=882e DRAW_BUFFER10=882f DRAW_BUFFER11=8830 DRAW_BUFFER12=8831 DRAW_BUFFER13=8832 DRAW_BUFFER14=8833 DRAW_BUFFER15=8834 TEXTURE_COMPARE_MODE=884c TEXTURE_COMPARE_FUNC=884d COMPARE_REF_TO_TEXTURE=884e CURRENT_QUERY=8865 QUERY_RESULT=8866 QUERY_RESULT_AVAILABLE=8867 BUFFER_MAPPED=88bc BUFFER_MAP_POINTER=88bd STREAM_READ=88e1 STREAM_COPY=88e2 STATIC_READ=88e5 STATIC_COPY=88e6 DYNAMIC_READ=88e9 DYNAMIC_COPY=88ea PIXEL_PACK_BUFFER=88eb PIXEL_UNPACK_BUFFER=88ec PIXEL_PACK_BUFFER_BINDING=88ed PIXEL_UNPACK_BUFFER_BINDING=88ef DEPTH24_STENCIL8=88f0 VERTEX_ATTRIB_ARRAY_INTEGER=88fd VERTEX_ATTRIB_ARRAY_DIVISOR=88fe MAX_ARRAY_TEXTURE_LAYERS=88ff MIN_PROGRAM_TEXEL_OFFSET=8904 MAX_PROGRAM_TEXEL_OFFSET=8905 SAMPLER_BINDING=8919 UNIFORM_BUFFER=8a11 UNIFORM_BUFFER_BINDING=8a28 UNIFORM_BUFFER_START=8a29 UNIFORM_BUFFER_SIZE=8a2a MAX_VERTEX_UNIFORM_BLOCKS=8a2b MAX_FRAGMENT_UNIFORM_BLOCKS=8a2d MAX_COMBINED_UNIFORM_BLOCKS=8a2e MAX_UNIFORM_BUFFER_BINDINGS=8a2f MAX_UNIFORM_BLOCK_SIZE=8a30 MAX_COMBINED_VERTEX_UNIFORM_COMPONENTS=8a31 MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS=8a33 UNIFORM_BUFFER_OFFSET_ALIGNMENT=8a34 ACTIVE_UNIFORM_BLOCK_MAX_NAME_LENGTH=8a35 ACTIVE_UNIFORM_BLOCKS=8a36 UNIFORM_TYPE=8a37 UNIFORM_SIZE=8a38 UNIFORM_NAME_LENGTH=8a39 UNIFORM_BLOCK_INDEX=8a3a UNIFORM_OFFSET=8a3b UNIFORM_ARRAY_STRIDE=8a3c UNIFORM_MATRIX_STRIDE=8a3d UNIFORM_IS_ROW_MAJOR=8a3e UNIFORM_BLOCK_BINDING=8a3f UNIFORM_BLOCK_DATA_SIZE=8a40 UNIFORM_BLOCK_NAME_LENGTH=8a41 UNIFORM_BLOCK_ACTIVE_UNIFORMS=8a42 UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES=8a43 UNIFORM_BLOCK_REFERENCED_BY_VERTEX_SHADER=8a44 UNIFORM_BLOCK_REFERENCED_BY_FRAGMENT_SHADER=8a46 MAX_FRAGMENT_UNIFORM_COMPONENTS=8b49 MAX_VERTEX_UNIFORM_COMPONENTS=8b4a MAX_VARYING_COMPONENTS=8b4b SAMPLER_3D=8b5f SAMPLER_2D_SHADOW=8b62 FRAGMENT_SHADER_DERIVATIVE_HINT=8b8b UNSIGNED_NORMALIZED=8c17 TEXTURE_2D_ARRAY=8c1a TEXTURE_BINDING_2D_ARRAY=8c1d ANY_SAMPLES_PASSED=8c2f R11F_G11F_B10F=8c3a UNSIGNED_INT_10F_11F_11F_REV=8c3b RGB9_E5=8c3d UNSIGNED_INT_5_9_9_9_REV=8c3e SRGB=8c40 SRGB8=8c41 SRGB8_ALPHA8=8c43 TRANSFORM_FEEDBACK_VARYING_MAX_LENGTH=8c76 TRANSFORM_FEEDBACK_BUFFER_MODE=8c7f MAX_TRANSFORM_FEEDBACK_SEPARATE_COMPONENTS=8c80 TRANSFORM_FEEDBACK_VARYINGS=8c83 TRANSFORM_FEEDBACK_BUFFER_START=8c84 TRANSFORM_FEEDBACK_BUFFER_SIZE=8c85 TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN=8c88 RASTERIZER_DISCARD=8c89 MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS=8c8a MAX_TRANSFORM_FEEDBACK_SEPARATE_ATTRIBS=8c8b INTERLEAVED_ATTRIBS=8c8c SEPARATE_ATTRIBS=8c8d TRANSFORM_FEEDBACK_BUFFER=8c8e TRANSFORM_FEEDBACK_BUFFER_BINDING=8c8f DRAW_FRAMEBUFFER_BINDING=8ca6 READ_FRAMEBUFFER=8ca8 DRAW_FRAMEBUFFER=8ca9 READ_FRAMEBUFFER_BINDING=8caa RENDERBUFFER_SAMPLES=8cab DEPTH_COMPONENT32F=8cac DEPTH32F_STENCIL8=8cad FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER=8cd4 MAX_COLOR_ATTACHMENTS=8cdf COLOR_ATTACHMENT1=8ce1 COLOR_ATTACHMENT2=8ce2 COLOR_ATTACHMENT3=8ce3 COLOR_ATTACHMENT4=8ce4 COLOR_ATTACHMENT5=8ce5 COLOR_ATTACHMENT6=8ce6 COLOR_ATTACHMENT7=8ce7 COLOR_ATTACHMENT8=8ce8 COLOR_ATTACHMENT9=8ce9 COLOR_ATTACHMENT10=8cea COLOR_ATTACHMENT11=8ceb COLOR_ATTACHMENT12=8cec COLOR_ATTACHMENT13=8ced COLOR_ATTACHMENT14=8cee COLOR_ATTACHMENT15=8cef COLOR_ATTACHMENT16=8cf0 COLOR_ATTACHMENT17=8cf1 COLOR_ATTACHMENT18=8cf2 COLOR_ATTACHMENT19=8cf3 COLOR_ATTACHMENT20=8cf4 COLOR_ATTACHMENT21=8cf5 COLOR_ATTACHMENT22=8cf6 COLOR_ATTACHMENT23=8cf7 COLOR_ATTACHMENT24=8cf8 COLOR_ATTACHMENT25=8cf9 COLOR_ATTACHMENT26=8cfa COLOR_ATTACHMENT27=8cfb COLOR_ATTACHMENT28=8cfc COLOR_ATTACHMENT29=8cfd COLOR_ATTACHMENT30=8cfe COLOR_ATTACHMENT31=8cff FRAMEBUFFER_INCOMPLETE_MULTISAMPLE=8d56 MAX_SAMPLES=8d57 ANY_SAMPLES_PASSED_CONSERVATIVE=8d6a MAX_ELEMENT_INDEX=8d6b RGBA32UI=8d70 RGB32UI=8d71 RGBA16UI=8d76 RGB16UI=8d77 RGBA8UI=8d7c RGB8UI=8d7d RGBA32I=8d82 RGB32I=8d83 RGBA16I=8d88 RGB16I=8d89 RGBA8I=8d8e RGB8I=8d8f RED_INTEGER=8d94 RGB_INTEGER=8d98 RGBA_INTEGER=8d99 INT_2_10_10_10_REV=8d9f FLOAT_32_UNSIGNED_INT_24_8_REV=8dad SAMPLER_2D_ARRAY=8dc1 SAMPLER_2D_ARRAY_SHADOW=8dc4 SAMPLER_CUBE_SHADOW=8dc5 UNSIGNED_INT_VEC2=8dc6 UNSIGNED_INT_VEC3=8dc7 UNSIGNED_INT_VEC4=8dc8 INT_SAMPLER_2D=8dca INT_SAMPLER_3D=8dcb INT_SAMPLER_CUBE=8dcc INT_SAMPLER_2D_ARRAY=8dcf UNSIGNED_INT_SAMPLER_2D=8dd2 UNSIGNED_INT_SAMPLER_3D=8dd3 UNSIGNED_INT_SAMPLER_CUBE=8dd4 UNSIGNED_INT_SAMPLER_2D_ARRAY=8dd7 TRANSFORM_FEEDBACK=8e22 TRANSFORM_FEEDBACK_PAUSED=8e23 TRANSFORM_FEEDBACK_ACTIVE=8e24 TRANSFORM_FEEDBACK_BINDING=8e25 TEXTURE_SWIZZLE_R=8e42 TEXTURE_SWIZZLE_G=8e43 TEXTURE_SWIZZLE_B=8e44 TEXTURE_SWIZZLE_A=8e45 COPY_READ_BUFFER=8f36 COPY_READ_BUFFER_BINDING=8f36 COPY_WRITE_BUFFER=8f37 COPY_WRITE_BUFFER_BINDING=8f37 R8_SNORM=8f94 RG8_SNORM=8f95 RGB8_SNORM=8f96 RGBA8_SNORM=8f97 SIGNED_NORMALIZED=8f9c RGB10_A2UI=906f MAX_SERVER_WAIT_TIMEOUT=9111 OBJECT_TYPE=9112 SYNC_CONDITION=9113 SYNC_STATUS=9114 SYNC_FLAGS=9115 SYNC_FENCE=9116 SYNC_GPU_COMMANDS_COMPLETE=9117 UNSIGNALED=9118 SIGNALED=9119 ALREADY_SIGNALED=911a TIMEOUT_EXPIRED=911b CONDITION_SATISFIED=911c WAIT_FAILED=911d BUFFER_ACCESS_FLAGS=911f BUFFER_MAP_LENGTH=9120 BUFFER_MAP_OFFSET=9121 MAX_VERTEX_OUTPUT_COMPONENTS=9122 MAX_FRAGMENT_INPUT_COMPONENTS=9125 TEXTURE_IMMUTABLE_FORMAT=912f MAX_CLIENT_WAIT_TIMEOUT_WEBGL=9247 COMPRESSED_R11_EAC=9270 COMPRESSED_SIGNED_R11_EAC=9271 COMPRESSED_RG11_EAC=9272 COMPRESSED_SIGNED_RG11_EAC=9273 COMPRESSED_RGB8_ETC2=9274 COMPRESSED_SRGB8_ETC2=9275 COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2=9276 COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2=9277 COMPRESSED_RGBA8_ETC2_EAC=9278 COMPRESSED_SRGB8_ALPHA8_ETC2_EAC=9279 NUM_SAMPLE_COUNTS=9380 INVALID_INDEX=ffffffff';
const glTable = s => { const o = {}; for (const kv of s.split(' ')) { const i = kv.indexOf('='); o[kv.slice(0, i)] = parseInt(kv.slice(i + 1), 16); } return o; };
const GLI = Symbol('webgl');
const glIllegal = () => { throw new TypeError('Illegal constructor'); };
class WebGLObject { constructor(t) { if (t !== GLI) glIllegal(); } }
/* the GL object wrappers: _ is {c: context, n: GL name, k: N.gl.gen kind, d: deleted} */
class WebGLBuffer extends WebGLObject {}
class WebGLFramebuffer extends WebGLObject {}
class WebGLRenderbuffer extends WebGLObject {}
class WebGLTexture extends WebGLObject {}
class WebGLProgram extends WebGLObject {}
class WebGLShader extends WebGLObject {}
class WebGLVertexArrayObject extends WebGLObject {}
class WebGLVertexArrayObjectOES extends WebGLObject {}
class WebGLQuery extends WebGLObject {}
class WebGLSampler extends WebGLObject {}
class WebGLSync extends WebGLObject {}
class WebGLTransformFeedback extends WebGLObject {}
const GL_KINDS = [WebGLBuffer, WebGLFramebuffer, WebGLRenderbuffer, WebGLTexture, WebGLProgram, WebGLShader, WebGLVertexArrayObject, WebGLQuery, WebGLSampler, WebGLTransformFeedback];
class WebGLUniformLocation { constructor(t, p, l) { if (t !== GLI) glIllegal(); def(this, '_p', p); def(this, '_l', l); } }
class WebGLActiveInfo {
    constructor(t, a) { if (t !== GLI) glIllegal(); def(this, '_', a); }
    get size() { return this._[0]; } get type() { return this._[1]; } get name() { return this._[2]; }
}
class WebGLShaderPrecisionFormat {
    constructor(t, a) { if (t !== GLI) glIllegal(); def(this, '_', a); }
    get rangeMin() { return this._[0]; } get rangeMax() { return this._[1]; } get precision() { return this._[2]; }
}
class WebGLContextEvent extends Event {
    constructor(type, init) { super(type, init); def(this, '_msg', init && init.statusMessage !== undefined ? String(init.statusMessage) : ''); }
    get statusMessage() { return this._msg; }
}
const glVao = o => ({ o, a: [], bad: 0, eab: null });   /* attribute arrays: a[i] = {on, buf}; bad counts enabled ones without a buffer */
const glF32 = v => v instanceof Float32Array ? v : new Float32Array(v);
const glI32 = v => v instanceof Int32Array ? v : new Int32Array(v);
const glU32 = v => v instanceof Uint32Array ? v : new Uint32Array(v);
const glIsView = v => ArrayBuffer.isView(v);
/* pages often create contexts only to probe for support: close the native context once its canvas is gone */
const glReg = typeof FinalizationRegistry === 'function' ? new FinalizationRegistry(id => N.gl.close(id)) : null;
/* binding queries the native side answers with a GL name: which object kind that name is */
const GL_BIND_KIND = { 0x8CA7: 2, 0x8069: 3, 0x8514: 3, 0x806A: 3, 0x8C1D: 3, 0x8F36: 0, 0x8F37: 0, 0x88ED: 0, 0x88EF: 0, 0x8C8F: 0, 0x8A28: 0, 0x8E25: 9, 0x8919: 8 };
const GL_ASTC = ['4x4', '5x4', '5x5', '6x5', '6x6', '8x5', '8x6', '8x8', '10x5', '10x6', '10x8', '10x10', '12x10', '12x12'];
const glExtConsts = (pairs) => { const o = {}; for (const [k, v] of pairs) o[k] = v; return o; };
/* extensions: [webgl versions bit 1 | 2, the driver extension needed or null, constants, methods(context) | null, compressed formats] */
const GL_EXTS = {
    ANGLE_instanced_arrays: [1, null, { VERTEX_ATTRIB_ARRAY_DIVISOR_ANGLE: 0x88FE }, g => ({
        drawArraysInstancedANGLE(m, f, c, n) { GL2M.drawArraysInstanced.call(g, m, f, c, n); },
        drawElementsInstancedANGLE(m, c, t, o, n) { GL2M.drawElementsInstanced.call(g, m, c, t, o, n); },
        vertexAttribDivisorANGLE(i, d) { GL2M.vertexAttribDivisor.call(g, i, d); } })],
    EXT_blend_minmax: [1, null, { MIN_EXT: 0x8007, MAX_EXT: 0x8008 }],
    EXT_color_buffer_float: [2, 'GL_EXT_color_buffer_float', {}],
    EXT_color_buffer_half_float: [3, ['GL_EXT_color_buffer_half_float', 'GL_EXT_color_buffer_float'], { RGBA16F_EXT: 0x881A, RGB16F_EXT: 0x881B, FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE_EXT: 0x8211, UNSIGNED_NORMALIZED_EXT: 0x8C17 }],
    EXT_float_blend: [3, 'GL_EXT_float_blend', {}],
    EXT_frag_depth: [1, 'GL_EXT_frag_depth', {}],
    EXT_shader_texture_lod: [1, 'GL_EXT_shader_texture_lod', {}],
    EXT_sRGB: [1, null, { SRGB_EXT: 0x8C40, SRGB_ALPHA_EXT: 0x8C42, SRGB8_ALPHA8_EXT: 0x8C43, FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING_EXT: 0x8210 }],
    EXT_texture_compression_bptc: [3, 'GL_EXT_texture_compression_bptc', { COMPRESSED_RGBA_BPTC_UNORM_EXT: 0x8E8C, COMPRESSED_SRGB_ALPHA_BPTC_UNORM_EXT: 0x8E8D, COMPRESSED_RGB_BPTC_SIGNED_FLOAT_EXT: 0x8E8E, COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT_EXT: 0x8E8F }, null, [0x8E8C, 0x8E8D, 0x8E8E, 0x8E8F]],
    EXT_texture_compression_rgtc: [3, 'GL_EXT_texture_compression_rgtc', { COMPRESSED_RED_RGTC1_EXT: 0x8DBB, COMPRESSED_SIGNED_RED_RGTC1_EXT: 0x8DBC, COMPRESSED_RED_GREEN_RGTC2_EXT: 0x8DBD, COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT: 0x8DBE }, null, [0x8DBB, 0x8DBC, 0x8DBD, 0x8DBE]],
    EXT_texture_filter_anisotropic: [3, 'GL_EXT_texture_filter_anisotropic', { TEXTURE_MAX_ANISOTROPY_EXT: 0x84FE, MAX_TEXTURE_MAX_ANISOTROPY_EXT: 0x84FF }],
    EXT_texture_norm16: [2, 'GL_EXT_texture_norm16', { R16_EXT: 0x822A, RG16_EXT: 0x822C, RGB16_EXT: 0x8054, RGBA16_EXT: 0x805B, R16_SNORM_EXT: 0x8F98, RG16_SNORM_EXT: 0x8F99, RGB16_SNORM_EXT: 0x8F9A, RGBA16_SNORM_EXT: 0x8F9B }],
    KHR_parallel_shader_compile: [3, null, { COMPLETION_STATUS_KHR: 0x91B1 }],
    OES_element_index_uint: [1, null, {}],
    OES_fbo_render_mipmap: [1, null, {}],
    OES_standard_derivatives: [1, null, { FRAGMENT_SHADER_DERIVATIVE_HINT_OES: 0x8B8B }],
    OES_texture_float: [1, null, {}],
    OES_texture_float_linear: [3, 'GL_OES_texture_float_linear', {}],
    OES_texture_half_float: [1, null, { HALF_FLOAT_OES: 0x8D61 }],
    OES_texture_half_float_linear: [1, null, {}],
    OES_vertex_array_object: [1, null, { VERTEX_ARRAY_BINDING_OES: 0x85B5 }, g => ({
        createVertexArrayOES() { return g._gen(6, WebGLVertexArrayObjectOES); },
        deleteVertexArrayOES(v) { GL2M.deleteVertexArray.call(g, v); },
        isVertexArrayOES(v) { return GL2M.isVertexArray.call(g, v); },
        bindVertexArrayOES(v) { GL2M.bindVertexArray.call(g, v); } })],
    WEBGL_color_buffer_float: [1, 'GL_EXT_color_buffer_float', { RGBA32F_EXT: 0x8814, FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE_EXT: 0x8211, UNSIGNED_NORMALIZED_EXT: 0x8C17 }],
    WEBGL_compressed_texture_astc: [3, 'GL_KHR_texture_compression_astc_ldr', glExtConsts(GL_ASTC.flatMap((b, i) => [['COMPRESSED_RGBA_ASTC_' + b + '_KHR', 0x93B0 + i], ['COMPRESSED_SRGB8_ALPHA8_ASTC_' + b + '_KHR', 0x93D0 + i]])),
        () => ({ getSupportedProfiles() { return ['ldr']; } }), GL_ASTC.flatMap((b, i) => [0x93B0 + i, 0x93D0 + i])],
    WEBGL_compressed_texture_etc: [3, null, { COMPRESSED_R11_EAC: 0x9270, COMPRESSED_SIGNED_R11_EAC: 0x9271, COMPRESSED_RG11_EAC: 0x9272, COMPRESSED_SIGNED_RG11_EAC: 0x9273, COMPRESSED_RGB8_ETC2: 0x9274, COMPRESSED_SRGB8_ETC2: 0x9275,
        COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2: 0x9276, COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2: 0x9277, COMPRESSED_RGBA8_ETC2_EAC: 0x9278, COMPRESSED_SRGB8_ALPHA8_ETC2_EAC: 0x9279 }, null, [0x9270, 0x9271, 0x9272, 0x9273, 0x9274, 0x9275, 0x9276, 0x9277, 0x9278, 0x9279]],
    WEBGL_compressed_texture_etc1: [3, 'GL_OES_compressed_ETC1_RGB8_texture', { COMPRESSED_RGB_ETC1_WEBGL: 0x8D64 }, null, [0x8D64]],
    WEBGL_compressed_texture_s3tc: [3, 'GL_EXT_texture_compression_s3tc', { COMPRESSED_RGB_S3TC_DXT1_EXT: 0x83F0, COMPRESSED_RGBA_S3TC_DXT1_EXT: 0x83F1, COMPRESSED_RGBA_S3TC_DXT3_EXT: 0x83F2, COMPRESSED_RGBA_S3TC_DXT5_EXT: 0x83F3 }, null, [0x83F0, 0x83F1, 0x83F2, 0x83F3]],
    WEBGL_debug_renderer_info: [3, null, { UNMASKED_VENDOR_WEBGL: 0x9245, UNMASKED_RENDERER_WEBGL: 0x9246 }],
    WEBGL_depth_texture: [1, null, { UNSIGNED_INT_24_8_WEBGL: 0x84FA }],
    WEBGL_draw_buffers: [1, 'GL_EXT_draw_buffers', glExtConsts([...Array(16).keys()].flatMap(i => [['COLOR_ATTACHMENT' + i + '_WEBGL', 0x8CE0 + i], ['DRAW_BUFFER' + i + '_WEBGL', 0x8825 + i]]).concat([['MAX_COLOR_ATTACHMENTS_WEBGL', 0x8CDF], ['MAX_DRAW_BUFFERS_WEBGL', 0x8824]])),
        g => ({ drawBuffersWEBGL(b) { N.gl.drawBuffers(g._.id, [...b]); } })],
    WEBGL_lose_context: [3, null, {}, g => ({ loseContext() { g._lose(true); }, restoreContext() { g._restore(); } })],
    WEBGL_multi_draw: [3, null, {}, g => ({
        multiDrawArraysWEBGL(m, f, fo, c, co, n) { for (let i = 0; i < n; i++) g.drawArrays(m, f[fo + i], c[co + i]); },
        multiDrawElementsWEBGL(m, c, co, t, o, oo, n) { for (let i = 0; i < n; i++) g.drawElements(m, c[co + i], t, o[oo + i]); },
        multiDrawArraysInstancedWEBGL(m, f, fo, c, co, ic, ico, n) { for (let i = 0; i < n; i++) GL2M.drawArraysInstanced.call(g, m, f[fo + i], c[co + i], ic[ico + i]); },
        multiDrawElementsInstancedWEBGL(m, c, co, t, o, oo, ic, ico, n) { for (let i = 0; i < n; i++) GL2M.drawElementsInstanced.call(g, m, c[co + i], t, o[oo + i], ic[ico + i]); } })],
};
const glDriverHas = (s, need) => need === null || (Array.isArray(need) ? need.some(n => s.dexts.has(n)) : s.dexts.has(need));

/* methods of both versions; `this._` is the context state */
const GL1M = {
    get canvas() { return this._.cv; },
    get drawingBufferWidth() { return this._.w; },
    get drawingBufferHeight() { return this._.h; },
    get drawingBufferColorSpace() { return 'srgb'; }, set drawingBufferColorSpace(v) {},
    get unpackColorSpace() { return 'srgb'; }, set unpackColorSpace(v) {},
    getContextAttributes() { return this._.lost ? null : Object.assign({}, this._.attrs); },
    isContextLost() { return this._.lost; },
    makeXRCompatible() { return Promise.resolve(); },
    getSupportedExtensions() {
        const s = this._;
        if (s.lost) return null;
        return Object.keys(GL_EXTS).filter(k => GL_EXTS[k][0] & s.v && glDriverHas(s, GL_EXTS[k][1]));
    },
    getExtension(name) {
        const s = this._;
        if (s.lost) return null;
        name = String(name);
        const key = Object.keys(GL_EXTS).find(k => k.toLowerCase() === name.toLowerCase());
        if (!key) return null;
        if (s.exts[key]) return s.exts[key];
        const e = GL_EXTS[key];
        if (!(e[0] & s.v) || !glDriverHas(s, e[1])) return null;
        const o = Object.assign(Object.create(null), e[2], e[3] ? e[3](this) : null);
        Object.defineProperty(o, Symbol.toStringTag, { value: key });
        if (e[4]) s.ctf.push(...e[4]);
        return (s.exts[key] = o);
    },
    _err(code) { const s = this._; if (s.errs.length < 16 && !s.errs.includes(code)) s.errs.push(code); },
    /* the GL name of an object argument: 0 for null, -1 (after INVALID_OPERATION) for another context's or a deleted one */
    _nm(o, C) {
        if (o === null || o === undefined) return 0;
        if (!(o instanceof C)) throw new TypeError(`parameter is not of type '${C.name}'`);
        const x = o._;
        if (x.c !== this || x.d) { this._err(0x502); return -1; }
        return x.n;
    },
    _gen(k, C, arg) {
        const s = this._;
        if (s.lost) return null;
        const n = N.gl.gen(s.id, k, arg);
        if (!n) return null;
        const o = Object.create(C.prototype);
        def(o, '_', { c: this, n, k, d: false });
        if (k === 6) o._.vs = glVao(o);
        s.m[k].set(n, o);
        return o;
    },
    _del(o, C) {
        const n = this._nm(o, C);
        if (n <= 0) return false;
        const s = this._;
        N.gl.del(s.id, o._.k, n);
        o._.d = true;
        s.m[o._.k].delete(n);
        return true;
    },
    _is(o, C) { return o instanceof C && o._.c === this && !o._.d && !this._.lost; },
    _byName(k, n) { return n ? this._.m[k].get(n) || null : null; },
    /* drew to the default framebuffer: present it once the current task's microtasks ran */
    _drew() {
        const s = this._;
        if (s.dfb === null && !s.dirty) { s.dirty = true; queueMicrotask(s.flush); }
    },
    _canDraw() { if (this._.vao.bad) { this._err(0x502); return false; } return true; },
    _resize(w, h) {
        const s = this._;
        if (s.lost) return;
        N.gl.resize(s.id, w, h);
        const z = N.gl.size(s.id);
        if (z) { s.w = z[0]; s.h = z[1]; }
        s.dirty = false; this._drew();
    },
    _lose(byPage) {
        const s = this._;
        if (s.lost) return;
        s.lost = true; s.restorable = false;
        if (glReg) glReg.unregister(this);
        N.gl.close(s.id); s.id = -1;
        for (const m of s.m) { for (const o of m.values()) o._.d = true; m.clear(); }
        s.errs = [0x9242];
        const ev = new WebGLContextEvent('webglcontextlost', { cancelable: true, statusMessage: '' });
        queueMicrotask(() => { s.cv.dispatchEvent(ev); if (ev.defaultPrevented && byPage) s.restorable = true; });
    },
    _restore() {
        const s = this._;
        if (!s.lost) { this._err(0x502); return; }
        setTimeout(() => {
            if (!s.lost) return;
            const id = N.gl.create(s.cv, s.v, s.bits, s.cv.width, s.cv.height);
            if (id < 0) return;
            glInit(this, id);
            s.cv.dispatchEvent(new WebGLContextEvent('webglcontextrestored', { statusMessage: '' }));
        });
    },

    getError() {
        const s = this._;
        if (s.errs.length) return s.errs.shift();
        return s.lost ? 0 : N.gl.getError(s.id);
    },
    activeTexture(t) { N.gl.activeTexture(this._.id, t); },
    attachShader(p, sh) { const a = this._nm(p, WebGLProgram), b = this._nm(sh, WebGLShader); if (a > 0 && b > 0) N.gl.attachShader(this._.id, a, b); else if (!a || !b) this._err(0x501); },
    detachShader(p, sh) { const a = this._nm(p, WebGLProgram), b = this._nm(sh, WebGLShader); if (a > 0 && b > 0) N.gl.detachShader(this._.id, a, b); else if (!a || !b) this._err(0x501); },
    bindAttribLocation(p, i, name) { const n = this._nm(p, WebGLProgram); if (n > 0) N.gl.bindAttribLocation(this._.id, n, i, String(name)); },
    bindBuffer(t, b) {
        const n = this._nm(b, WebGLBuffer);
        if (n < 0) return;
        const s = this._;
        N.gl.bindBuffer(s.id, t, n);
        if (t === 0x8892) s.abuf = b || null;
        else if (t === 0x8893) s.vao.eab = b || null;
    },
    bindFramebuffer(t, f) {
        const n = this._nm(f, WebGLFramebuffer);
        if (n < 0) return;
        const s = this._;
        N.gl.bindFramebuffer(s.id, t, n);
        if (t === 0x8D40 || t === 0x8CA9) s.dfb = f || null;
        if (t === 0x8D40 || t === 0x8CA8) s.rfb = f || null;
    },
    bindRenderbuffer(t, r) { const n = this._nm(r, WebGLRenderbuffer); if (n >= 0) N.gl.bindRenderbuffer(this._.id, t, n); },
    bindTexture(t, x) { const n = this._nm(x, WebGLTexture); if (n >= 0) N.gl.bindTexture(this._.id, t, n); },
    blendColor(r, g, b, a) { N.gl.blendColor(this._.id, r, g, b, a); },
    blendEquation(m) { N.gl.blendEquation(this._.id, m); },
    blendEquationSeparate(a, b) { N.gl.blendEquationSeparate(this._.id, a, b); },
    blendFunc(a, b) { N.gl.blendFunc(this._.id, a, b); },
    blendFuncSeparate(a, b, c, d) { N.gl.blendFuncSeparate(this._.id, a, b, c, d); },
    checkFramebufferStatus(t) { return this._.lost ? 0 : N.gl.checkFramebufferStatus(this._.id, t); },
    clear(m) { N.gl.clear(this._.id, m); this._drew(); },
    clearColor(r, g, b, a) { N.gl.clearColor(this._.id, r, g, b, a); },
    clearDepth(d) { N.gl.clearDepth(this._.id, d); },
    clearStencil(v) { N.gl.clearStencil(this._.id, v); },
    colorMask(r, g, b, a) { N.gl.colorMask(this._.id, r, g, b, a); },
    compileShader(sh) { const n = this._nm(sh, WebGLShader); if (n > 0) N.gl.compileShader(this._.id, n); },
    copyTexImage2D(t, l, f, x, y, w, h, b) { N.gl.copyTexImage2D(this._.id, t, l, f, x, y, w, h, b); },
    copyTexSubImage2D(t, l, xo, yo, x, y, w, h) { N.gl.copyTexSubImage2D(this._.id, t, l, xo, yo, x, y, w, h); },
    createBuffer() { return this._gen(0, WebGLBuffer); },
    createFramebuffer() { return this._gen(1, WebGLFramebuffer); },
    createProgram() { return this._gen(4, WebGLProgram); },
    createRenderbuffer() { return this._gen(2, WebGLRenderbuffer); },
    createShader(t) {
        if (t !== 0x8B31 && t !== 0x8B30) { this._err(0x500); return null; }
        const o = this._gen(5, WebGLShader, t);
        if (o) o._.t = t;
        return o;
    },
    createTexture() { return this._gen(3, WebGLTexture); },
    cullFace(m) { N.gl.cullFace(this._.id, m); },
    deleteBuffer(b) {
        if (!this._del(b, WebGLBuffer)) return;
        const s = this._, v = s.vao;   /* GL unbinds it from the current bindings and vertex array */
        if (s.abuf === b) s.abuf = null;
        if (v.eab === b) v.eab = null;
        for (const a of v.a) if (a && a.buf === b) { a.buf = null; if (a.on) v.bad++; }
    },
    deleteFramebuffer(f) {
        const s = this._, d = s.dfb === f, r = s.rfb === f;
        if (!this._del(f, WebGLFramebuffer)) return;
        /* deleting the bound framebuffer binds 0, the window's; the page's default is an FBO of its own */
        if (d && r) this.bindFramebuffer(0x8D40, null);
        else if (d) this.bindFramebuffer(0x8CA9, null);
        else if (r) this.bindFramebuffer(0x8CA8, null);
    },
    deleteProgram(p) { this._del(p, WebGLProgram); },
    deleteRenderbuffer(r) { this._del(r, WebGLRenderbuffer); },
    deleteShader(sh) { this._del(sh, WebGLShader); },
    deleteTexture(t) { this._del(t, WebGLTexture); },
    depthFunc(f) { N.gl.depthFunc(this._.id, f); },
    depthMask(f) { N.gl.depthMask(this._.id, f); },
    depthRange(a, b) { N.gl.depthRange(this._.id, a, b); },
    disable(c) { N.gl.disable(this._.id, c); },
    enable(c) { N.gl.enable(this._.id, c); },
    disableVertexAttribArray(i) {
        const s = this._, v = s.vao;
        N.gl.disableVertexAttribArray(s.id, i);
        const a = i < s.maxAttr ? v.a[i] : null;
        if (a && a.on) { a.on = false; if (!a.buf) v.bad--; }
    },
    enableVertexAttribArray(i) {
        const s = this._, v = s.vao;
        N.gl.enableVertexAttribArray(s.id, i);
        if (!(i >= 0 && i < s.maxAttr)) return;
        const a = v.a[i] || (v.a[i] = { on: false, buf: null });
        if (!a.on) { a.on = true; if (!a.buf) v.bad++; }
    },
    drawArrays(m, f, c) { if (this._canDraw()) { N.gl.drawArrays(this._.id, m, f, c); this._drew(); } },
    drawElements(m, c, t, o) {
        if (!this._canDraw()) return;
        if (!this._.vao.eab) { this._err(0x502); return; }   /* no index buffer: GL would read client memory */
        N.gl.drawElements(this._.id, m, c, t, o); this._drew();
    },
    finish() { N.gl.finish(this._.id); },
    flush() { N.gl.flush(this._.id); },
    framebufferRenderbuffer(t, a, rt, r) { const n = this._nm(r, WebGLRenderbuffer); if (n >= 0) N.gl.framebufferRenderbuffer(this._.id, t, a, rt, n); },
    framebufferTexture2D(t, a, tt, x, l) { const n = this._nm(x, WebGLTexture); if (n >= 0) N.gl.framebufferTexture2D(this._.id, t, a, tt, n, l); },
    frontFace(m) { N.gl.frontFace(this._.id, m); },
    generateMipmap(t) { N.gl.generateMipmap(this._.id, t); },
    getActiveAttrib(p, i) { const n = this._nm(p, WebGLProgram); const r = n > 0 ? N.gl.getActive(this._.id, n, i, 0) : null; return r ? new WebGLActiveInfo(GLI, r) : null; },
    getActiveUniform(p, i) { const n = this._nm(p, WebGLProgram); const r = n > 0 ? N.gl.getActive(this._.id, n, i, 1) : null; return r ? new WebGLActiveInfo(GLI, r) : null; },
    getAttachedShaders(p) { const n = this._nm(p, WebGLProgram); return n > 0 ? N.gl.getAttachedShaders(this._.id, n).map(x => this._byName(5, x)).filter(Boolean) : null; },
    getAttribLocation(p, name) { const n = this._nm(p, WebGLProgram); return n > 0 ? N.gl.getAttribLocation(this._.id, n, String(name)) : -1; },
    getBufferParameter(t, p) { return this._.lost ? null : N.gl.getBufferParameter(this._.id, t, p); },
    getParameter(p) {
        const s = this._;
        if (s.lost) return null;
        switch (p) {
        case 0x1F00: return 'WebKit';
        case 0x1F01: return 'WebKit WebGL';
        case 0x1F02: return s.v === 2 ? 'WebGL 2.0 (OpenGL ES 3.0 Lumen)' : 'WebGL 1.0 (OpenGL ES 2.0 Lumen)';
        case 0x8B8C: return s.v === 2 ? 'WebGL GLSL ES 3.00 (OpenGL ES GLSL ES 3.0 Lumen)' : 'WebGL GLSL ES 1.0 (OpenGL ES GLSL ES 1.0 Lumen)';
        case 0x9245: if (s.exts.WEBGL_debug_renderer_info) return 'Lumen'; break;
        case 0x9246: if (s.exts.WEBGL_debug_renderer_info) return N.gl.renderer(); break;
        case 0x9240: return s.flip;
        case 0x9241: return s.premul;
        case 0x9243: return s.conv;
        case 0x86A3: return new Uint32Array(s.ctf);
        case 0x86A2: return s.ctf.length;
        case 0x8894: return s.abuf;
        case 0x8895: return s.vao.eab;
        case 0x85B5: return s.vao === s.vao0 ? null : s.vao.o;
        case 0x8B8D: return s.prog;
        case 0x8CA6: return s.dfb;
        case 0x8CAA: return s.rfb;
        case 0x9247: return 0;
        }
        const k = GL_BIND_KIND[p];
        const r = N.gl.getParameter(s.id, p);
        return k !== undefined ? this._byName(k, r) : r;
    },
    getFramebufferAttachmentParameter(t, a, p) {
        const s = this._;
        if (s.lost) return null;
        if (p === 0x8CD1) {   /* the attached object */
            const ty = N.gl.getFramebufferAttachmentParameter(s.id, t, a, 0x8CD0);
            const n = N.gl.getFramebufferAttachmentParameter(s.id, t, a, p);
            return ty === 0x1702 ? this._byName(3, n) : ty === 0x8D41 ? this._byName(2, n) : null;
        }
        return N.gl.getFramebufferAttachmentParameter(s.id, t, a, p);
    },
    getProgramParameter(p, q) {
        const n = this._nm(p, WebGLProgram);
        if (n <= 0) return null;
        if (q === 0x91B1) return true;
        const r = N.gl.getProgramParameter(this._.id, n, q);
        return q === 0x8B80 || q === 0x8B82 || q === 0x8B83 ? !!r : r;
    },
    getProgramInfoLog(p) { const n = this._nm(p, WebGLProgram); return n > 0 ? N.gl.getProgramInfoLog(this._.id, n) : null; },
    getRenderbufferParameter(t, p) { return this._.lost ? null : N.gl.getRenderbufferParameter(this._.id, t, p); },
    getShaderParameter(sh, q) {
        const n = this._nm(sh, WebGLShader);
        if (n <= 0) return null;
        if (q === 0x91B1) return true;
        const r = N.gl.getShaderParameter(this._.id, n, q);
        return q === 0x8B80 || q === 0x8B81 ? !!r : r;
    },
    getShaderPrecisionFormat(st, pt) { return this._.lost ? null : new WebGLShaderPrecisionFormat(GLI, N.gl.getShaderPrecisionFormat(this._.id, st, pt)); },
    getShaderInfoLog(sh) { const n = this._nm(sh, WebGLShader); return n > 0 ? N.gl.getShaderInfoLog(this._.id, n) : null; },
    getShaderSource(sh) { const n = this._nm(sh, WebGLShader); return n > 0 ? sh._.src ?? '' : null; },
    getTexParameter(t, p) { if (this._.lost) return null; const r = N.gl.getTexParameter(this._.id, t, p); return p === 0x912F ? !!r : r; },
    getUniform(p, l) {
        const n = this._nm(p, WebGLProgram);
        if (n <= 0 || !(l instanceof WebGLUniformLocation) || l._p !== p) { if (n > 0) this._err(0x502); return null; }
        return N.gl.getUniform(this._.id, n, l._l);
    },
    getUniformLocation(p, name) {
        const n = this._nm(p, WebGLProgram);
        if (n <= 0) return null;
        const l = N.gl.getUniformLocation(this._.id, n, String(name));
        return l < 0 ? null : new WebGLUniformLocation(GLI, p, l);
    },
    getVertexAttrib(i, p) {
        const s = this._;
        if (s.lost) return null;
        if (p === 0x889F) { const a = s.vao.a[i]; return a ? a.buf : null; }
        const r = N.gl.getVertexAttrib(s.id, i, p);
        return p === 0x8622 || p === 0x886A || p === 0x88FD ? !!r : r;
    },
    getVertexAttribOffset(i, p) { return this._.lost ? 0 : N.gl.getVertexAttribOffset(this._.id, i, p); },
    hint(t, m) { N.gl.hint(this._.id, t, m); },
    isBuffer(b) { return this._is(b, WebGLBuffer); },
    isEnabled(c) { return this._.lost ? false : N.gl.isEnabled(this._.id, c); },
    isFramebuffer(f) { return this._is(f, WebGLFramebuffer); },
    isProgram(p) { return this._is(p, WebGLProgram); },
    isRenderbuffer(r) { return this._is(r, WebGLRenderbuffer); },
    isShader(s) { return this._is(s, WebGLShader); },
    isTexture(t) { return this._is(t, WebGLTexture); },
    lineWidth(w) { N.gl.lineWidth(this._.id, w); },
    linkProgram(p) { const n = this._nm(p, WebGLProgram); if (n > 0) N.gl.linkProgram(this._.id, n); },
    pixelStorei(p, v) {
        const s = this._;
        if (p === 0x9240) s.flip = !!v;
        else if (p === 0x9241) s.premul = !!v;
        else if (p === 0x9243) s.conv = v;
        else N.gl.pixelStorei(s.id, p, v);
    },
    polygonOffset(f, u) { N.gl.polygonOffset(this._.id, f, u); },
    renderbufferStorage(t, f, w, h) { N.gl.renderbufferStorage(this._.id, t, f, w, h); },
    sampleCoverage(v, i) { N.gl.sampleCoverage(this._.id, v, i); },
    scissor(x, y, w, h) { N.gl.scissor(this._.id, x, y, w, h); },
    shaderSource(sh, src) { const n = this._nm(sh, WebGLShader); if (n > 0) { sh._.src = String(src); N.gl.shaderSource(this._.id, n, sh._.src); } },
    stencilFunc(f, r, m) { N.gl.stencilFunc(this._.id, f, r, m); },
    stencilFuncSeparate(fc, f, r, m) { N.gl.stencilFuncSeparate(this._.id, fc, f, r, m); },
    stencilMask(m) { N.gl.stencilMask(this._.id, m); },
    stencilMaskSeparate(f, m) { N.gl.stencilMaskSeparate(this._.id, f, m); },
    stencilOp(a, b, c) { N.gl.stencilOp(this._.id, a, b, c); },
    stencilOpSeparate(f, a, b, c) { N.gl.stencilOpSeparate(this._.id, f, a, b, c); },
    texParameterf(t, p, v) { N.gl.texParameterf(this._.id, t, p, v); },
    texParameteri(t, p, v) { N.gl.texParameteri(this._.id, t, p, v); },
    /* the location's GL value, or null to skip the call: from another program than the current one is INVALID_OPERATION */
    _loc(l) {
        if (l === null || l === undefined) return null;
        if (!(l instanceof WebGLUniformLocation)) throw new TypeError("parameter 1 is not of type 'WebGLUniformLocation'");
        if (l._p !== this._.prog) { this._err(0x502); return null; }
        return l._l;
    },
    uniform1f(l, x) { const k = this._loc(l); if (k !== null) N.gl.uniform1f(this._.id, k, x); },
    uniform2f(l, x, y) { const k = this._loc(l); if (k !== null) N.gl.uniform2f(this._.id, k, x, y); },
    uniform3f(l, x, y, z) { const k = this._loc(l); if (k !== null) N.gl.uniform3f(this._.id, k, x, y, z); },
    uniform4f(l, x, y, z, w) { const k = this._loc(l); if (k !== null) N.gl.uniform4f(this._.id, k, x, y, z, w); },
    uniform1i(l, x) { const k = this._loc(l); if (k !== null) N.gl.uniform1i(this._.id, k, x); },
    uniform2i(l, x, y) { const k = this._loc(l); if (k !== null) N.gl.uniform2i(this._.id, k, x, y); },
    uniform3i(l, x, y, z) { const k = this._loc(l); if (k !== null) N.gl.uniform3i(this._.id, k, x, y, z); },
    uniform4i(l, x, y, z, w) { const k = this._loc(l); if (k !== null) N.gl.uniform4i(this._.id, k, x, y, z, w); },
    uniform1fv(l, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformv(this._.id, 0, k, glF32(v), o, n); },
    uniform2fv(l, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformv(this._.id, 1, k, glF32(v), o, n); },
    uniform3fv(l, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformv(this._.id, 2, k, glF32(v), o, n); },
    uniform4fv(l, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformv(this._.id, 3, k, glF32(v), o, n); },
    uniform1iv(l, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformv(this._.id, 4, k, glI32(v), o, n); },
    uniform2iv(l, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformv(this._.id, 5, k, glI32(v), o, n); },
    uniform3iv(l, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformv(this._.id, 6, k, glI32(v), o, n); },
    uniform4iv(l, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformv(this._.id, 7, k, glI32(v), o, n); },
    uniformMatrix2fv(l, t, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformMatrix(this._.id, 0, k, t, glF32(v), o, n); },
    uniformMatrix3fv(l, t, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformMatrix(this._.id, 1, k, t, glF32(v), o, n); },
    uniformMatrix4fv(l, t, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformMatrix(this._.id, 2, k, t, glF32(v), o, n); },
    useProgram(p) { const n = this._nm(p, WebGLProgram); if (n >= 0) { N.gl.useProgram(this._.id, n); this._.prog = p || null; } },
    validateProgram(p) { const n = this._nm(p, WebGLProgram); if (n > 0) N.gl.validateProgram(this._.id, n); },
    vertexAttrib1f(i, x) { N.gl.vertexAttrib4f(this._.id, i, x, 0, 0, 1); },
    vertexAttrib2f(i, x, y) { N.gl.vertexAttrib4f(this._.id, i, x, y, 0, 1); },
    vertexAttrib3f(i, x, y, z) { N.gl.vertexAttrib4f(this._.id, i, x, y, z, 1); },
    vertexAttrib4f(i, x, y, z, w) { N.gl.vertexAttrib4f(this._.id, i, x, y, z, w); },
    vertexAttrib1fv(i, v) { N.gl.vertexAttrib4f(this._.id, i, v[0], 0, 0, 1); },
    vertexAttrib2fv(i, v) { N.gl.vertexAttrib4f(this._.id, i, v[0], v[1], 0, 1); },
    vertexAttrib3fv(i, v) { N.gl.vertexAttrib4f(this._.id, i, v[0], v[1], v[2], 1); },
    vertexAttrib4fv(i, v) { N.gl.vertexAttrib4f(this._.id, i, v[0], v[1], v[2], v[3]); },
    _attribPointer(i, call) {
        const s = this._, v = s.vao;
        if (!s.abuf) { this._err(0x502); return; }   /* no buffer: GL would take the offset as a client pointer */
        if (!(i >= 0 && i < s.maxAttr)) { this._err(0x501); return; }
        call();
        const a = v.a[i] || (v.a[i] = { on: false, buf: null });
        if (a.on && !a.buf) v.bad--;
        a.buf = s.abuf;
    },
    vertexAttribPointer(i, size, t, norm, stride, off) { this._attribPointer(i, () => N.gl.vertexAttribPointer(this._.id, i, size, t, norm, stride, off)); },
    viewport(x, y, w, h) { N.gl.viewport(this._.id, x, y, w, h); },

    bufferData(t, d, u, o, n) {
        const s = this._;
        if (typeof d === 'number') { if (d < 0) { this._err(0x501); return; } N.gl.bufferData(s.id, t, d, u); return; }
        if (d === null || d === undefined) { this._err(0x501); return; }
        N.gl.bufferData(s.id, t, d, u, o || 0, n || 0);
    },
    bufferSubData(t, off, d, o, n) { if (d === null || d === undefined) { this._err(0x501); return; } N.gl.bufferSubData(this._.id, t, off, d, o || 0, n || 0); },
    compressedTexImage2D(t, l, f, w, h, b, d, o, n) { if (glIsView(d)) N.gl.compressedTexImage(this._.id, false, t, l, f, 0, 0, w, h, d, o || 0, n || 0); else this._err(0x502); },
    compressedTexSubImage2D(t, l, x, y, w, h, f, d, o, n) { if (glIsView(d)) N.gl.compressedTexImage(this._.id, true, t, l, f, x, y, w, h, d, o || 0, n || 0); else this._err(0x502); },
    readPixels(x, y, w, h, f, t, dst, o) {
        if (dst === null || dst === undefined) { this._err(0x501); return; }
        N.gl.readPixels(this._.id, x, y, w, h, f, t, dst, o || 0);
    },
    /* an element, ImageData or ImageBitmap source: (sub, three, target, level, ifmt, x, y, z, fmt, type, src) */
    _texSrc(sub, three, t, l, ifmt, x, y, z, f, ty, src) {
        const s = this._;
        if (src instanceof ImageData) N.gl.texImageSource(s.id, sub, three, t, l, ifmt, x, y, z, f, ty, src.data, s.flip, s.premul, src.width, src.height);
        else if (src instanceof Element) N.gl.texImageSource(s.id, sub, three, t, l, ifmt, x, y, z, f, ty, src, s.flip, s.premul, 0, 0);
        else if (src instanceof ImageBitmap) {   /* its own orientation and alpha options apply, not the unpack state */
            const m = src._, d = m.src;
            if (!d) throw new DOMException('The ImageBitmap is closed.', 'InvalidStateError');
            if (d instanceof ImageData) N.gl.texImageSource(s.id, sub, three, t, l, ifmt, x, y, z, f, ty, d.data, m.flipY, m.premul, d.width, d.height);
            else N.gl.texImageSource(s.id, sub, three, t, l, ifmt, x, y, z, f, ty, d, m.flipY, m.premul, 0, 0);
        }
        else throw new TypeError("Failed to execute 'texImage2D': The provided value is not of type '(ArrayBufferView or TexImageSource)'.");
    },
    texImage2D(t, l, ifmt, a, b, c, d, e, f, g) {
        if (arguments.length === 6) { this._texSrc(false, false, t, l, ifmt, 0, 0, 0, a, b, c); return; }
        if (f !== null && typeof f === 'object' && !glIsView(f)) { this._texSrc(false, false, t, l, ifmt, 0, 0, 0, d, e, f); return; }
        const s = this._;
        N.gl.texImage(s.id, false, false, t, l, ifmt, 0, 0, 0, a, b, 1, d, e, f ?? null, g || 0, s.flip);
    },
    texSubImage2D(t, l, x, y, a, b, c, d, e, f) {
        if (arguments.length === 7) { this._texSrc(true, false, t, l, 0, x, y, 0, a, b, c); return; }
        if (e !== null && typeof e === 'object' && !glIsView(e)) { this._texSrc(true, false, t, l, 0, x, y, 0, c, d, e); return; }
        if (e === null || e === undefined) { this._err(0x501); return; }
        const s = this._;
        N.gl.texImage(s.id, true, false, t, l, 0, x, y, 0, a, b, 1, c, d, e, f || 0, s.flip);
    },
};

/* WebGL 2 only */
const GL2M = {
    copyBufferSubData(r, w, ro, wo, n) { N.gl.copyBufferSubData(this._.id, r, w, ro, wo, n); },
    getBufferSubData(t, so, dst, o, n) { if (glIsView(dst)) N.gl.getBufferSubData(this._.id, t, so, dst, o || 0, n || 0); },
    blitFramebuffer(a, b, c, d, e, f, g, h, m, fl) { N.gl.blitFramebuffer(this._.id, a, b, c, d, e, f, g, h, m, fl); this._drew(); },
    framebufferTextureLayer(t, a, x, l, ly) { const n = this._nm(x, WebGLTexture); if (n >= 0) N.gl.framebufferTextureLayer(this._.id, t, a, n, l, ly); },
    invalidateFramebuffer(t, a) { N.gl.invalidateFramebuffer(this._.id, t, [...a]); },
    invalidateSubFramebuffer(t, a, x, y, w, h) { N.gl.invalidateSubFramebuffer(this._.id, t, [...a], x, y, w, h); },
    readBuffer(b) { N.gl.readBuffer(this._.id, b); },
    getInternalformatParameter(t, f, p) { return this._.lost ? null : N.gl.getInternalformatParameter(this._.id, t, f, p); },
    renderbufferStorageMultisample(t, n, f, w, h) { N.gl.renderbufferStorageMultisample(this._.id, t, n, f, w, h); },
    texStorage2D(t, l, f, w, h) { N.gl.texStorage2D(this._.id, t, l, f, w, h); },
    texStorage3D(t, l, f, w, h, d) { N.gl.texStorage3D(this._.id, t, l, f, w, h, d); },
    texImage3D(t, l, ifmt, w, h, d, b, f, ty, src, o) {
        if (src !== null && typeof src === 'object' && !glIsView(src)) { this._texSrc(false, true, t, l, ifmt, 0, 0, 0, f, ty, src); return; }
        const s = this._;
        N.gl.texImage(s.id, false, true, t, l, ifmt, 0, 0, 0, w, h, d, f, ty, src ?? null, o || 0, s.flip);
    },
    texSubImage3D(t, l, x, y, z, w, h, d, f, ty, src, o) {
        if (src !== null && typeof src === 'object' && !glIsView(src)) { this._texSrc(true, true, t, l, 0, x, y, z, f, ty, src); return; }
        if (src === null || src === undefined) { this._err(0x501); return; }
        const s = this._;
        N.gl.texImage(s.id, true, true, t, l, 0, x, y, z, w, h, d, f, ty, src, o || 0, s.flip);
    },
    copyTexSubImage3D(t, l, xo, yo, zo, x, y, w, h) { N.gl.copyTexSubImage3D(this._.id, t, l, xo, yo, zo, x, y, w, h); },
    compressedTexImage3D() { this._err(0x502); },
    compressedTexSubImage3D() { this._err(0x502); },
    getFragDataLocation(p, name) { const n = this._nm(p, WebGLProgram); return n > 0 ? N.gl.getFragDataLocation(this._.id, n, String(name)) : -1; },
    uniform1ui(l, x) { const k = this._loc(l); if (k !== null) N.gl.uniform1ui(this._.id, k, x); },
    uniform2ui(l, x, y) { const k = this._loc(l); if (k !== null) N.gl.uniform2ui(this._.id, k, x, y); },
    uniform3ui(l, x, y, z) { const k = this._loc(l); if (k !== null) N.gl.uniform3ui(this._.id, k, x, y, z); },
    uniform4ui(l, x, y, z, w) { const k = this._loc(l); if (k !== null) N.gl.uniform4ui(this._.id, k, x, y, z, w); },
    uniform1uiv(l, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformv(this._.id, 8, k, glU32(v), o, n); },
    uniform2uiv(l, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformv(this._.id, 9, k, glU32(v), o, n); },
    uniform3uiv(l, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformv(this._.id, 10, k, glU32(v), o, n); },
    uniform4uiv(l, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformv(this._.id, 11, k, glU32(v), o, n); },
    uniformMatrix2x3fv(l, t, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformMatrix(this._.id, 3, k, t, glF32(v), o, n); },
    uniformMatrix3x2fv(l, t, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformMatrix(this._.id, 4, k, t, glF32(v), o, n); },
    uniformMatrix2x4fv(l, t, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformMatrix(this._.id, 5, k, t, glF32(v), o, n); },
    uniformMatrix4x2fv(l, t, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformMatrix(this._.id, 6, k, t, glF32(v), o, n); },
    uniformMatrix3x4fv(l, t, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformMatrix(this._.id, 7, k, t, glF32(v), o, n); },
    uniformMatrix4x3fv(l, t, v, o, n) { const k = this._loc(l); if (k !== null) N.gl.uniformMatrix(this._.id, 8, k, t, glF32(v), o, n); },
    vertexAttribI4i(i, x, y, z, w) { N.gl.vertexAttribI4i(this._.id, i, x, y, z, w); },
    vertexAttribI4iv(i, v) { N.gl.vertexAttribI4i(this._.id, i, v[0], v[1], v[2], v[3]); },
    vertexAttribI4ui(i, x, y, z, w) { N.gl.vertexAttribI4ui(this._.id, i, x, y, z, w); },
    vertexAttribI4uiv(i, v) { N.gl.vertexAttribI4ui(this._.id, i, v[0], v[1], v[2], v[3]); },
    vertexAttribIPointer(i, size, t, stride, off) { this._attribPointer(i, () => N.gl.vertexAttribIPointer(this._.id, i, size, t, stride, off)); },
    vertexAttribDivisor(i, d) { N.gl.vertexAttribDivisor(this._.id, i, d); },
    drawArraysInstanced(m, f, c, n) { if (this._canDraw()) { N.gl.drawArraysInstanced(this._.id, m, f, c, n); this._drew(); } },
    drawElementsInstanced(m, c, t, o, n) {
        if (!this._canDraw()) return;
        if (!this._.vao.eab) { this._err(0x502); return; }
        N.gl.drawElementsInstanced(this._.id, m, c, t, o, n); this._drew();
    },
    drawRangeElements(m, a, b, c, t, o) {
        if (!this._canDraw()) return;
        if (!this._.vao.eab) { this._err(0x502); return; }
        N.gl.drawRangeElements(this._.id, m, a, b, c, t, o); this._drew();
    },
    drawBuffers(b) { N.gl.drawBuffers(this._.id, [...b]); },
    clearBufferfv(b, d, v, o) { N.gl.clearBuffer(this._.id, 0, b, d, glF32(v), o || 0); this._drew(); },
    clearBufferiv(b, d, v, o) { N.gl.clearBuffer(this._.id, 1, b, d, glI32(v), o || 0); this._drew(); },
    clearBufferuiv(b, d, v, o) { N.gl.clearBuffer(this._.id, 2, b, d, glU32(v), o || 0); this._drew(); },
    clearBufferfi(b, d, depth, st) { N.gl.clearBufferfi(this._.id, b, d, depth, st); this._drew(); },
    createQuery() { return this._gen(7, WebGLQuery); },
    deleteQuery(q) { this._del(q, WebGLQuery); },
    isQuery(q) { return this._is(q, WebGLQuery); },
    beginQuery(t, q) { const n = this._nm(q, WebGLQuery); if (n > 0) N.gl.beginQuery(this._.id, t, n); else if (!n) this._err(0x502); },
    endQuery(t) { N.gl.endQuery(this._.id, t); },
    getQuery(t, p) { if (this._.lost) return null; const r = N.gl.getQuery(this._.id, t, p); return p === 0x8865 ? this._byName(7, r) : r; },
    getQueryParameter(q, p) { const n = this._nm(q, WebGLQuery); if (n <= 0) return null; const r = N.gl.getQueryParameter(this._.id, n, p); return p === 0x8867 ? !!r : r; },
    createSampler() { return this._gen(8, WebGLSampler); },
    deleteSampler(x) { this._del(x, WebGLSampler); },
    isSampler(x) { return this._is(x, WebGLSampler); },
    bindSampler(u, x) { const n = this._nm(x, WebGLSampler); if (n >= 0) N.gl.bindSampler(this._.id, u, n); },
    samplerParameteri(x, p, v) { const n = this._nm(x, WebGLSampler); if (n > 0) N.gl.samplerParameteri(this._.id, n, p, v); },
    samplerParameterf(x, p, v) { const n = this._nm(x, WebGLSampler); if (n > 0) N.gl.samplerParameterf(this._.id, n, p, v); },
    getSamplerParameter(x, p) { const n = this._nm(x, WebGLSampler); return n > 0 ? N.gl.getSamplerParameter(this._.id, n, p) : null; },
    fenceSync(c, f) {
        const s = this._;
        if (s.lost) return null;
        const n = N.gl.fenceSync(s.id, c, f);
        if (!n) return null;
        const o = Object.create(WebGLSync.prototype);
        def(o, '_', { c: this, n, k: -1, d: false });
        return o;
    },
    isSync(x) { return x instanceof WebGLSync && x._.c === this && !x._.d && N.gl.isSync(this._.id, x._.n); },
    deleteSync(x) { const n = this._nm(x, WebGLSync); if (n > 0) { N.gl.deleteSync(this._.id, n); x._.d = true; } },
    clientWaitSync(x, f, t) { const n = this._nm(x, WebGLSync); return n > 0 ? N.gl.clientWaitSync(this._.id, n, f) : 0x911D; },
    waitSync(x, f, t) { const n = this._nm(x, WebGLSync); if (n > 0) N.gl.waitSync(this._.id, n); },
    getSyncParameter(x, p) { const n = this._nm(x, WebGLSync); return n > 0 ? N.gl.getSyncParameter(this._.id, n, p) : null; },
    createTransformFeedback() { return this._gen(9, WebGLTransformFeedback); },
    deleteTransformFeedback(x) { this._del(x, WebGLTransformFeedback); },
    isTransformFeedback(x) { return this._is(x, WebGLTransformFeedback); },
    bindTransformFeedback(t, x) { const n = this._nm(x, WebGLTransformFeedback); if (n >= 0) N.gl.bindTransformFeedback(this._.id, t, n); },
    beginTransformFeedback(m) { N.gl.beginTransformFeedback(this._.id, m); },
    endTransformFeedback() { N.gl.endTransformFeedback(this._.id); },
    pauseTransformFeedback() { N.gl.pauseTransformFeedback(this._.id); },
    resumeTransformFeedback() { N.gl.resumeTransformFeedback(this._.id); },
    transformFeedbackVaryings(p, v, m) { const n = this._nm(p, WebGLProgram); if (n > 0) N.gl.transformFeedbackVaryings(this._.id, n, [...v].map(String), m); },
    getTransformFeedbackVarying(p, i) { const n = this._nm(p, WebGLProgram); const r = n > 0 ? N.gl.getActive(this._.id, n, i, 2) : null; return r ? new WebGLActiveInfo(GLI, r) : null; },
    bindBufferBase(t, i, b) { const n = this._nm(b, WebGLBuffer); if (n >= 0) N.gl.bindBufferBase(this._.id, t, i, n); },
    bindBufferRange(t, i, b, o, z) { const n = this._nm(b, WebGLBuffer); if (n >= 0) N.gl.bindBufferRange(this._.id, t, i, n, o, z); },
    getIndexedParameter(t, i) {
        if (this._.lost) return null;
        const r = N.gl.getIndexedParameter(this._.id, t, i);
        return t === 0x8C8F || t === 0x8A28 ? this._byName(0, r) : r;
    },
    getUniformIndices(p, names) { const n = this._nm(p, WebGLProgram); return n > 0 ? N.gl.getUniformIndices(this._.id, n, [...names].map(String)) : null; },
    getActiveUniforms(p, idx, q) { const n = this._nm(p, WebGLProgram); return n > 0 ? N.gl.getActiveUniforms(this._.id, n, [...idx], q) : null; },
    getUniformBlockIndex(p, name) { const n = this._nm(p, WebGLProgram); return n > 0 ? N.gl.getUniformBlockIndex(this._.id, n, String(name)) : 0xFFFFFFFF; },
    getActiveUniformBlockParameter(p, i, q) { const n = this._nm(p, WebGLProgram); return n > 0 ? N.gl.getActiveUniformBlockParameter(this._.id, n, i, q) : null; },
    getActiveUniformBlockName(p, i) { const n = this._nm(p, WebGLProgram); return n > 0 ? N.gl.getActiveUniformBlockName(this._.id, n, i) : null; },
    uniformBlockBinding(p, i, b) { const n = this._nm(p, WebGLProgram); if (n > 0) N.gl.uniformBlockBinding(this._.id, n, i, b); },
    createVertexArray() { return this._gen(6, WebGLVertexArrayObject); },
    deleteVertexArray(v) {
        if (v === null || v === undefined) return;
        const s = this._, cur = s.vao.o === v;
        if (v instanceof WebGLVertexArrayObjectOES ? !this._del(v, WebGLVertexArrayObjectOES) : !this._del(v, WebGLVertexArrayObject)) return;
        if (cur) s.vao = s.vao0;
    },
    isVertexArray(v) { return (v instanceof WebGLVertexArrayObject || v instanceof WebGLVertexArrayObjectOES) && v._.c === this && !v._.d && !this._.lost; },
    bindVertexArray(v) {
        const n = v instanceof WebGLVertexArrayObjectOES ? this._nm(v, WebGLVertexArrayObjectOES) : this._nm(v, WebGLVertexArrayObject);
        if (n < 0) return;
        N.gl.bindVertexArray(this._.id, n);
        this._.vao = v ? v._.vs : this._.vao0;
    },
};

class WebGLRenderingContext { constructor() { glIllegal(); } }
class WebGL2RenderingContext { constructor() { glIllegal(); } }
{
    const c1 = glTable(GL1_NAMES), c2 = Object.assign({}, c1, glTable(GL2_NAMES)), ds = c => { const o = {}; for (const k in c) o[k] = { value: c[k], enumerable: true }; return o; };
    const d1 = ds(c1), d2 = ds(c2), m1 = Object.getOwnPropertyDescriptors(GL1M), m2 = Object.getOwnPropertyDescriptors(GL2M);
    for (const k of Object.keys(m1)) if (k[0] === '_') m1[k].enumerable = false;
    Object.defineProperties(WebGLRenderingContext, d1); Object.defineProperties(WebGLRenderingContext.prototype, d1); Object.defineProperties(WebGLRenderingContext.prototype, m1);
    Object.defineProperties(WebGL2RenderingContext, d2); Object.defineProperties(WebGL2RenderingContext.prototype, d2);
    Object.defineProperties(WebGL2RenderingContext.prototype, m1); Object.defineProperties(WebGL2RenderingContext.prototype, m2);
}
/* (re)builds a context's state over native context id */
function glInit(g, id) {
    const s = g._;
    s.id = id; s.lost = false; s.errs = []; s.dirty = false; s.exts = {}; s.ctf = [];
    s.m = GL_KINDS.map(() => new Map());
    s.vao0 = glVao(null); s.vao = s.vao0; s.abuf = null; s.prog = null; s.dfb = null; s.rfb = null;
    s.flip = false; s.premul = false; s.conv = 0x9244;
    s.dexts = new Set(N.gl.exts(id).split(' '));
    s.maxAttr = N.gl.getParameter(id, 0x8869) || 16;
    const z = N.gl.size(id); s.w = z[0]; s.h = z[1];
    if (glReg) glReg.register(g, id, g);
}
/* canvas.getContext('webgl' | 'experimental-webgl' | 'webgl2', attributes): null when the canvas can't have one */
function glCreate(cv, v, o) {
    if (!N.gl || !N.gl.available()) return null;
    o = o && typeof o === 'object' ? o : {};
    const attrs = { alpha: o.alpha !== false, depth: o.depth !== false, stencil: !!o.stencil, antialias: o.antialias !== false, premultipliedAlpha: o.premultipliedAlpha !== false,
        preserveDrawingBuffer: !!o.preserveDrawingBuffer, powerPreference: o.powerPreference || 'default', failIfMajorPerformanceCaveat: !!o.failIfMajorPerformanceCaveat, desynchronized: !!o.desynchronized, xrCompatible: !!o.xrCompatible };
    const bits = (attrs.alpha ? 1 : 0) | (attrs.depth ? 2 : 0) | (attrs.stencil ? 4 : 0) | (attrs.antialias ? 8 : 0) | (attrs.premultipliedAlpha ? 16 : 0) | (attrs.preserveDrawingBuffer ? 32 : 0);
    const id = N.gl.create(cv, v, bits, cv.width, cv.height);
    if (id < 0) {
        queueMicrotask(() => cv.dispatchEvent(new WebGLContextEvent('webglcontextcreationerror', { statusMessage: 'Could not create a WebGL context.' })));
        return null;
    }
    const g = Object.create((v === 2 ? WebGL2RenderingContext : WebGLRenderingContext).prototype);
    const s = { cv, v, attrs, bits, restorable: false };
    s.flush = () => { s.dirty = false; if (!s.lost) N.gl.present(s.id); };
    def(g, '_', s);
    glInit(g, id);
    return g;
}

/* ImageBitmap: a source element or ImageData kept as is; WebGL and WebGPU uploads read its pixels then.
   Crops and resizes are not applied to element sources. */
class ImageBitmap {
    constructor(t) { if (t !== GLI) glIllegal(); }
    get width() { return this._.w; }
    get height() { return this._.h; }
    close() { this._.src = null; this._.w = this._.h = 0; }
}
const bmpSize = s => s instanceof ImageData ? [s.width, s.height] : s instanceof ImageBitmap ? [s._.w, s._.h]
    : s instanceof H.HTMLImageElement ? [s.naturalWidth, s.naturalHeight] : s instanceof H.HTMLVideoElement ? [s.videoWidth, s.videoHeight]
    : s instanceof H.HTMLCanvasElement ? [s.width, s.height] : null;
const bmpCrop = (d, sx, sy, sw, sh) => {
    const x0 = Math.max(0, sx | 0), y0 = Math.max(0, sy | 0), w = Math.max(0, Math.min(d.width - x0, sw | 0)), h = Math.max(0, Math.min(d.height - y0, sh | 0));
    const o = new ImageData(Math.max(w, 1), Math.max(h, 1));
    for (let y = 0; y < h; y++) o.data.set(d.data.subarray(((y0 + y) * d.width + x0) * 4, ((y0 + y) * d.width + x0 + w) * 4), y * o.width * 4);
    return o;
};
async function createImageBitmap(src, a, b, c, d, e) {
    const opts = (typeof a === 'object' && a ? a : e) || {};
    if (src instanceof Blob) {   /* decoded natively off the main thread to straight-alpha pixels */
        const id = N.imgDecode(await src.arrayBuffer());
        let r = id ? undefined : null;
        while (r === undefined) { await new Promise(f => setTimeout(f, 4)); r = N.imgTake(id); }
        if (!r) throw new DOMException('The source image could not be decoded.', 'InvalidStateError');
        src = new ImageData(new Uint8ClampedArray(r[2]), r[0], r[1]);
    }
    const z = bmpSize(src);
    if (!z) throw new TypeError("Failed to execute 'createImageBitmap': The provided value is not of type '(Blob or HTMLCanvasElement or HTMLImageElement or HTMLVideoElement or ImageBitmap or ImageData)'.");
    if (!z[0] || !z[1]) throw new DOMException('The source has no width or height.', 'InvalidStateError');
    if (src instanceof ImageBitmap) src = src._.src;
    if (typeof a === 'number' && src instanceof ImageData) src = bmpCrop(src, a, b, c, d);
    const o = Object.create(ImageBitmap.prototype), w = typeof a === 'number' ? Math.abs(c | 0) : z[0], h = typeof a === 'number' ? Math.abs(d | 0) : z[1];
    def(o, '_', { src, w, h, flipY: opts.imageOrientation === 'flipY', premul: opts.premultiplyAlpha === 'premultiply' });
    return o;
}

const canvasResized = cv => { if (cv.__ck === 'webgl' || cv.__ck === 'webgl2') cv.__cx._resize(cv.width, cv.height); };
/* canvas.captureStream(fps): a video track over the context's live source, opened once the canvas has a WebGL or WebGPU context */
function canvasStream(cv, fps) {
    const hub = {
        refs: 1, deviceId: '',
        get src() {
            const k = cv.__ck, x = cv.__cx;
            if (k === 'webgl' || k === 'webgl2') return x._.lost ? 0 : N.gl.capture(x._.id);
            if (k === 'webgpu') return x._.cid ? N.gpu.canvasCapture(x._.cid) : 0;
            return 0;
        },
        info() { return [cv.width, cv.height, fps > 0 ? +fps : 30, 0]; },
        release() { this.refs--; },
    };
    const t = new MediaStreamTrack(RTC_INTERNAL, 'video', '');
    t._.hub = hub;
    def(t, 'canvas', cv);
    def(t, 'requestFrame', () => {});
    return new MediaStream([t]);
}
