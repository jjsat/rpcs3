#pragma once
#include "Utilities/types.h"
#include "Utilities/geometry.h"

#include <string>
#include <vector>
#include <memory>

// STB_IMAGE_IMPLEMENTATION and STB_TRUETYPE_IMPLEMENTATION defined externally
#include <stb_image.h>
#include <stb_truetype.h>

// Definitions for common UI controls and their routines
namespace rsx
{
	namespace overlays
	{
		enum image_resource_id : u8
		{
			//NOTE: 1 - 252 are user defined
			none = 0,         //No image
			font_file = 253,  //Font file
			game_icon = 254,  //Use game icon
			backbuffer = 255  //Use current backbuffer contents
		};

		struct vertex
		{
			float values[4];

			vertex() {}

			vertex(float x, float y)
			{
				vec2(x, y);
			}

			vertex(float x, float y, float z)
			{
				vec3(x, y, z);
			}

			vertex(float x, float y, float z, float w)
			{
				vec4(x, y, z, w);
			}

			float& operator[](int index)
			{
				return values[index];
			}

			void vec2(float x, float y)
			{
				values[0] = x;
				values[1] = y;
				values[2] = 0.f;
				values[3] = 1.f;
			}

			void vec3(float x, float y, float z)
			{
				values[0] = x;
				values[1] = y;
				values[2] = z;
				values[3] = 1.f;
			}

			void vec4(float x, float y, float z, float w)
			{
				values[0] = x;
				values[1] = y;
				values[2] = z;
				values[3] = w;
			}

			void operator += (vertex& other)
			{
				values[0] += other.values[0];
				values[1] += other.values[1];
				values[2] += other.values[2];
				values[3] += other.values[3];
			}

			void operator -= (vertex& other)
			{
				values[0] -= other.values[0];
				values[1] -= other.values[1];
				values[2] -= other.values[2];
				values[3] -= other.values[3];
			}
		};

		struct font
		{
			const u32 width = 1024;
			const u32 height = 1024;
			const u32 oversample = 2;
			const u32 char_count = 256; //16x16 grid at max 48pt

			f32 size_pt = 12.f;
			f32 size_px = 16.f; //Default font 12pt size
			f32 em_size = 0.f;
			std::string font_name;
			std::vector<stbtt_packedchar> pack_info;
			std::vector<u8> glyph_data;
			bool initialized = false;

			font(const char *ttf_name, f32 size)
			{
				//Init glyph
				std::vector<u8> bytes;
				fs::file f(std::string("C:/Windows/Fonts/") + ttf_name + ".ttf");

				if (!f.size())
				{
					LOG_ERROR(RSX, "Failed to initialize font '%s.ttf'", ttf_name);
					return;
				}

				f.read(bytes, f.size());

				glyph_data.resize(width * height);
				pack_info.resize(256);

				stbtt_pack_context context;
				if (!stbtt_PackBegin(&context, glyph_data.data(), width, height, 0, 1, nullptr))
				{
					LOG_ERROR(RSX, "Font packing failed");
					return;
				}

				stbtt_PackSetOversampling(&context, oversample, oversample);

				//Convert pt to px
				size_px = ceilf((f32)size * 96.f / 72.f);
				size_pt = size;

				if (!stbtt_PackFontRange(&context, bytes.data(), 0, size_px, 0, 256, pack_info.data()))
				{
					LOG_ERROR(RSX, "Font packing failed");
					stbtt_PackEnd(&context);
					return;
				}

				stbtt_PackEnd(&context);

				f32 unused;
				get_char('m', em_size, unused);

				font_name = ttf_name;
				initialized = true;
			}

			stbtt_aligned_quad get_char(char c, f32 &x_advance, f32 &y_advance)
			{
				stbtt_aligned_quad quad;
				stbtt_GetPackedQuad(pack_info.data(), width, height, c, &x_advance, &y_advance, &quad, true);
				return quad;
			}

