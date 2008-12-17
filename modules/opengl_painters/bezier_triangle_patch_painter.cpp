// K-3D
// Copyright (c) 1995-2008, Timothy M. Shead
//
// Contact: tshead@k-3d.com
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public
// License along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

/** \file
		\author Ashish Myles (marcianx@gmail.com)
*/

#include <k3d-i18n-config.h>
#include <k3dsdk/bezier_triangle_patch.h>
#include <k3dsdk/document_plugin_factory.h>
#include <k3dsdk/mesh_operations.h>
#include <k3dsdk/mesh_painter_gl.h>
#include <k3dsdk/painter_render_state_gl.h>
#include <k3dsdk/painter_selection_state_gl.h>
#include <k3dsdk/selection.h>
#include <k3dsdk/basic_math.h>

#include <boost/scoped_ptr.hpp>

#include <vector>

namespace module
{

namespace opengl
{

namespace painters
{

/////////////////////////////////////////////////////////////////////////////
// bezier_triangle_patch_painter

class bezier_triangle_patch_painter :
	public k3d::gl::mesh_painter
{
	typedef k3d::gl::mesh_painter base;

public:
	bezier_triangle_patch_painter(k3d::iplugin_factory& Factory, k3d::idocument& Document) :
		base(Factory, Document)
	{
	}

	void on_paint_mesh(const k3d::mesh& Mesh, const k3d::gl::painter_render_state& RenderState)
	{
		extract_and_render_bezier_triangle(Mesh, RenderState, false);
	}
	
	void on_select_mesh(const k3d::mesh& Mesh, const k3d::gl::painter_render_state& RenderState, const k3d::gl::painter_selection_state& SelectionState)
	{
		if (!SelectionState.select_bezier_triangle_patches)
			return;

		extract_and_render_bezier_triangle(Mesh, RenderState, true);
	}
	
	static k3d::iplugin_factory& get_factory()
	{
		static k3d::document_plugin_factory<bezier_triangle_patch_painter, k3d::interface_list<k3d::gl::imesh_painter > > factory(
			k3d::uuid(0x8d38bda1, 0xdf47e212, 0x1e06bfb8, 0xa8f440c5),
			"OpenGLBezierTrianglePatchPainter",
			_("Renders Bezier triangle patches"),
			"OpenGL Painter",
			k3d::iplugin_factory::EXPERIMENTAL);

		return factory;
	}

//	/// Converts the homogeneous coordinate to Cartesian.
//	point3 cartesian() const
//	{
//		k3d::double_t denom_inv = (d[3] == 0) ? k3d::double_t(1.0) : k3d::double_t(1.0) / d[3];
//		return point3(d[0] * denom, d[1] * denom, d[2] * denom);
//	}

	/// Render a rational triangle Bezier patch.
	static void gl_render_bezier_triangle(const std::vector<k3d::point4 > &control_points, k3d::uint_t order, k3d::bool_t select_mode)
	{
		assert(order > 0);

		// The selection mode is passed in so that it may be
		// (eventually) used to render a lower-resolution patch on
		// selection for performance.

		// For now, use a power-of-two EVALUATION DENSITY at least as large
		// as the number of control points.
		// (Powers of two avoid errors related to evaluation
		// watertightness along the boundary.)
		k3d::uint_t num_segments = 1;
		while (num_segments < order)
			num_segments *= 2;

		// Pre-tabulate the factorials
		std::vector<k3d::double_t > fact_inv_tab(order);
		k3d::double_t deg_fact  = k3d::factorial(order - 1);
		k3d::double_t degm_fact = k3d::factorial(order - 2);
		for(k3d::double_t n; n < k3d::double_t(order); n += 1.0)
			fact_inv_tab[n] = k3d::double_t(1.0) / k3d::factorial(n);

		// BEZIER TRIANGLES:
		//     http://en.wikipedia.org/wiki/B%C3%A9zier_triangle
		//
		// Triple-index notation with indices adding up to degree = order-1.
		// E.g. For deg 3, the control points c are labeled:
		// 	          (w=1)
		//             003
		//      c    102 012
		//         201 111 021
		// (u=1) 300 210 120 030 (v=1)
		//
		// For index ijk,
		//     u = i / deg,   v = j / deg,   w = k / deg
		//     which implies that  u + v + w = 1.
		//
		// Then, f(u, v) = Sum_{i,j,k} [ c_{ijk}  * choose(deg, i, j, k) * u^i * v^j * w^k ]
		// where choose(deg, i, j, k) = deg! / (i! j! k!)
		//
		std::vector<k3d::point4 >  eval_points ((num_segments + 1) * (num_segments + 2) / 2);
		std::vector<k3d::vector3 > eval_normals((num_segments + 1) * (num_segments + 2) / 2);
		for(k3d::uint_t ui = 0; ui <= num_segments; ++ui)
		{
			// When  ui  =  num_segments  :=  uvw_count - 1,  then  u = 0.
			// Similarly for v.
			const k3d::double_t u = k3d::double_t(ui) / num_segments;
			for(k3d::uint_t vi = 0; vi <= ui; ++vi)
			{
				const k3d::double_t v = k3d::double_t(vi) / num_segments;
				const k3d::double_t w = k3d::double_t(1.0) - u - v;

				// Indexing of eval_points and control_points.
				//          003                0
				//  ijk   102 012      -->    1 2  index
				//      201 111 021          3 4 5
				//    300 210 120 030       6 7 8 9
				// ...
				// index = (i+j) * ((i+j)+1) / 2 + j
				k3d::uint_t eval_i = (ui + vi) * (ui + vi + 1) / 2 + vi;
				k3d::point4 &eval_pt = eval_points[eval_i];
				k3d::point4 eval_diff_u_homog; // derivatives of the homogeneous coordinates
				k3d::point4 eval_diff_v_homog;
				// eval_points[eval_i] and eval_normals[eval_i] should already be 0
				for(k3d::uint_t i = 0; i < order; ++i)
				{
					const k3d::double_t upi  = k3d::fast_pow(u, i);
					const k3d::double_t upim = k3d::fast_pow(u, i - 1);
					for(k3d::uint_t j = 0; j < order - i; ++j)
					{
						const k3d::uint_t k = order - 1 - i - j;
						const k3d::double_t vpjm = k3d::fast_pow(v, j - 1);
						const k3d::double_t vpj  = k3d::fast_pow(v, j);
						const k3d::double_t wpk  = k3d::fast_pow(w, k);
						k3d::double_t coeff = deg_fact * fact_inv_tab[i] * fact_inv_tab[j] * fact_inv_tab[k];

						//         0
						//   i-,j-   i-,j
						//   3    ij    5
						// 6    7    8    9
						//
						k3d::uint_t cpts_ij  = (i + j) * (i + j + 1) / 2 + j;
						eval_pt += (coeff * upi * vpj * wpk) * to_vector(control_points[cpts_ij]);

						// (Homogeneous) derivatives
						const k3d::point4 &c_ij = control_points[cpts_ij];
						if (i > 0) {
							assert(int(cpts_ij) - int(i) >= 0);
							k3d::uint_t cpts_imj = cpts_ij - i;
							const k3d::point4 &c_imj = control_points[cpts_imj];
							k3d::double_t coeff_du = degm_fact * fact_inv_tab[i - 1] * fact_inv_tab[j] * fact_inv_tab[k];
							eval_diff_u_homog += (coeff_du * upi/u * vpj * wpk) * (
									k3d::vector4( c_ij[0] - c_imj[0], c_ij[1] - c_imj[1],
									             c_ij[2] - c_imj[2], c_ij[3] - c_imj[3] ));
						}
						if (j > 0) {
							assert(int(cpts_ij) - int(i) - 1 >= 0);
							k3d::uint_t cpts_ijm = cpts_ij - i - 1;
							const k3d::point4 &c_ijm = control_points[cpts_ijm];
							k3d::double_t coeff_dv = degm_fact * fact_inv_tab[i] * fact_inv_tab[j - 1] * fact_inv_tab[k];
							eval_diff_v_homog += (coeff_dv * upi * vpj/v * wpk) * (
									k3d::vector4( c_ij[0] - c_ijm[0], c_ij[1] - c_ijm[1],
									             c_ij[2] - c_ijm[2], c_ij[3] - c_ijm[3] ));
						}
					}
				}
				// Since we have a rational patch in general, we need
				// to use the division rule to compute tangents.
				//     diff(a/b) = (b a' - a b') / b^2
				// Note that the denominator (b) and its u derivative (b')
				// are eval_pt[3] and eval_diff_u_homog[3], respectively
				// (eval_diff_v_homog[3] when differentiating w.r.t. v).
				//
				// Since we don't care about the derivative but the
				// normalized normal, there is no need to divide by the
				// denominator.
				//
				//const k3d::double_t denom_inv = (eval_pt[3] == 0.0) ? 1.0 : 1.0 / (eval_pt[3] * eval_pt[3]);
				k3d::vector3 eval_diff_u = k3d::vector3(
						(eval_diff_u_homog[0] * eval_pt[3] - eval_pt[0] * eval_diff_u_homog[3]), // * denom_inv,
						(eval_diff_u_homog[1] * eval_pt[3] - eval_pt[1] * eval_diff_u_homog[3]), // * denom_inv,
						(eval_diff_u_homog[2] * eval_pt[3] - eval_pt[2] * eval_diff_u_homog[3])  // * denom_inv
						);
				k3d::vector3 eval_diff_v = k3d::vector3(
						(eval_diff_v_homog[0] * eval_pt[3] - eval_pt[0] * eval_diff_v_homog[3]), // * denom_inv,
						(eval_diff_v_homog[1] * eval_pt[3] - eval_pt[1] * eval_diff_v_homog[3]), // * denom_inv,
						(eval_diff_v_homog[2] * eval_pt[3] - eval_pt[2] * eval_diff_v_homog[3])  // * denom_inv
						);

				// normal = the normalized cross-product of the derivatives.
				eval_normals[eval_i] = k3d::normalize( eval_diff_u ^ eval_diff_v );
			}
		}

		// Render using triangle strips
		for (k3d::uint_t ui = 1; ui <= num_segments; ++ui)
		{
			glBegin(GL_TRIANGLE_STRIP);
			k3d::uint_t eval_i  = ui * (ui + 1) / 2;
			k3d::uint_t eval_im = eval_i - ui;
			k3d::gl::normal3d(eval_normals[eval_i]);
			k3d::gl::vertex4d(eval_points [eval_i]);
			for (k3d::uint_t vi = 0; vi < ui; ++vi)
			{
				k3d::gl::normal3d(eval_normals[eval_i + vi + 1]);
				k3d::gl::vertex4d(eval_points [eval_i + vi + 1]);
				k3d::gl::normal3d(eval_normals[eval_im + vi]);
				k3d::gl::vertex4d(eval_points [eval_im + vi]);
			}

			glEnd();
		}
	}

private:
	/// Common computation for bezier_triangle_patch_painter::on_paint_mesh() and bezier_triangle_patch_painter::on_select_mesh().
	void extract_and_render_bezier_triangle(const k3d::mesh& Mesh, const k3d::gl::painter_render_state& RenderState, k3d::bool_t select_mode)
	{
		k3d::uint_t primitive_index = 0; // Need these indices (OpenGL selection "names") only for SELECTION MODE
		for(k3d::mesh::primitives_t::const_iterator primitive = Mesh.primitives.begin(); primitive != Mesh.primitives.end(); ++primitive, ++primitive_index)
		{
			boost::scoped_ptr<k3d::bezier_triangle_patch::const_primitive> bezier_triangle_patch(k3d::bezier_triangle_patch::validate(**primitive));
			if (!bezier_triangle_patch)
				continue;

			const k3d::mesh::indices_t& patch_first_points = bezier_triangle_patch->patch_first_points;
			const k3d::mesh::orders_t& patch_orders = bezier_triangle_patch->patch_orders;
			const k3d::mesh::indices_t& patch_points = bezier_triangle_patch->patch_points;
			const k3d::mesh::weights_t& patch_point_weights = bezier_triangle_patch->patch_point_weights;
			const k3d::mesh::points_t& points = *Mesh.points;

			const k3d::uint_t num_patches = patch_orders.size();

			k3d::gl::store_attributes attributes;

			// Needed only for RENDERING MODE
			const k3d::mesh::selection_t& patch_selections = bezier_triangle_patch->patch_selections;
			glEnable(GL_LIGHTING);
			const k3d::color color = k3d::color(0.8, 0.8, 0.8);
			const k3d::color selected_color = RenderState.show_component_selection ? k3d::color(1, 0, 0) : color;

			glFrontFace(RenderState.inside_out ? GL_CW : GL_CCW);
			glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
			k3d::gl::set(GL_CULL_FACE, RenderState.draw_two_sided);

			if (select_mode) // Tokens are only for SELECTION MODE
				k3d::gl::push_selection_token(k3d::selection::PRIMITIVE, primitive_index);

			std::vector<k3d::point4> bezier_control_points;
			const k3d::uint_t patch_begin = 0;
			const k3d::uint_t patch_end = patch_begin + num_patches;
			for(k3d::uint_t patch = patch_begin; patch < patch_end; ++patch)
			{
				if (!select_mode) // Material is only for RENDERING MODE
					k3d::gl::material(GL_FRONT_AND_BACK, GL_DIFFUSE, patch_selections[patch] ? selected_color : color);

				const k3d::uint_t order = patch_orders[patch];
				assert(order > 0);
				const k3d::uint_t patch_size = (order * (order + 1)) / 2;

				// Convert control points to weighted points (since it is a rational patch in general).
				bezier_control_points.clear();
				bezier_control_points.reserve(patch_size);
				const k3d::uint_t point_begin = patch_first_points[patch];
				const k3d::uint_t point_end = point_begin + patch_size;
				assert(point_end <= patch_points.size());

				for(k3d::uint_t point = point_begin; point < point_end; ++point)
				{
					const k3d::double_t weight = patch_point_weights[point];
					bezier_control_points.push_back(k3d::point4(
						weight * points[patch_points[point]][0],
						weight * points[patch_points[point]][1],
						weight * points[patch_points[point]][2],
						weight));
				}

				if (select_mode) // Tokens are only for SELECTION MODE
					k3d::gl::push_selection_token(k3d::selection::UNIFORM, patch);

				// Tessellate/evaluate and render the patch.
				bezier_triangle_patch_painter::gl_render_bezier_triangle(bezier_control_points, order, false);

				if (select_mode) // Tokens are only for SELECTION MODE
					k3d::gl::pop_selection_token(); // UNIFORM
			}

			if (select_mode) // Tokens are only for SELECTION MODE
				k3d::gl::pop_selection_token(); // PRIMITIVE
		}
	}
};

/////////////////////////////////////////////////////////////////////////////
// bezier_triangle_patch_painter_factory

k3d::iplugin_factory& bezier_triangle_patch_painter_factory()
{
	return bezier_triangle_patch_painter::get_factory();
}

} // namespace painters

} // namespace opengl

} // namespace module


