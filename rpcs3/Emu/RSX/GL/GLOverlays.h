#pragma once

#include "stdafx.h"
#include "GLHelpers.h"
#include "../Overlays.h"

namespace gl
{
	struct overlay_pass
	{
		std::string fs_src;
		std::string vs_src;

		gl::glsl::program program_handle;
		gl::glsl::shader vs;
		gl::glsl::shader fs;

		gl::fbo fbo;

		gl::vao m_vao;
		gl::buffer m_vertex_data_buffer;

		bool compiled = false;

		u32 num_drawable_elements = 4;
		GLenum primitives = GL_TRIANGLE_STRIP;

		void create()
		{
			if (!compiled)
			{
				fs.create(gl::glsl::shader::type::fragment);
				fs.source(fs_src);
				fs.compile();

				vs.create(gl::glsl::shader::type::vertex);
				vs.source(vs_src);
				vs.compile();

				program_handle.create();
				program_handle.attach(vs);
				program_handle.attach(fs);
				program_handle.make();

				fbo.create();

				m_vertex_data_buffer.create();

				int old_vao;
				glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);

				m_vao.create();
				m_vao.bind();

				m_vao.array_buffer = m_vertex_data_buffer;
				m_vao[0] = buffer_pointer(&m_vao);

				glBindVertexArray(old_vao);

				compiled = true;
			}
		}

		void destroy()
		{
			if (compiled)
			{
				program_handle.remove();
				vs.remove();
				fs.remove();

				fbo.remove();
				m_vao.remove();
				m_vertex_data_buffer.remove();

				compiled = false;
			}
		}

		virtual void on_load() {}
		virtual void on_unload() {}

		virtual void bind_resources() {}
		virtual void cleanup_resources() {}

		virtual void upload_vertex_data(f32* data, u32 elements_count)
		{
			elements_count <<= 2;
			m_vertex_data_buffer.data(elements_count, data);
		}

		virtual void emit_geometry()
		{
			int old_vao;
			glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);

			m_vao.bind();
			glDrawArrays(primitives, 0, num_drawable_elements);