			std::vector<vertex> render_text(const char *text)
			{
				if (!initialized)
				{
					return{};
				}

				std::vector<vertex> result;

				int i = 0;
				f32 x_advance = 0.f, y_advance = 0.f;

				while (true)
				{
					if (char c = text[i++])
					{
						if (c >= char_count)
						{
							//Unsupported glyph, render null for now
							c = ' ';
						}

						auto quad = get_char(c, x_advance, y_advance);
						result.push_back({ quad.x0, quad.y0, quad.s0, quad.t0 });
						result.push_back({ quad.x1, quad.y0, quad.s1, quad.t0 });
						result.push_back({ quad.x0, quad.y1, quad.s0, quad.t1 });
						result.push_back({ quad.x1, quad.y1, quad.s1, quad.t1 });
					}
					else
					{
						break;
					}
				}

				return result;
			}
		};

		//TODO: Singletons are cancer
		class fontmgr
		{
		private:
			std::vector<std::unique_ptr<font>> fonts;
			static fontmgr *m_instance;

			font* find(const char *name, int size)
			{
				for (auto &f : fonts)
				{
					if (f->font_name == name &&
						f->size_pt == size)
						return f.get();
				}

				fonts.push_back(std::make_unique<font>(name, (f32)size));
				return fonts.back().get();
			}

		public:

			fontmgr() {}
			~fontmgr()
			{
				if (m_instance)
				{
					delete m_instance;
					m_instance = nullptr;
				}
			}

			static font* get(const char *name, int size)
			{
				if (m_instance == nullptr)
					m_instance = new fontmgr;

				return m_instance->find(name, size);
			}
		};

		struct resource_config
		{
			struct image_info
			{
				int w = 0, h = 0;
				int bpp = 0;
				u8* data = nullptr;

				image_info(image_info&) = delete;

				image_info(const char* filename)
				{
					std::vector<u8> bytes;
					fs::file f(filename);
					f.read(bytes, f.size());
					data = stbi_load_from_memory(bytes.data(), (s32)f.size(), &w, &h, &bpp, STBI_rgb_alpha);
				}

				~image_info()
				{
					stbi_image_free(data);
					data = nullptr;
					w = h = bpp = 0;
				}
			};

			enum standard_image_resource : u8
			{
				fade_top = 1,
				fade_bottom,
				cross,
				circle,
				triangle,
				square,
				save,
				new_entry
			};

			//Define resources
			std::vector<std::string> texture_resource_files;
			std::vector<std::unique_ptr<image_info>> texture_raw_data;

			resource_config()
			{
				texture_resource_files.push_back("fade_top.png");
				texture_resource_files.push_back("fade_bottom.png");
				texture_resource_files.push_back("cross.png");
				texture_resource_files.push_back("circle.png");
				texture_resource_files.push_back("triangle.png");
				texture_resource_files.push_back("square.png");
				texture_resource_files.push_back("save.png");
				texture_resource_files.push_back("new.png");
			}

			void load_files()
			{
				for (const auto &res : texture_resource_files)
				{
					auto info = std::make_unique<image_info>((fs::get_config_dir() + "/Icons/ui/" + res).c_str());
					texture_raw_data.push_back(std::move(info));
				}
			}

			void free_resources()
			{
				texture_raw_data.clear();
			}
		};

		struct compiled_resource
		{
			struct command_config
			{
				color4f color = { 1.f, 1.f, 1.f, 1.f };
				bool pulse_glow = false;

				u8 texture_ref = image_resource_id::none;
				font *font_ref = nullptr;

				command_config() {}

				command_config(u8 ref)
				{
					texture_ref = ref;
					font_ref = nullptr;
				}

				command_config(font *ref)
				{
					texture_ref = image_resource_id::font_file;
					font_ref = ref;
				}
			};

			std::vector<std::pair<command_config, std::vector<vertex>>> draw_commands;

			void add(compiled_resource& other)
			{
				auto old_size = draw_commands.size();
				draw_commands.resize(old_size + other.draw_commands.size());
				std::copy(other.draw_commands.begin(), other.draw_commands.end(), draw_commands.begin() + old_size);
			}

