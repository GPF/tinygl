#include "zgl.h"

#ifdef TINYGL_PROFILE_STAGES
uint64_t (*tgl_profile_clock)(void) = NULL;
uint64_t tgl_profile_transform_us = 0;
uint64_t tgl_profile_normal_us = 0;
uint64_t tgl_profile_light_us = 0;
uint64_t tgl_profile_submit_us = 0;
uint64_t tgl_profile_viewport_us = 0;
#endif

void glopNormal(GLContext * c, GLParam * p)
{
    V3 v;

    v.X = p[1].f;
    v.Y = p[2].f;
    v.Z = p[3].f;

    c->current_normal.X = v.X;
    c->current_normal.Y = v.Y;
    c->current_normal.Z = v.Z;
    c->current_normal.W = 0;
}

void glopTexCoord(GLContext * c, GLParam * p)
{
    c->current_tex_coord.X = p[1].f;
    c->current_tex_coord.Y = p[2].f;
    c->current_tex_coord.Z = p[3].f;
    c->current_tex_coord.W = p[4].f;
}

void glopEdgeFlag(GLContext * c, GLParam * p)
{
    c->current_edge_flag = p[1].i;
}

void glopColor(GLContext * c, GLParam * p)
{

    c->current_color.X = p[1].f;
    c->current_color.Y = p[2].f;
    c->current_color.Z = p[3].f;
    c->current_color.W = p[4].f;
    c->longcurrent_color[0] = p[5].ui;
    c->longcurrent_color[1] = p[6].ui;
    c->longcurrent_color[2] = p[7].ui;

    if (c->color_material_enabled) {
	GLParam q[7];
	q[0].op = OP_Material;
	q[1].i = c->current_color_material_mode;
	q[2].i = c->current_color_material_type;
	q[3].f = p[1].f;
	q[4].f = p[2].f;
	q[5].f = p[3].f;
	q[6].f = p[4].f;
	glopMaterial(c, q);
    }
}


void gl_eval_viewport(GLContext * c)
{
    GLViewport *v;
    float zsize = (1 << (ZB_Z_BITS + ZB_POINT_Z_FRAC_BITS));

    v = &c->viewport;

    v->trans.X = ((v->xsize - 0.5) / 2.0) + v->xmin;
    v->trans.Y = ((v->ysize - 0.5) / 2.0) + v->ymin;
    v->trans.Z = ((zsize - 0.5) / 2.0) + ((1 << ZB_POINT_Z_FRAC_BITS)) / 2;

    v->scale.X = (v->xsize - 0.5) / 2.0;
    v->scale.Y = -(v->ysize - 0.5) / 2.0;
    v->scale.Z = -((zsize - 0.5) / 2.0);
}

void glopBegin(GLContext * c, GLParam * p)
{
    int type;
    M4 tmp;

    assert(c->in_begin == 0);

    type = p[1].i;
    c->begin_type = type;
    c->in_begin = 1;
    c->vertex_n = 0;
    c->vertex_cnt = 0;

    if (c->matrix_model_projection_updated) {

	if (c->lighting_enabled) {
	    /* precompute inverse modelview */
	    gl_M4_Inv(&tmp, c->matrix_stack_ptr[0]);
	    gl_M4_Transpose(&c->matrix_model_view_inv, &tmp);
	} else {
	    float *m = &c->matrix_model_projection.m[0][0];
	    /* precompute projection matrix */
	    gl_M4_Mul(&c->matrix_model_projection,
		      c->matrix_stack_ptr[1],
		      c->matrix_stack_ptr[0]);
	    /* test to accelerate computation */
	    c->matrix_model_projection_no_w_transform = 0;
	    if (m[12] == 0.0 && m[13] == 0.0 && m[14] == 0.0)
		c->matrix_model_projection_no_w_transform = 1;
	}

	/* test if the texture matrix is not Identity */
	c->apply_texture_matrix = !gl_M4_IsId(c->matrix_stack_ptr[2]);

	c->matrix_model_projection_updated = 0;
    }
#ifdef TINYGL_USE_SH4ZAM
    if (!c->lighting_enabled) {
	/* Reload once per glBegin, then reuse XMTRX for all vertices in this
	 * primitive. The input matrix is TinyGL row-major, hence transpose. */
	shz_xmtrx_load_transpose_unaligned_4x4(
	    (const float *)(const void *)&c->matrix_model_projection);
    }
#endif
    /*  viewport */
    if (c->viewport.updated) {
	gl_eval_viewport(c);
	c->viewport.updated = 0;
    }
    /* triangle drawing functions */
    if (c->render_mode == GL_SELECT) {
	c->draw_triangle_front = gl_draw_triangle_select;
	c->draw_triangle_back = gl_draw_triangle_select;
    } else {
	switch (c->polygon_mode_front) {
	case GL_POINT:
	    c->draw_triangle_front = gl_draw_triangle_point;
	    break;
	case GL_LINE:
	    c->draw_triangle_front = gl_draw_triangle_line;
	    break;
	default:
	    c->draw_triangle_front = gl_draw_triangle_fill;
	    break;
	}

	switch (c->polygon_mode_back) {
	case GL_POINT:
	    c->draw_triangle_back = gl_draw_triangle_point;
	    break;
	case GL_LINE:
	    c->draw_triangle_back = gl_draw_triangle_line;
	    break;
	default:
	    c->draw_triangle_back = gl_draw_triangle_fill;
	    break;
	}
    }
}

