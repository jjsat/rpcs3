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

			int font_size;
			std::string font_face;
			std::vector<vertex> memory_resource;

			overlay_element() {}
			overlay_element(u16 _w, u16 _h) : w(_w), h(_h) {}

			//Draw into image resource
			virtual void render() {}

			virtual void translate(u16 _x, u16 _y)
			{
				x += _x;
				y += _y;
			}

			virtual void scale(f32 _x, f32 _y, bool origin_scaling)
			{
				if (origin_scaling)
				{
					x *= _x;
					y *= _y;
				}

				w *= _x;
				h *= _y;
			}

			virtual compiled_resource get_compiled()
			{
				return{};
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

			compiled_resource get_compiled() override
			{
				compiled_resource result;
				for (auto &itm : m_items)
					result.add(itm->get_compiled());

				return result;
			}
		};

		struct user_interface
		{
			u16 virtual_width = 1280;
			u16 virtual_height = 720;

			virtual void on_button_pressed(u32 button);
			virtual compiled_resource get_compiled();
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
			label *m_header_view;

			image_view *m_scroll_indicator_top;
			image_view *m_scroll_indicator_bottom;
			button *m_cancel_btn;
			button *m_accept_btn;

			u16 m_scroll_offset = 0;
			u16 m_entry_height = 16;
			u16 m_elements_height = 0;
			s16 m_selected_entry = -1;
			u16 m_elements_count;
		
		public:
			list_view(std::string& title, u16 width, u16 height, u16 entry_height)
			{
				m_entry_height = entry_height;
				w = width;
				h = height;

				std::unique_ptr<overlay_element> header = std::make_unique<label>(width, 40);
				header->text = title;
				header->font_size = 16;
				header->font_face = "Arial Bold";

				m_header_view = static_cast<label*>(add_element(header));
				m_header_view->render();

				std::unique_ptr<overlay_element> top_indicator = std::make_unique<image_view>(width, 5);
				std::unique_ptr<overlay_element> bottom_indicator = std::make_unique<image_view>(width, 5);
				std::unique_ptr<overlay_element> accept = std::make_unique<button>(120, 20);
				std::unique_ptr<overlay_element> cancel = std::make_unique<button>(120, 20);

				m_scroll_indicator_top = static_cast<image_view*>(add_element(top_indicator));
				m_scroll_indicator_bottom = static_cast<image_view*>(add_element(bottom_indicator));
				m_cancel_btn = static_cast<button*>(add_element(cancel));
				m_accept_btn = static_cast<button*>(add_element(accept));

				//TODO: Add X icon to accept and O to cancel
				//TODO: Add ^ icon to indicator top and V to indicator bottom
				m_scroll_indicator_top->render();
				m_scroll_indicator_bottom->render();
				m_accept_btn->render();
				m_cancel_btn->render();
			}

			list_view(const char* title, u16 width, u16 height, u16 entry_height)
			{
				std::string s = title;
				list_view(s, width, height, entry_height);
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
				m_list = std::make_unique<list_view>("Save Dialog", 320, 400, 16);
				m_description = std::make_unique<label>();
				m_time_thingy = std::make_unique<label>();

				m_description->font_face = "Arial";
				m_description->font_size = 14;
				m_description->text = "Save Entry 0";
				m_description->render();

				m_time_thingy->font_face = "Arial";
				m_time_thingy->font_size = 11;
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