			void add(compiled_resource& other, f32 x_offset, f32 y_offset)
			{
				auto old_size = draw_commands.size();
				draw_commands.resize(old_size + other.draw_commands.size());
				std::copy(other.draw_commands.begin(), other.draw_commands.end(), draw_commands.begin() + old_size);

				for (size_t n = old_size; n < draw_commands.size(); ++n)
				{
					for (auto &v : draw_commands[n].second)
						v += vertex(x_offset, y_offset, 0.f, 0.f);
				}
			}
		};

		struct overlay_element
		{
			u16 x = 0;
			u16 y = 0;
			u16 w = 0;
			u16 h = 0;

			std::string text;
			font* font = nullptr;

			color4f back_color = { 0.f, 0.f, 0.f, 1.f };
			color4f fore_color = { 1.f, 1.f, 1.f, 1.f };
			bool pulse_effect_enabled = false;

			compiled_resource compiled_resources;
			bool is_compiled = false;

			f32 padding_left = 0.f;
			f32 padding_right = 0.f;
			f32 padding_top = 0.f;
			f32 padding_bottom = 0.f;

			overlay_element() {}
			overlay_element(u16 _w, u16 _h) : w(_w), h(_h) {}

			virtual void refresh()
			{
				//Just invalidate for draw when get_compiled() is called
				is_compiled = false;
			}

			virtual void translate(s16 _x, s16 _y)
			{
				x = (u16)(x + _x);
				y = (u16)(y + _y);

				is_compiled = false;
			}

			virtual void scale(f32 _x, f32 _y, bool origin_scaling)
			{
				if (origin_scaling)
				{
					x = (u16)(_x * x);
					y = (u16)(_y * y);
				}

				w = (u16)(_x * w);
				h = (u16)(_y * h);

				is_compiled = false;
			}

			virtual void set_pos(u16 _x, u16 _y)
			{
				x = _x;
				y = _y;

				is_compiled = false;
			}

			virtual void set_size(u16 _w, u16 _h)
			{
				w = _w;
				h = _h;

				is_compiled = false;
			}

			virtual void set_padding(f32 left, f32 right, f32 top, f32 bottom)
			{
				padding_left = left;
				padding_right = right;
				padding_top = top;
				padding_bottom = bottom;

				is_compiled = false;
			}

			virtual void set_padding(f32 padding)
			{
				padding_left = padding_right = padding_top = padding_bottom = padding;
				is_compiled = false;
			}

			virtual void set_text(std::string& text)
			{
				this->text = text;
				is_compiled = false;
			}

			virtual void set_text(const char* text)
			{
				this->text = text;
				is_compiled = false;
			}

			virtual void set_font(const char* font_name, u16 font_size)
			{
				font = fontmgr::get(font_name, font_size);
				is_compiled = false;
			}

			virtual std::vector<vertex> render_text(const char *string, f32 x, f32 y)
			{
				auto renderer = font;
				if (!renderer) renderer = fontmgr::get("Arial", 12);

				std::vector<vertex> result = renderer->render_text(string);
				for (auto &v : result)
				{
					//Apply transform.
					//(0, 0) has text sitting 50% off the top left corner (text is outside the rect) hence the offset by text height / 2
					v.values[0] += x + padding_left;
					v.values[1] += y + padding_top + (f32)renderer->size_px * 0.5f;
				}

				return result;
			}

			virtual compiled_resource& get_compiled()
			{
				if (!is_compiled)
				{
					compiled_resources = {};
					compiled_resources.draw_commands.push_back({});

					auto &config = compiled_resources.draw_commands.front().first;
					config.color = back_color;
					config.pulse_glow = pulse_effect_enabled;

					auto& verts = compiled_resources.draw_commands.front().second;
					verts.resize(4);
					verts[0].vec4(x + padding_left, y + padding_bottom, 0.f, 0.f);
					verts[1].vec4(x + w - padding_right, y + padding_bottom, 1.f, 0.f);
					verts[2].vec4(x + padding_left, y + h - padding_top, 0.f, 1.f);
					verts[3].vec4(x + w - padding_right, y + h - padding_top, 1.f, 1.f);

					if (!text.empty())
					{
						compiled_resources.draw_commands.push_back({});
						compiled_resources.draw_commands.back().first = font? font : fontmgr::get("Arial", 12);
						compiled_resources.draw_commands.back().first.color = fore_color;
						compiled_resources.draw_commands.back().second = render_text(text.c_str(), (f32)(x + 15), (f32)(y + 5));
					}

					is_compiled = true;
				}

				return compiled_resources;
			}

