#pragma once
#include "overlay_controls.h"

#include "../Io/PadHandler.h"
#include "Emu/Memory/vm.h"
#include "Emu/Cell/Modules/cellSaveData.h"
#include "Emu/IdManager.h"
#include "pad_thread.h"

// Definition of user interface implementations
namespace rsx
{
	namespace overlays
	{
		struct user_interface
		{
			//Move this somewhere to avoid duplication
			enum selection_code
			{
				new_save = -1,
				canceled = -2,
				error = -3
			};

			enum pad_button : u8
			{
				dpad_up = 0,
				dpad_down,
				dpad_left,
				dpad_right,
				triangle,
				circle,
				square,
				cross
			};

			u16 virtual_width = 1280;
			u16 virtual_height = 720;

			u64  input_timestamp = 0;
			bool exit = false;

			virtual compiled_resource get_compiled() = 0;

			void close();
			void refresh();

			virtual void on_button_pressed(pad_button button_press)
			{
				switch (button_press)
				{
				case pad_button::circle:
					close();
					break;
				}
			};

			s32 run_input_loop()
			{
				const auto handler = fxm::get<pad_thread>();
				if (!handler)
				{
					LOG_ERROR(RSX, "Pad handler expected but none initialized!");
					return selection_code::error;
				}

				const PadInfo& rinfo = handler->GetInfo();
				if (rinfo.max_connect == 0 || !rinfo.now_connect)
					return selection_code::error;

				std::array<bool, 8> button_state;
				button_state.fill(true);

				while (!exit)
				{
					if (Emu.IsStopped())
						return selection_code::canceled;

					if (Emu.IsPaused())
					{
						std::this_thread::sleep_for(10ms);
						continue;
					}

					for (const auto &pad : handler->GetPads())
					{
						for (auto &button : pad->m_buttons)
						{
							u8 button_id = 255;
							if (button.m_offset == CELL_PAD_BTN_OFFSET_DIGITAL1)
							{
								switch (button.m_outKeyCode)
								{
								case CELL_PAD_CTRL_LEFT:
									button_id = pad_button::dpad_left;
									break;
								case CELL_PAD_CTRL_RIGHT:
									button_id = pad_button::dpad_right;
									break;
								case CELL_PAD_CTRL_DOWN:
									button_id = pad_button::dpad_down;
									break;
								case CELL_PAD_CTRL_UP:
									button_id = pad_button::dpad_up;
									break;
								}
							}
							else if (button.m_offset == CELL_PAD_BTN_OFFSET_DIGITAL2)
							{
								switch (button.m_outKeyCode)
								{
								case CELL_PAD_CTRL_TRIANGLE:
									button_id = pad_button::triangle;
									break;
								case CELL_PAD_CTRL_CIRCLE:
									button_id = pad_button::circle;
									break;
								case CELL_PAD_CTRL_SQUARE:
									button_id = pad_button::square;
									break;
								case CELL_PAD_CTRL_CROSS:
									button_id = pad_button::cross;
									break;
								}
							}

							if (button_id < 255)
							{
								if (button.m_pressed != button_state[button_id])
									if (button.m_pressed) on_button_pressed(static_cast<pad_button>(button_id));

								button_state[button_id] = button.m_pressed;
							}

							if (button.m_flush)
							{
								button.m_pressed = false;
								button.m_flush = false;
								button.m_value = 0;
							}

							if (exit)
								return 0;
						}
					}

					refresh();
				}
			}
		};

		struct fps_display : user_interface
		{
			label m_display;

			fps_display()
			{
				m_display.w = 150;
				m_display.h = 30;
				m_display.font = fontmgr::get("Arial", 16);
				m_display.set_pos(1100, 20);
			}

			void update(std::string current_fps)
			{
				m_display.text = current_fps;
				m_display.refresh();
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
					image->set_padding(20);
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

					//Make back color transparent for text
					header_text->back_color.a = 0.f;
					subtext->back_color.a = 0.f;

					static_cast<vertical_layout*>(text_stack.get())->padding = 5;
					static_cast<vertical_layout*>(text_stack.get())->add_element(padding);
					static_cast<vertical_layout*>(text_stack.get())->add_element(header_text);
					static_cast<vertical_layout*>(text_stack.get())->add_element(subtext);

					//Pack
					add_element(image);
					add_element(text_stack);
				}
			};

			std::unique_ptr<overlay_element> m_dim_background;
			std::unique_ptr<list_view> m_list;
			std::unique_ptr<label> m_description;
			std::unique_ptr<label> m_time_thingy;

