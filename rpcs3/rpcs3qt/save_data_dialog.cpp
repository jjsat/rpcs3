#include "save_data_dialog.h"
#include "save_data_list_dialog.h"

#include <Emu/Cell/lv2/sys_sync.h>
#include <Emu/IdManager.h>
#include <Emu/RSX/GSRender.h>

s32 save_data_dialog::ShowSaveDataList(ppu_thread& ppu, std::vector<SaveDataEntry>& save_entries, s32 focused, u32 op, vm::ptr<CellSaveDataListSet> listSet)
{
	//TODO: Install native shell as an Emu callback
	if (auto rsxthr = fxm::get<GSRender>())
	{
		if (auto native_dlg = rsxthr->shell_open_save_dialog())
		{
			s32 result;
			
			thread_ctrl::spawn("save dialog thread", [&]
			{
				result = native_dlg->show(save_entries, op, listSet);
				lv2_obj::awake(ppu);
			});

			lv2_obj::sleep(ppu);

			while (!ppu.state.test_and_reset(cpu_flag::signal))
			{
				thread_ctrl::wait();
			}

			if (result != rsx::overlays::user_interface::selection_code::error)
				return result;
		}
	}

	//Fall back to front-end GUI
	atomic_t<s32> selection;

	Emu.CallAfter([&]()
	{
		save_data_list_dialog sdid(save_entries, focused, op, listSet);
		sdid.exec();
		selection = sdid.GetSelection();
		lv2_obj::awake(ppu);
	});

	lv2_obj::sleep(ppu);

	while (!ppu.state.test_and_reset(cpu_flag::signal))
	{
		thread_ctrl::wait();
	}

	return selection.load();
}
