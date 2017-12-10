#pragma once
#include "Utilities/types.h"
#include "../Io/PadHandler.h"

#include <string>
#include <vector>
#include <memory>

namespace rsx
{
	namespace overlays
	{
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

		struct compiled_resource
		{
			std::vector<std::pair<u64, std::vector<vertex>>> draw_commands;

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

			int font_size = 14;
			std::string font_face;

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

			virtual std::vector<vertex> render_text(const char *string, f32 x, f32 y, f32 font_w, f32 font_h)
			{
				std::vector<vertex> result;
				int index = 0;
				f32 x_loc = x;
				bool first = true;

				while (true)
				{
					char c = string[index++];
					if (!c) break;

					//TODO: Glyph metrics
					if (first)
					{
						result.push_back({ x_loc, y + font_h });
						result.push_back({ x_loc, y });
						result.push_back({ x_loc + font_w, y + font_h });
						result.push_back({ x_loc + font_w, y });

						first = false;
					}
					else
					{
						//Stripify
						result.push_back({ x_loc + font_w, y + font_h });
						result.push_back({ x_loc + font_w, y});
					}

					x_loc += font_w;;
				}

				return result;
			}

			virtual compiled_resource get_compiled()
			{
				if (!is_compiled)
				{
					compiled_resources = {};
					compiled_resources.draw_commands.push_back({});

					auto& verts = compiled_resources.draw_commands.front().second;
					verts.resize(4);
					verts[0].vec2(x, y);
					verts[1].vec2(x + w, y);
					verts[2].vec2(x, y + h);
					verts[3].vec2(x + w, y + h);

					if (!text.empty())
					{
						compiled_resources.draw_commands.push_back({});
						f32 font_height = (f32)font_size * 1.3333f;
						f32 font_width = font_height * 0.5f;
						compiled_resources.draw_commands.back().second = render_text(text.c_str(), x + 15, y + 5, font_width, font_height);
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

			compiled_resource get_compiled() override
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
			u16 m_vertical_pos = 0;

			overlay_element* add_element(std::unique_ptr<overlay_element>& item, int offset = -1)
			{
				item->y = m_vertical_pos;
				m_vertical_pos += item->h;

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
			u16 m_horizontal_pos = 0;

			overlay_element* add_element(std::unique_ptr<overlay_element>& item, int offset = -1)
			{
				item->x = m_horizontal_pos;
				m_horizontal_pos += item->w;

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
		};

		struct image_view : public overlay_element
		{
			using overlay_element::overlay_element;
		};

		struct button : public overlay_element
		{
			using overlay_element::overlay_element;
		};

		struct label : public overlay_element
		{
			using overlay_element::overlay_element;
		};

		struct list_view : public vertical_layout
		{
		private:
			std::unique_ptr<image_view> m_scroll_indicator_top;
			std::unique_ptr<image_view> m_scroll_indicator_bottom;
			std::unique_ptr<button> m_cancel_btn;
			std::unique_ptr<button> m_accept_btn;

			u16 m_scroll_offset = 0;
			u16 m_entry_height = 16;
			u16 m_elements_height = 0;
			s16 m_selected_entry = -1;
			u16 m_elements_count;
		
		public:
			list_view(u16 width, u16 height, u16 entry_height)
			{
				m_entry_height = entry_height;
				w = width;
				h = height;

				m_scroll_indicator_top = std::make_unique<image_view>(width, 5);
				m_scroll_indicator_bottom = std::make_unique<image_view>(width, 5);
				m_accept_btn = std::make_unique<button>(120, 20);
				m_cancel_btn = std::make_unique<button>(120, 20);

				m_scroll_indicator_top->set_size(width, 20);
				m_scroll_indicator_bottom->set_size(width, 20);
				m_accept_btn->set_size(120, 30);
				m_cancel_btn->set_size(120, 30);

				m_scroll_indicator_top->translate(0, -20);
				m_scroll_indicator_bottom->set_pos(0, height);
				m_accept_btn->set_pos(0, height + 30);
				m_cancel_btn->set_pos(150, height + 30);
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

			void add_entry(std::string& text)
			{
				std::unique_ptr<overlay_element> entry = std::make_unique<label>(w, m_entry_height);
				entry->text = text;
				entry->font_size = 12;
				entry->font_face = "Arial";
				entry->render();

				add_element(entry, m_elements_count + 2);
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

				return m_items[m_selected_entry + 2]->text;
			}

			void translate(s16 _x, s16 _y) override
			{
				layout_container::translate(_x, _y);
				m_scroll_indicator_top->translate(_x, _y);
				m_scroll_indicator_bottom->translate(_x, _y);
				m_accept_btn->translate(_x, _y);
				m_cancel_btn->translate(_x, _y);
			}

			compiled_resource get_compiled()
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
			std::unique_ptr<list_view> m_list;
			std::unique_ptr<label> m_description;
			std::unique_ptr<label> m_time_thingy;

			save_dialog()
			{
				m_list = std::make_unique<list_view>(1240, 500, 16);
				m_description = std::make_unique<label>();
				m_time_thingy = std::make_unique<label>();

				m_list->set_pos(20, 120);

				m_description->font_face = "Arial";
				m_description->font_size = 20;
				m_description->set_pos(20, 50);
				m_description->text = "Save Dialog";
				m_description->render();

				m_time_thingy->font_face = "Arial";
				m_time_thingy->font_size = 11;
				m_time_thingy->set_pos(1000, 20);
				m_time_thingy->text = "Sat, Jan 01, 00: 00: 00 GMT";
				m_time_thingy->render();
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

		struct resource_config
		{
			//Define resources
			std::vector<std::string> texture_resource_files;

			resource_config()
			{
				texture_resource_files.push_back("cross.png");
				texture_resource_files.push_back("circle.png");
			}
		};
	}
}