			s32 return_code = selection_code::canceled;

			save_dialog()
			{
				m_dim_background = std::make_unique<overlay_element>();
				m_dim_background->set_size(1280, 720);

				m_list = std::make_unique<list_view>(1240, 540);
				m_description = std::make_unique<label>();
				m_time_thingy = std::make_unique<label>();

				m_list->set_pos(20, 85);

				m_description->font = fontmgr::get("Arial", 20);
				m_description->set_pos(20, 50);
				m_description->text = "Save Dialog";

				m_time_thingy->font = fontmgr::get("Arial", 14);
				m_time_thingy->set_pos(1000, 40);
				m_time_thingy->text = "Sat, Jan 01, 00: 00: 00 GMT";

				m_dim_background->back_color.a = 0.8f;
			}

			void on_button_pressed(pad_button button_press) override
			{
				switch (button_press)
				{
				case pad_button::cross:
					return_code = m_list->get_selected_index();
					//Fall through
				case pad_button::circle:
					close();
					break;
				case pad_button::dpad_up:
					m_list->select_previous();
					break;
				case pad_button::dpad_down:
					m_list->select_next();
					break;
				default:
					LOG_TRACE(RSX, "[ui] Button %d pressed", (u8)button_press);
				}
			}

			compiled_resource get_compiled() override
			{
				compiled_resource result;
				result.add(m_dim_background->get_compiled());
				result.add(m_list->get_compiled());
				result.add(m_description->get_compiled());
				result.add(m_time_thingy->get_compiled());
				return result;
			}

			s32 run(std::vector<SaveDataEntry>& save_entries, u32 op, vm::ptr<CellSaveDataListSet> listSet)
			{
				auto num_actual_saves = save_entries.size();
				for (auto &entry : save_entries)
				{
					std::unique_ptr<overlay_element> e = std::make_unique<save_dialog_entry>(entry.title.c_str(), (entry.subtitle + " - " + entry.details).c_str(), resource_config::standard_image_resource::save);
					m_list->add_entry(e);
				}

				if (op >= 8)
				{
					m_description->text = "Delete Save";
				}
				else if (op & 1)
				{
					m_description->text = "Load Save";
				}
				else
				{
					m_description->text = "Create Save";
					std::unique_ptr<overlay_element> new_stub = std::make_unique<save_dialog_entry>("Create New", "Select to create a new entry", resource_config::standard_image_resource::new_entry);
					m_list->add_entry(new_stub);
				}

				if (auto err = run_input_loop())
					return err;

				if (return_code == num_actual_saves)
					return selection_code::new_save;

				return return_code;
			}
		};

		struct message_dialog : public user_interface
		{
			label text_display;
			image_button btn_ok;
			image_button btn_cancel;

			overlay_element bottom_bar, background;
			bool interactive = false;

			message_dialog()
			{
				background.set_size(1280, 720);
				background.back_color.a = 0.85f;

				text_display.set_size(1200, 40);
				text_display.set_pos(40, 350);
				text_display.set_font("Arial", 16);

				bottom_bar.back_color = color4f(1.f, 1.f, 1.f, 1.f);
				bottom_bar.set_size(1200, 2);
				bottom_bar.set_pos(40, 400);

				btn_ok.image_resource_ref = resource_config::standard_image_resource::cross;
				btn_ok.set_text("Accept");
				btn_ok.set_size(120, 30);
				btn_ok.set_pos(40, 420);
				btn_ok.set_font("Arial", 16);

				btn_cancel.image_resource_ref = resource_config::standard_image_resource::circle;
				btn_cancel.set_text("Cancel");
				btn_cancel.set_size(120, 30);
				btn_cancel.set_pos(180, 420);
				btn_cancel.set_font("Arial", 16);
			}

			compiled_resource get_compiled() override
			{
				compiled_resource result;
				result.add(background.get_compiled());
				result.add(text_display.get_compiled());
				result.add(bottom_bar.get_compiled());

				if (interactive)
				{
					result.add(btn_ok.get_compiled());
					result.add(btn_cancel.get_compiled());
				}

				return result;
			}

			void show(std::string text, bool interactive)
			{
				text_display.set_text(text.c_str());

				auto w = text_display.measure_text_width();
				auto loc = (w < virtual_width) ? (virtual_width - w) / 2 : 0;
				text_display.set_pos(loc, text_display.y);

				this->interactive = interactive;
				if (interactive)
				{
					run_input_loop();
				}
			}
		};
	}
}