			u16 measure_text_width() const
			{
				auto renderer = font;
				if (!renderer) renderer = fontmgr::get("Arial", 12);

				return renderer->em_size * text.length();
			}
		};

		struct animation_base
		{
			float duration = 0.f;
			float t = 0.f;
			overlay_element *ref = nullptr;

			virtual void update(float /*elapsed*/) {}
			void reset() { t = 0.f; }
		};

		struct layout_container : public overlay_element
		{
			std::vector<std::unique_ptr<overlay_element>> m_items;
			u16 advance_pos = 0;
			u16 padding = 0;
			u16 scroll_offset_index = 0;
			bool auto_resize = true;

			virtual overlay_element* add_element(std::unique_ptr<overlay_element>&, int = -1) = 0;

			layout_container()
			{
				//Transparent by default
				back_color.a = 0.f;
			}

			void translate(s16 _x, s16 _y) override
			{
				overlay_element::translate(_x, _y);

				for (auto &itm : m_items)
					itm->translate(_x, _y);
			}

			void set_pos(u16 _x, u16 _y) override
			{
				s16 dx = (s16)(_x - x);
				s16 dy = (s16)(_y - y);
				translate(dx, dy);
			}

			compiled_resource& get_compiled() override
			{
				if (!is_compiled)
				{
					compiled_resource result = overlay_element::get_compiled();

					for (auto &itm : m_items)
						result.add(itm->get_compiled());

					compiled_resources = result;
				}

				return compiled_resources;
			}

			virtual u16 get_scroll_offset_px() = 0;
		};

		struct vertical_layout : public layout_container
		{
			overlay_element* add_element(std::unique_ptr<overlay_element>& item, int offset = -1)
			{
				if (auto_resize)
				{
					item->set_pos(item->x + x, h + padding + y);
					h += item->h + padding;
					w = std::max(w, item->w);
				}
				else
				{
					item->set_pos(item->x + x, advance_pos + padding + y);
					advance_pos += item->h + padding;
				}

				if (offset < 0)
				{
					m_items.push_back(std::move(item));
					return m_items.back().get();
				}
				else
				{
					auto result = item.get();
					m_items.insert(m_items.begin() + offset, std::move(item));
					return result;
				}
			}

			compiled_resource& get_compiled() override
			{
				if (scroll_offset_index == 0 && auto_resize)
					return layout_container::get_compiled();

				if (!is_compiled)
				{
					compiled_resource result = overlay_element::get_compiled();
					f32 global_y_offset = -(f32)get_scroll_offset_px();
					for (size_t index = scroll_offset_index; index < m_items.size(); ++index)
					{
						if (((f32)(m_items[index]->h + m_items[index]->y) - (f32)y + global_y_offset) > h)
						{
							//Clip tail
							break;
						}

						result.add(m_items[index]->get_compiled(), 0.f, global_y_offset);
					}

					compiled_resources = result;
				}

				return compiled_resources;
			}

			u16 get_scroll_offset_px() override
			{
				u16 result = 0;
				for (size_t index = 0; index < m_items.size(); ++index)
				{
					if (index < scroll_offset_index)
					{
						result += m_items[index]->h + padding;
						continue;
					}
				}

				return result;
			}
		};

		struct horizontal_layout : public layout_container
		{
			overlay_element* add_element(std::unique_ptr<overlay_element>& item, int offset = -1)
			{
				if (auto_resize)
				{
					item->set_pos(w + padding + x, item->y + y);
					w += item->w + padding;
					h = std::max(h, item->h);
				}
				else
				{
					item->set_pos(advance_pos + padding + x, item->y + y);
					advance_pos += item->w + padding;
				}

				if (offset < 0)
				{
					m_items.push_back(std::move(item));
					return m_items.back().get();
				}
				else
				{
					auto result = item.get();
					m_items.insert(m_items.begin() + offset, std::move(item));
					return result;
				}
			}