/* coords, tranformation , clip code and projection */
/* TODO : handle all cases */
static inline void gl_vertex_transform(GLContext * c, GLVertex * v)
{
    float *m;
    V4 *n;

    if (c->lighting_enabled) {
#ifdef TINYGL_PROFILE_STAGES
	uint64_t __tgl_prof_t0 = tgl_profile_clock ? tgl_profile_clock() : 0;
#endif

#ifdef TINYGL_USE_SH4ZAM
	{
	    /* Eye coordinates: full modelview * (coord, 1). XMTRX is reloaded
	     * per vertex here since the lit path needs both the modelview and
	     * projection matrices in turn, unlike the unlit path which can
	     * keep one combined matrix loaded for the whole primitive. */
	    shz_vec4_t coord4 = shz_vec4_init(v->coord.X, v->coord.Y, v->coord.Z, 1.0f);
	    shz_vec4_t ec4, pc4;

	    shz_xmtrx_load_transpose_unaligned_4x4(
		(const float *)(const void *)c->matrix_stack_ptr[0]);
	    ec4 = shz_xmtrx_transform_vec4(coord4);
	    v->ec.X = ec4.x;
	    v->ec.Y = ec4.y;
	    v->ec.Z = ec4.z;
	    v->ec.W = ec4.w;

	    /* projection coordinates */
	    shz_xmtrx_load_transpose_unaligned_4x4(
		(const float *)(const void *)c->matrix_stack_ptr[1]);
	    pc4 = shz_xmtrx_transform_vec4(ec4);
	    v->pc.X = pc4.x;
	    v->pc.Y = pc4.y;
	    v->pc.Z = pc4.z;
	    v->pc.W = pc4.w;
	}
#else
	m = &c->matrix_stack_ptr[0]->m[0][0];
	v->ec.X = (v->coord.X * m[0] + v->coord.Y * m[1] +
		   v->coord.Z * m[2] + m[3]);
	v->ec.Y = (v->coord.X * m[4] + v->coord.Y * m[5] +
		   v->coord.Z * m[6] + m[7]);
	v->ec.Z = (v->coord.X * m[8] + v->coord.Y * m[9] +
		   v->coord.Z * m[10] + m[11]);
	v->ec.W = (v->coord.X * m[12] + v->coord.Y * m[13] +
		   v->coord.Z * m[14] + m[15]);

	/* projection coordinates */
	m = &c->matrix_stack_ptr[1]->m[0][0];
	v->pc.X = (v->ec.X * m[0] + v->ec.Y * m[1] +
		   v->ec.Z * m[2] + v->ec.W * m[3]);
	v->pc.Y = (v->ec.X * m[4] + v->ec.Y * m[5] +
		   v->ec.Z * m[6] + v->ec.W * m[7]);
	v->pc.Z = (v->ec.X * m[8] + v->ec.Y * m[9] +
		   v->ec.Z * m[10] + v->ec.W * m[11]);
	v->pc.W = (v->ec.X * m[12] + v->ec.Y * m[13] +
		   v->ec.Z * m[14] + v->ec.W * m[15]);
#endif

#ifdef TINYGL_PROFILE_STAGES
	if (tgl_profile_clock) {
	    uint64_t __tgl_prof_t1 = tgl_profile_clock();
	    tgl_profile_transform_us += __tgl_prof_t1 - __tgl_prof_t0;
	    __tgl_prof_t0 = __tgl_prof_t1;
	}
#endif

	n = &c->current_normal;

#ifdef TINYGL_USE_SH4ZAM
	{
	    /* transform_vec3 treats the input as a direction (w=0), which
	     * drops the translation column exactly like the scalar dot
	     * products below did by never adding m[3]/m[7]/m[11]. */
	    shz_vec3_t normal_in = shz_vec3_init(n->X, n->Y, n->Z);
	    shz_vec3_t normal_out;

	    shz_xmtrx_load_transpose_unaligned_4x4(
		(const float *)(const void *)&c->matrix_model_view_inv);
	    normal_out = shz_xmtrx_transform_vec3(normal_in);
	    v->normal.X = normal_out.x;
	    v->normal.Y = normal_out.y;
	    v->normal.Z = normal_out.z;
	}
#else
	m = &c->matrix_model_view_inv.m[0][0];
	v->normal.X = (n->X * m[0] + n->Y * m[1] + n->Z * m[2]);
	v->normal.Y = (n->X * m[4] + n->Y * m[5] + n->Z * m[6]);
	v->normal.Z = (n->X * m[8] + n->Y * m[9] + n->Z * m[10]);
#endif

	if (c->normalize_enabled) {
	    gl_V3_Norm(&v->normal);
	}
#ifdef TINYGL_PROFILE_STAGES
	if (tgl_profile_clock) tgl_profile_normal_us += tgl_profile_clock() - __tgl_prof_t0;
#endif
    } else {
	/* no eye coordinates needed, no normal */
	/* NOTE: W = 1 is assumed */
	m = &c->matrix_model_projection.m[0][0];

#ifdef TINYGL_USE_SH4ZAM
	{
	    shz_vec3_t coord = shz_vec3_init(v->coord.X, v->coord.Y, v->coord.Z);
	    shz_vec3_t projected = shz_xmtrx_transform_point3(coord);
	    v->pc.X = projected.x;
	    v->pc.Y = projected.y;
	    v->pc.Z = projected.z;
	    if (c->matrix_model_projection_no_w_transform) {
		v->pc.W = m[15];
	    } else {
		v->pc.W = (v->coord.X * m[12] + v->coord.Y * m[13] +
			   v->coord.Z * m[14] + m[15]);
	    }
	}
#else
	v->pc.X = (v->coord.X * m[0] + v->coord.Y * m[1] +
		   v->coord.Z * m[2] + m[3]);
	v->pc.Y = (v->coord.X * m[4] + v->coord.Y * m[5] +
		   v->coord.Z * m[6] + m[7]);
	v->pc.Z = (v->coord.X * m[8] + v->coord.Y * m[9] +
		   v->coord.Z * m[10] + m[11]);
	if (c->matrix_model_projection_no_w_transform) {
	    v->pc.W = m[15];
	} else {
	    v->pc.W = (v->coord.X * m[12] + v->coord.Y * m[13] +
		       v->coord.Z * m[14] + m[15]);
	}
#endif
    }

    v->clip_code = gl_clipcode(v->pc.X, v->pc.Y, v->pc.Z, v->pc.W);
}

