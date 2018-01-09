#pragma once

#include "stdafx.h"
#include "Emu/Memory/Memory.h"
#include "Emu/Cell/Modules/cellSaveData.h"
#include "Emu/Cell/PPUThread.h"

class save_data_dialog : public SaveDialogBase
{
public:
	virtual s32 ShowSaveDataList(ppu_thread& ppu, std::vector<SaveDataEntry>& save_entries, s32 focused, u32 op, vm::ptr<CellSaveDataListSet> listSet) override;
};