			glBindVertexArray(old_vao);
		}

		virtual void run(u16 w, u16 h, GLuint target_texture, bool depth_target)
		{
			if (!compiled)
			{
				LOG_ERROR(RSX, "You must initialize overlay passes with create() before calling run()");
				return;
			}

			GLint program;
			GLint old_fbo;
			GLint depth_func;
			GLint viewport[4];
			GLboolean color_writes[4];
			GLboolean depth_write;

			if (target_texture)
			{
				glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
				glBindFramebuffer(GL_FRAMEBUFFER, fbo.id());

				if (depth_target)
				{
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, target_texture, 0);
					glDrawBuffer(GL_NONE);
				}
				else
				{
					GLenum buffer = GL_COLOR_ATTACHMENT0;
					glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_texture, 0);
					glDrawBuffers(1, &buffer);
				}
			}

			if (!target_texture || glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
			{
				// Push rasterizer state
				glGetIntegerv(GL_VIEWPORT, viewport);
				glGetBooleanv(GL_COLOR_WRITEMASK, color_writes);
				glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write);
				glGetIntegerv(GL_CURRENT_PROGRAM, &program);
				glGetIntegerv(GL_DEPTH_FUNC, &depth_func);

				GLboolean scissor_enabled = glIsEnabled(GL_SCISSOR_TEST);
				GLboolean depth_test_enabled = glIsEnabled(GL_DEPTH_TEST);
				GLboolean cull_face_enabled = glIsEnabled(GL_CULL_FACE);
				GLboolean blend_enabled = glIsEnabled(GL_BLEND);
				GLboolean stencil_test_enabled = glIsEnabled(GL_STENCIL_TEST);

				// Set initial state
				glViewport(0, 0, w, h);
				glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
				glDepthMask(depth_target ? GL_TRUE : GL_FALSE);

				// Disabling depth test will also disable depth writes which is not desired
				glDepthFunc(GL_ALWAYS);
				glEnable(GL_DEPTH_TEST);

				if (scissor_enabled) glDisable(GL_SCISSOR_TEST);
				if (cull_face_enabled) glDisable(GL_CULL_FACE);
				if (blend_enabled) glDisable(GL_BLEND);
				if (stencil_test_enabled) glDisable(GL_STENCIL_TEST);

				// Render
				program_handle.use();
				on_load();
				bind_resources();
				emit_geometry();

				// Clean up
				if (target_texture)
				{
					if (depth_target)
						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
					else
						glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);

					glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
				}

				glUseProgram((GLuint)program);

				glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
				glColorMask(color_writes[0], color_writes[1], color_writes[2], color_writes[3]);
				glDepthMask(depth_write);
				glDepthFunc(depth_func);

				if (!depth_test_enabled) glDisable(GL_DEPTH_TEST);
				if (scissor_enabled) glEnable(GL_SCISSOR_TEST);
				if (cull_face_enabled) glEnable(GL_CULL_FACE);
				if (blend_enabled) glEnable(GL_BLEND);
				if (stencil_test_enabled) glEnable(GL_STENCIL_TEST);
			}
			else
			{
				LOG_ERROR(RSX, "Overlay pass failed because framebuffer was not complete. Run with debug output enabled to diagnose the problem");
			}
		}
	};

	struct depth_convert_pass : public overlay_pass
	{
		depth_convert_pass()
		{
			vs_src =
			{
				"#version 420\n\n"
				"out vec2 tc0;\n"
				"\n"
				"void main()\n"
				"{\n"
				"	vec2 positions[] = {vec2(-1., -1.), vec2(1., -1.), vec2(-1., 1.), vec2(1., 1.)};\n"
				"	vec2 coords[] = {vec2(0., 0.), vec2(1., 0.), vec2(0., 1.), vec2(1., 1.)};\n"
				"	gl_Position = vec4(positions[gl_VertexID % 4], 0., 1.);\n"
				"	tc0 = coords[gl_VertexID % 4];\n"
				"}\n"
			};

			fs_src =
			{
				"#version 420\n\n"
				"in vec2 tc0;\n"
				"layout(binding=31) uniform sampler2D fs0;\n"
				"\n"
				"void main()\n"
				"{\n"
				"	vec4 rgba_in = texture(fs0, tc0);\n"
				"	gl_FragDepth = rgba_in.w * 0.99609 + rgba_in.x * 0.00389 + rgba_in.y * 0.00002;\n"
				"}\n"
			};
		}

		void run(u16 w, u16 h, GLuint target, GLuint source)
		{
			glActiveTexture(GL_TEXTURE31);
			glBindTexture(GL_TEXTURE_2D, source);

			overlay_pass::run(w, h, target, true);
		}
	};

	struct rgba8_unorm_rg16_sfloat_convert_pass : public overlay_pass
	{
		//Not really needed since directly copying data via ARB_copy_image works out fine
		rgba8_unorm_rg16_sfloat_convert_pass()
		{
			vs_src =
			{
				"#version 420\n\n"
				"\n"
				"void main()\n"
				"{\n"
				"	vec2 positions[] = {vec2(-1., -1.), vec2(1., -1.), vec2(-1., 1.), vec2(1., 1.)};\n"
				"	gl_Position = vec4(positions[gl_VertexID % 4], 0., 1.);\n"
				"}\n"
			};

			fs_src =
			{
				"#version 420\n\n"
				"layout(binding=31) uniform sampler2D fs0;\n"
				"layout(location=0) out vec4 ocol;\n"
				"\n"
				"void main()\n"
				"{\n"
				"	uint value = packUnorm4x8(texelFetch(fs0, ivec2(gl_FragCoord.xy), 0).zyxw);\n"
				"	ocol.xy = unpackHalf2x16(value);\n"
				"}\n"
			};
		}

		void run(u16 w, u16 h, GLuint target, GLuint source)
		{
			glActiveTexture(GL_TEXTURE31);
			glBindTexture(GL_TEXTURE_2D, source);

			overlay_pass::run(w, h, target, false);
		}
	};

	struct ui_overlay_text : public overlay_pass
	{
		ui_overlay_text()
		{
			vs_src =
			{
				"#version 420\n\n"
				"layout(location=0) in vec4 in_pos;\n"
				"layout(location=0) out vec2 tc0;\n"
				"uniform vec4 ui_scale_parameters;\n"
				"\n"
				"void main()\n"
				"{\n"
				"	const vec2 offsets[] = {vec2(0., 0.), vec2(1., 0.), vec2(1., 1.), vec2(0., 1.)};\n"
				"	vec2 pos = offsets[gl_VertexID % 4] * ui_scale_parameters.xy;\n"
				"	tc0 = offsets[gl_VertexID % 4] * ui_scale_parameters.zw;\n"
				"	tc0.y += (in_pos.z / 16.) / 16.;\n"
				"	tc0.x += (in_pos.z % 16.) / 16.;\n"
				"	gl_Position = vec4(pos, 0., 1.);\n"
				"}\n"
			};

			fs_src =
			{
				"#version 420\n\n"
				"layout(binding=31) uniform sampler2D fs0;\n"
				"layout(location=0) in vec2 tc0;\n"
				"layout(location=0) out vec4 ocol;\n"
				"\n"
				"void main()\n"
				"{\n"
				"	ocol = texture(fs0, tc0).xxxx;\n"
				"}\n"
			};
		}

		void load_config()
		{

		}

		void run(u16 w, u16 h, GLuint target, GLuint source, rsx::overlays::user_interface& ui)
		{
			glActiveTexture(GL_TEXTURE31);
			glBindTexture(GL_TEXTURE_2D, source);

			//Set up vaos, etc
			overlay_pass::run(w, h, target, false);
		}
	};

	struct ui_overlay_debug : public overlay_pass
	{
		u32 num_elements = 0;
		std::vector<std::unique_ptr<gl::texture>> resources;

		ui_overlay_debug(/*rsx::overlays::resource_config& configuration*/)
		{
			vs_src =
			{
				"#version 420\n\n"
				"layout(location=0) in vec4 in_pos;\n"
				"layout(location=0) out vec2 tc0;\n"
				"uniform vec4 ui_scale;\n"
				"\n"
				"void main()\n"
				"{\n"
				"	const vec2 offsets[] = {vec2(0., 0.), vec2(1., 0.), vec2(0., 1.), vec2(1., 1.)};\n"
				"	tc0 = offsets[gl_VertexID % 4];\n"
				"	vec4 pos = in_pos / ui_scale;\n"
				"	pos.y = (1. - pos.y);\n"
				"	gl_Position = (pos + pos) - 1.;\n"
				"}\n"
			};

			fs_src =
			{
				"#version 420\n\n"
				"//layout(binding=31) uniform sampler2D fs0;\n"
				"layout(location=0) in vec2 tc0;\n"
				"layout(location=0) out vec4 ocol;\n"
				"\n"
				"void main()\n"
				"{\n"
				"	//ocol = texture(fs0, tc0).xxxx;\n"
				"	if (tc0.x < 0.05 || tc0.y < 0.05 ||\n"
				"		tc0.x > 0.95 || tc0.y > 0.95)\n"
				"		ocol = vec4(1.);\n"
				"	else\n"
				"		ocol = vec4(0.);\n"
				"}\n"
			};
		}

		void run(u16 w, u16 h, GLuint target, rsx::overlays::user_interface& ui)
		{
			glProgramUniform4f(program_handle.id(), program_handle.uniforms["ui_scale"].location(), (f32)ui.virtual_width, (f32)ui.virtual_height, 1.f, 1.f);
			for (auto &cmd : ui.get_compiled().draw_commands)
			{
				upload_vertex_data((f32*)cmd.second.data(), cmd.second.size() * 4);
				num_drawable_elements = cmd.second.size();
				overlay_pass::run(w, h, target, false);
			}
		}
	};
}