void glopVertex(GLContext * c, GLParam * p)
{
    GLVertex *v;
    int n, i, cnt;

    assert(c->in_begin != 0);

    n = c->vertex_n;
    cnt = c->vertex_cnt;
    cnt++;
    c->vertex_cnt = cnt;

    /* quick fix to avoid crashes on large polygons */
    if (n >= c->vertex_max) {
	GLVertex *newarray;
	c->vertex_max <<= 1;	/* just double size */
	newarray = gl_malloc(sizeof(GLVertex) * c->vertex_max);
	if (!newarray) {
	    gl_fatal_error("unable to allocate GLVertex array.\n");
	}
	memcpy(newarray, c->vertex, n * sizeof(GLVertex));
	gl_free(c->vertex);
	c->vertex = newarray;
    }
    /* new vertex entry */
    v = &c->vertex[n];
    n++;

    v->coord.X = p[1].f;
    v->coord.Y = p[2].f;
    v->coord.Z = p[3].f;
    v->coord.W = p[4].f;

    gl_vertex_transform(c, v);

    /* color */

    if (c->lighting_enabled) {
#ifdef TINYGL_PROFILE_STAGES
	uint64_t __tgl_prof_light_t0 = tgl_profile_clock ? tgl_profile_clock() : 0;
#endif
	gl_shade_vertex(c, v);
#ifdef TINYGL_PROFILE_STAGES
	if (tgl_profile_clock) tgl_profile_light_us += tgl_profile_clock() - __tgl_prof_light_t0;
#endif
    } else {
	v->color = c->current_color;
    }

    /* tex coords */

    if (c->texture_2d_enabled) {
	if (c->apply_texture_matrix) {
	    gl_M4_MulV4(&v->tex_coord, c->matrix_stack_ptr[2], &c->current_tex_coord);
	} else {
	    v->tex_coord = c->current_tex_coord;
	}
    }
    /* precompute the mapping to the viewport */
    if (v->clip_code == 0) {
#ifdef TINYGL_PROFILE_STAGES
	uint64_t __tgl_prof_vp_t0 = tgl_profile_clock ? tgl_profile_clock() : 0;
#endif
	gl_transform_to_viewport(c, v);
#ifdef TINYGL_PROFILE_STAGES
	if (tgl_profile_clock) tgl_profile_viewport_us += tgl_profile_clock() - __tgl_prof_vp_t0;
#endif
    }

    /* edge flag */

    v->edge_flag = c->current_edge_flag;

    switch (c->begin_type) {
    case GL_POINTS:
	gl_draw_point(c, &c->vertex[0]);
	n = 0;
	break;

    case GL_LINES:
	if (n == 2) {
	    gl_draw_line(c, &c->vertex[0], &c->vertex[1]);
	    n = 0;
	}
	break;
    case GL_LINE_STRIP:
    case GL_LINE_LOOP:
	if (n == 1) {
	    c->vertex[2] = c->vertex[0];
	} else if (n == 2) {
	    gl_draw_line(c, &c->vertex[0], &c->vertex[1]);
	    c->vertex[0] = c->vertex[1];
	    n = 1;
	}
	break;

    case GL_TRIANGLES:
	if (n == 3) {
	    gl_draw_triangle(c, &c->vertex[0], &c->vertex[1], &c->vertex[2]);
	    n = 0;
	}
	break;
    case GL_TRIANGLE_STRIP:
#ifdef TINYGL_USE_DREAMCAST_PVR
	/* Keep the complete stream until glEnd so the Dreamcast backend can
	 * submit eligible strips natively. The fallback replays the same parity
	 * ordering through gl_draw_triangle(). */
#else
	if (cnt >= 3) {
	    if (n == 3) n = 0;
	    if (cnt & 1)
		gl_draw_triangle(c, &c->vertex[0], &c->vertex[1], &c->vertex[2]);
	    else
		gl_draw_triangle(c, &c->vertex[2], &c->vertex[1], &c->vertex[0]);
	}
#endif
	break;
    case GL_TRIANGLE_FAN:
	if (n == 3) {
	    gl_draw_triangle(c, &c->vertex[0], &c->vertex[1], &c->vertex[2]);
	    c->vertex[1] = c->vertex[2];
	    n = 2;
	}
	break;

    case GL_QUADS:
	if (n == 4) {
	    c->vertex[2].edge_flag = 0;
	    gl_draw_triangle(c, &c->vertex[0], &c->vertex[1], &c->vertex[2]);
	    c->vertex[2].edge_flag = 1;
	    c->vertex[0].edge_flag = 0;
	    gl_draw_triangle(c, &c->vertex[0], &c->vertex[2], &c->vertex[3]);
	    n = 0;
	}
	break;

    case GL_QUAD_STRIP:
	if (n == 4) {
	    gl_draw_triangle(c, &c->vertex[0], &c->vertex[1], &c->vertex[2]);
	    gl_draw_triangle(c, &c->vertex[1], &c->vertex[3], &c->vertex[2]);
	    for (i = 0; i < 2; i++)
		c->vertex[i] = c->vertex[i + 2];
	    n = 2;
	}
	break;
    case GL_POLYGON:
	break;
    default:
	gl_fatal_error("glBegin: type %x not handled\n", c->begin_type);
    }

    c->vertex_n = n;
}

