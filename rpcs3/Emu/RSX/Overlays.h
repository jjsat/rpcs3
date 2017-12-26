#pragma once
#include "Utilities/types.h"
#include "../Io/PadHandler.h"

#include <string>
#include <vector>
#include <memory>

// STB_IMAGE_IMPLEMENTATION defined externally
#include <stb_image.h>
#include <stb_truetype.h>

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
		};

		struct font
		{
			const u32 width = 1024;
			const u32 height = 1024;
			const u32 oversample = 2;
			const u32 char_count = 256; //16x16 grid at max 48pt

			u32 size_pt = 12;
			u32 size_px = 16; //Default font 12pt size
			std::string font_name;
			std::vector<stbtt_packedchar> pack_info;
			std::vector<u8> glyph_data;
			bool initialized = false;

			font(const char *ttf_name, int size)
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
				size_px = (u32)ceil((double)size * 96. / 72.);
				size_pt = size;

				if (!stbtt_PackFontRange(&context, bytes.data(), 0, size_px, 0, 256, pack_info.data()))
				{
					LOG_ERROR(RSX, "Font packing failed");
					stbtt_PackEnd(&context);
					return;
				}

				stbtt_PackEnd(&context);

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
				f32 x_advance, y_advance;

				while (true)
				{
					if (char c = text[i++])
					{
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

				fonts.push_back(std::make_unique<font>(name, size));
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
					data = stbi_load_from_memory(bytes.data(), f.size(), &w, &h, &bpp, STBI_rgb_alpha);
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
				arrow_up = 1,
				arrow_down,
				scroll_indicator,
				cross,
				circle
			};

			//Define resources
			std::vector<std::string> texture_resource_files;
			std::vector<std::unique_ptr<image_info>> texture_raw_data;

			resource_config()
			{
				texture_resource_files.push_back("arrow_up.png");
				texture_resource_files.push_back("arrow_down.png");
				texture_resource_files.push_back("scroll_indicator.png");
				texture_resource_files.push_back("cross.png");
				texture_resource_files.push_back("circle.png");
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
		};

		struct overlay_element
		{
			u16 x = 0;
			u16 y = 0;
			u16 w = 0;
			u16 h = 0;

			std::string text;
			font* font = nullptr;

			compiled_resource compiled_resources;
			bool is_compiled = false;

			overlay_element() {}
			overlay_element(u16 _w, u16 _h) : w(_w), h(_h) {}

			//Draw into image resource
			virtual void render() {}

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

			virtual std::vector<vertex> render_text(const char *string, f32 x, f32 y)
			{
				auto renderer = font;
				if (!renderer) renderer = fontmgr::get("Arial", 12);

				std::vector<vertex> result = renderer->render_text(string);
				for (auto &v : result)
				{
					//Apply transform.
					//(0, 0) has text sitting 50% off the top left corner (text is outside the rect) hence the offset by text height / 2
					v.values[0] += x;
					v.values[1] += y + (f32)renderer->size_px * 0.5f;
				}

				return result;
			}

			virtual compiled_resource& get_compiled()
			{
				if (!is_compiled)
				{
					compiled_resources = {};
					compiled_resources.draw_commands.push_back({});

					auto& verts = compiled_resources.draw_commands.front().second;
					verts.resize(4);
					verts[0].vec4(x, y, 0.f, 0.f);
					verts[1].vec4(x + w, y, 1.f, 0.f);
					verts[2].vec4(x, y + h, 0.f, 1.f);
					verts[3].vec4(x + w, y + h, 1.f, 1.f);

					if (!text.empty())
					{
						compiled_resources.draw_commands.push_back({});
						compiled_resources.draw_commands.back().first = font? font : fontmgr::get("Arial", 12);
						compiled_resources.draw_commands.back().second = render_text(text.c_str(), x + 15, y + 5);
					}

					is_compiled = true;
				}

				return compiled_resources;
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
			bool auto_resize = true;

			virtual overlay_element* add_element(std::unique_ptr<overlay_element>&, int = -1) = 0;

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
		};

		struct user_interface
		{
			u16 virtual_width = 1280;
			u16 virtual_height = 720;

			virtual void on_button_pressed(u32 button) = 0;
			virtual compiled_resource get_compiled() = 0;
		};

		struct vertical_layout : public layout_container
		{
			overlay_element* add_element(std::unique_ptr<overlay_element>& item, int offset = -1)
			{
				if (auto_resize)
				{
					item->set_pos(item->x + x, h + y);
					h += item->h;
					w = std::max(w, item->w);
				}
				else
				{
					item->set_pos(item->x + x, advance_pos + y);
					advance_pos += item->h;
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
		};

		struct horizontal_layout : public layout_container
		{
			overlay_element* add_element(std::unique_ptr<overlay_element>& item, int offset = -1)
			{
				if (auto_resize)
				{
					item->set_pos(w + x, item->y + y);
					w += item->w;
					h = std::max(h, item->h);
				}
				else
				{
					item->set_pos(advance_pos + x, item->y + y);
					advance_pos += item->w;
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
				text_offset = h;
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
							const u16 text_height = font ? font->size_px : 16;
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

			u16 m_scroll_offset = 0;
			u16 m_entry_height = 16;
			u16 m_elements_height = 0;
			s16 m_selected_entry = -1;
			u16 m_elements_count = 0;
		
		public:
			list_view(u16 width, u16 height, u16 entry_height)
			{
				m_entry_height = entry_height;
				w = width;
				h = height;

				m_scroll_indicator_top = std::make_unique<image_view>(width, 5);
				m_scroll_indicator_bottom = std::make_unique<image_view>(width, 5);
				m_accept_btn = std::make_unique<image_button>(120, 20);
				m_cancel_btn = std::make_unique<image_button>(120, 20);

				m_scroll_indicator_top->set_size(width, 20);
				m_scroll_indicator_bottom->set_size(width, 20);
				m_accept_btn->set_size(120, 30);
				m_cancel_btn->set_size(120, 30);

				m_scroll_indicator_top->image_resource_ref = resource_config::standard_image_resource::arrow_up;
				m_scroll_indicator_bottom->image_resource_ref = resource_config::standard_image_resource::arrow_down;
				m_accept_btn->image_resource_ref = resource_config::standard_image_resource::circle;
				m_cancel_btn->image_resource_ref = resource_config::standard_image_resource::cross;

				m_scroll_indicator_top->translate(0, -20);
				m_scroll_indicator_bottom->set_pos(0, height);
				m_accept_btn->set_pos(0, height + 30);
				m_cancel_btn->set_pos(150, height + 30);

				m_accept_btn->text = "Accept";
				m_cancel_btn->text = "Cancel";

				auto fnt = fontmgr::get("Arial", 16);
				m_accept_btn->font = fnt;
				m_cancel_btn->font = fnt;

				auto_resize = false;
			}

			void scroll_down()
			{
				if (m_scroll_offset < m_elements_height)
					m_scroll_offset += m_entry_height;
			}

			void scroll_up()
			{
				if (m_scroll_offset)
					m_scroll_offset -= m_entry_height;
			}

			void add_entry(std::unique_ptr<overlay_element>& entry)
			{
				add_element(entry);
				m_elements_count++;
				m_elements_height += m_entry_height;
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
					auto compiled = layout_container::get_compiled();
					compiled.add(m_scroll_indicator_top->get_compiled());
					compiled.add(m_scroll_indicator_bottom->get_compiled());
					compiled.add(m_accept_btn->get_compiled());
					compiled.add(m_cancel_btn->get_compiled());

					compiled_resources = compiled;
				}

				return compiled_resources;
			}
		};

		struct fps_display : user_interface
		{
			label m_display;

			fps_display()
			{
				m_display.w = 150;
				m_display.h = 30;
			}

			void update(std::string current_fps)
			{
				m_display.text = current_fps;
				m_display.render();
			}

			compiled_resource get_compiled() override
			{
				return m_display.get_compiled();
			}
		};

		struct save_dialog : public user_interface
		{
			struct save_dialog_entry : horizontal_layout
			{
				save_dialog_entry(const char* text1, const char* text2, u8 image_resource_id)
				{
					std::unique_ptr<overlay_element> image = std::make_unique<image_view>();
					image->set_size(100, 100);
					static_cast<image_view*>(image.get())->image_resource_ref = image_resource_id;

					std::unique_ptr<overlay_element> text_stack = std::make_unique<vertical_layout>();
					std::unique_ptr<overlay_element> padding = std::make_unique<spacer>();
					std::unique_ptr<overlay_element> header_text = std::make_unique<label>(text1);
					std::unique_ptr<overlay_element> subtext = std::make_unique<label>(text2);

					padding->set_size(1, 10);
					header_text->set_size(200, 40);
					header_text->text = text1;
					header_text->font = fontmgr::get("Arial", 16);
					subtext->set_size(200, 40);
					subtext->text = text2;
					subtext->font = fontmgr::get("Arial", 14);

					static_cast<vertical_layout*>(text_stack.get())->add_element(padding);
					static_cast<vertical_layout*>(text_stack.get())->add_element(header_text);
					static_cast<vertical_layout*>(text_stack.get())->add_element(subtext);

					//Pack
					add_element(image);
					add_element(text_stack);
				}
			};

			std::unique_ptr<list_view> m_list;
			std::unique_ptr<label> m_description;
			std::unique_ptr<label> m_time_thingy;

			save_dialog()
			{
				m_list = std::make_unique<list_view>(1240, 500, 16);
				m_description = std::make_unique<label>();
				m_time_thingy = std::make_unique<label>();

				m_list->set_pos(20, 120);

				m_description->font = fontmgr::get("Arial", 20);
				m_description->set_pos(20, 50);
				m_description->text = "Save Dialog";
				m_description->render();

				m_time_thingy->font = fontmgr::get("Arial", 12);
				m_time_thingy->set_pos(1000, 20);
				m_time_thingy->text = "Sat, Jan 01, 00: 00: 00 GMT";
				m_time_thingy->render();

				std::unique_ptr<overlay_element> entry1 = std::make_unique<save_dialog_entry>("Entry 1", "Sub-entry 1", resource_config::standard_image_resource::cross);
				std::unique_ptr<overlay_element> entry2 = std::make_unique<save_dialog_entry>("Entry 2", "Sub-entry 2", resource_config::standard_image_resource::circle);
				m_list->add_entry(entry1);
				m_list->add_entry(entry2);
			}

			void add_entries(std::vector<std::string>& entries)
			{
				//TODO: Should receive save file list
			}

			void on_button_pressed(u32 button) override
			{
				//TODO
			}

			compiled_resource get_compiled() override
			{
				compiled_resource result;
				result.add(m_list->get_compiled());
				result.add(m_description->get_compiled());
				result.add(m_time_thingy->get_compiled());
				return result;
			}
		};
	}
}