			compiled_resource& get_compiled() override
			{
				if (scroll_offset_index == 0 && auto_resize)
					return layout_container::get_compiled();

				if (!is_compiled)
				{
					compiled_resource result = overlay_element::get_compiled();
					f32 global_x_offset = -(f32)get_scroll_offset_px();
					for (size_t index = scroll_offset_index; index < m_items.size(); ++index)
					{
						if (((f32)(m_items[index]->w + m_items[index]->x) - (f32)x + global_x_offset) > w)
						{
							//Clip tail
							break;
						}

						result.add(m_items[index]->get_compiled(), global_x_offset, 0.f);
					}

					compiled_resources = result;
				}

				return compiled_resources;
			}

			u16 get_scroll_offset_px() override
			{
				u16 result = 0;
				for (size_t index = 0; index < m_items.size(); ++index)
				{
					if (index < scroll_offset_index)
					{
						result += m_items[index]->w + padding;
						continue;
					}
				}

				return result;
			}
		};

		//Controls
		struct spacer : public overlay_element
		{
			using overlay_element::overlay_element;
			compiled_resource& get_compiled() override
			{
				//No draw
				return compiled_resources;
			}
		};

		struct image_view : public overlay_element
		{
			using overlay_element::overlay_element;
			u8 image_resource_ref = image_resource_id::none;

			compiled_resource& get_compiled() override
			{
				if (!is_compiled)
				{
					auto &result = overlay_element::get_compiled();
					result.draw_commands.front().first = image_resource_ref;
					result.draw_commands.front().first.color = fore_color;
				}

				return compiled_resources;
			}
		};

		struct image_button : public image_view
		{
			using image_view::image_view;
			u16 text_offset = 0;

			void set_size(u16 w, u16 h) override
			{
				image_view::set_size(h, h);
				text_offset = (h / 2) + 10; //By default text is at the horizontal center
			}

			compiled_resource& get_compiled() override
			{
				if (!is_compiled)
				{
					auto& compiled = image_view::get_compiled();
					for (auto &cmd : compiled.draw_commands)
					{
						if (cmd.first.texture_ref == image_resource_id::font_file)
						{
							//Text, translate geometry to the right
							const f32 text_height = font ? font->size_px : 16.f;
							const f32 offset_y = (h > text_height) ? (f32)(h - text_height) : ((f32)h - text_height);

							for (auto &v : cmd.second)
							{
								v.values[0] += text_offset;
								v.values[1] += offset_y;
							}
						}
					}
				}

				return compiled_resources;
			}
		};

		struct label : public overlay_element
		{
			label() {}

			label(const char *text)
			{
				this->text = text;
			}
		};

		struct list_view : public vertical_layout
		{
		private:
			std::unique_ptr<image_view> m_scroll_indicator_top;
			std::unique_ptr<image_view> m_scroll_indicator_bottom;
			std::unique_ptr<image_button> m_cancel_btn;
			std::unique_ptr<image_button> m_accept_btn;
			std::unique_ptr<overlay_element> m_highlight_box;

			u16 m_elements_height = 0;
			s16 m_selected_entry = -1;
			u16 m_elements_count = 0;
		
		public:
			list_view(u16 width, u16 height)
			{
				w = width;
				h = height;

				m_scroll_indicator_top = std::make_unique<image_view>(width, 5);
				m_scroll_indicator_bottom = std::make_unique<image_view>(width, 5);
				m_accept_btn = std::make_unique<image_button>(120, 20);
				m_cancel_btn = std::make_unique<image_button>(120, 20);
				m_highlight_box = std::make_unique<overlay_element>(width, 0);

				m_scroll_indicator_top->set_size(width, 40);
				m_scroll_indicator_bottom->set_size(width, 40);
				m_accept_btn->set_size(120, 30);
				m_cancel_btn->set_size(120, 30);

				m_scroll_indicator_top->image_resource_ref = resource_config::standard_image_resource::fade_top;
				m_scroll_indicator_bottom->image_resource_ref = resource_config::standard_image_resource::fade_bottom;
				m_accept_btn->image_resource_ref = resource_config::standard_image_resource::cross;
				m_cancel_btn->image_resource_ref = resource_config::standard_image_resource::circle;

				m_scroll_indicator_bottom->set_pos(0, height - 40);
				m_accept_btn->set_pos(30, height + 20);
				m_cancel_btn->set_pos(180, height + 20);

				m_accept_btn->text = "Select";
				m_cancel_btn->text = "Cancel";

				auto fnt = fontmgr::get("Arial", 16);
				m_accept_btn->font = fnt;
				m_cancel_btn->font = fnt;

				auto_resize = false;
				back_color = { 0.15f, 0.15f, 0.15f, 0.8f };

				m_highlight_box->back_color = { .5f, .5f, .8f, 0.2f };
				m_highlight_box->pulse_effect_enabled = true;
				m_scroll_indicator_top->fore_color.a = 0.f;
				m_scroll_indicator_bottom->fore_color.a = 0.f;
			}