void glopEnd(GLContext * c, GLParam * param)
{
    assert(c->in_begin == 1);

#ifdef TINYGL_USE_DREAMCAST_PVR
    if (c->begin_type == GL_TRIANGLE_STRIP && c->vertex_cnt >= 3) {
	int i;
	int eligible = c->polygon_mode_front == GL_FILL &&
	               c->polygon_mode_back == GL_FILL &&
	               c->render_mode == GL_RENDER &&
	               !c->cull_face_enabled && c->pvr_backend &&
	               !c->lighting_enabled;

	for (i = 0; eligible && i < c->vertex_cnt; ++i) {
	    if (c->vertex[i].clip_code != 0) eligible = 0;
	}
	if (eligible) {
	    tgl_pvr_draw_strip(c, c->vertex, c->vertex_cnt);
	} else {
	    for (i = 0; i + 2 < c->vertex_cnt; ++i) {
		if (i & 1)
		    gl_draw_triangle(c, &c->vertex[i + 1], &c->vertex[i],
			             &c->vertex[i + 2]);
		else
		    gl_draw_triangle(c, &c->vertex[i], &c->vertex[i + 1],
			             &c->vertex[i + 2]);
	    }
	}
    } else
#endif
    if (c->begin_type == GL_LINE_LOOP) {
	if (c->vertex_cnt >= 3) {
	    gl_draw_line(c, &c->vertex[0], &c->vertex[2]);
	}
    } else if (c->begin_type == GL_POLYGON) {
	int i = c->vertex_cnt;
	while (i >= 3) {
	    i--;
	    gl_draw_triangle(c, &c->vertex[i], &c->vertex[0], &c->vertex[i - 1]);
	}
    }
    c->in_begin = 0;
}