			void scroll_down()
			{
				if (scroll_offset_index < (m_elements_count * 2))
					scroll_offset_index += 2;
			}

			void scroll_up()
			{
				if (scroll_offset_index >= 2)
					scroll_offset_index -= 2;
				else
					scroll_offset_index = 0;
			}

			void update_selection()
			{
				auto current_element = m_items[m_selected_entry * 2].get();

				//Calculate bounds
				auto min_y = current_element->y - y;
				auto max_y = current_element->y + current_element->h + padding + 2 - y;

				auto scroll_distance = get_scroll_offset_px();

				if (min_y < scroll_distance)
				{
					scroll_up();
					scroll_distance = get_scroll_offset_px();
				}
				else if (max_y > (h + scroll_distance))
				{
					scroll_down();
					scroll_distance = get_scroll_offset_px();
				}

				if ((get_scroll_offset_px() + h) >= m_elements_height)
					m_scroll_indicator_bottom->fore_color.a = 0.f;
				else
					m_scroll_indicator_bottom->fore_color.a = 0.5f;

				if (scroll_offset_index == 0)
					m_scroll_indicator_top->fore_color.a = 0.f;
				else
					m_scroll_indicator_top->fore_color.a = 0.5f;

				m_highlight_box->set_pos(current_element->x, current_element->y);
				m_highlight_box->h = current_element->h + padding;
				m_highlight_box->y -= scroll_distance;

				m_highlight_box->refresh();
				m_scroll_indicator_top->refresh();
				m_scroll_indicator_bottom->refresh();
				refresh();
			}

			void select_next()
			{
				if (m_selected_entry < (m_elements_count - 1))
				{
					m_selected_entry++;
					update_selection();
				}
			}

			void select_previous()
			{
				if (m_selected_entry > 0)
				{
					m_selected_entry--;
					update_selection();
				}
			}

			void add_entry(std::unique_ptr<overlay_element>& entry)
			{
				//Add entry view
				add_element(entry);
				m_elements_count++;

				//Add separator
				auto separator = std::make_unique<overlay_element>();
				separator->back_color = fore_color;
				separator->w = w;
				separator->h = 2;
				add_element(separator);

				if (m_selected_entry < 0)
					m_selected_entry = 0;

				m_elements_height = advance_pos;
				update_selection();
			}

			int get_selected_index()
			{
				return m_selected_entry;
			}

			std::string get_selected_item()
			{
				if (m_selected_entry < 0)
					return{};

				return m_items[m_selected_entry]->text;
			}

			void translate(s16 _x, s16 _y) override
			{
				layout_container::translate(_x, _y);
				m_scroll_indicator_top->translate(_x, _y);
				m_scroll_indicator_bottom->translate(_x, _y);
				m_accept_btn->translate(_x, _y);
				m_cancel_btn->translate(_x, _y);
			}

			compiled_resource& get_compiled()
			{
				if (!is_compiled)
				{
					auto compiled = vertical_layout::get_compiled();
					compiled.add(m_highlight_box->get_compiled());
					compiled.add(m_scroll_indicator_top->get_compiled());
					compiled.add(m_scroll_indicator_bottom->get_compiled());
					compiled.add(m_accept_btn->get_compiled());
					compiled.add(m_cancel_btn->get_compiled());

					compiled_resources = compiled;
				}

				return compiled_resources;
			}
		};
	}
}