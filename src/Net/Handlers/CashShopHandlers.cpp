//////////////////////////////////////////////////////////////////////////////////
//	This file is part of the continued Journey MMORPG client					//
//	Copyright (C) 2015-2019  Daniel Allendorf, Ryan Payton						//
//																				//
//	This program is free software: you can redistribute it and/or modify		//
//	it under the terms of the GNU Affero General Public License as published by	//
//	the Free Software Foundation, either version 3 of the License, or			//
//	(at your option) any later version.											//
//																				//
//	This program is distributed in the hope that it will be useful,				//
//	but WITHOUT ANY WARRANTY; without even the implied warranty of				//
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the				//
//	GNU Affero General Public License for more details.							//
//																				//
//	You should have received a copy of the GNU Affero General Public License	//
//	along with this program.  If not, see <https://www.gnu.org/licenses/>.		//
//////////////////////////////////////////////////////////////////////////////////
#include "CashShopHandlers.h"

#include "Helpers/CashShopParser.h"
#include "Helpers/ItemParser.h"

#include "../../Gameplay/Stage.h"
#include "../../IO/UI.h"
#include "../../IO/UITypes/UIChatBar.h"
#include "../../IO/UITypes/UIStatusMessenger.h"
#include "../../IO/UITypes/UIMTS.h"
#include "../../IO/Window.h"

#include <algorithm>

namespace ms
{
	static std::vector<CashShopItemEntry> s_cash_shop_items;
	static int64_t s_pending_cash_shop_withdrawal = 0;

	const std::vector<CashShopItemEntry>& get_cash_shop_items()
	{
		return s_cash_shop_items;
	}

	bool get_cash_shop_item(int64_t instance_id, CashShopItemEntry& item)
	{
		for (const CashShopItemEntry& entry : s_cash_shop_items)
			if (entry.instance_id == instance_id)
			{
				item = entry;
				return true;
			}

		return false;
	}

	void add_cash_shop_item(const CashShopItemEntry& item)
	{
		for (CashShopItemEntry& entry : s_cash_shop_items)
			if (entry.instance_id == item.instance_id)
			{
				entry = item;
				return;
			}

		s_cash_shop_items.push_back(item);
	}

	void replace_cash_shop_items(const std::vector<CashShopItemEntry>& items)
	{
		s_cash_shop_items = items;
		s_pending_cash_shop_withdrawal = 0;
	}

	void remove_cash_shop_item(int64_t instance_id)
	{
		s_cash_shop_items.erase(
			std::remove_if(s_cash_shop_items.begin(), s_cash_shop_items.end(),
				[instance_id](const CashShopItemEntry& item) { return item.instance_id == instance_id; }),
			s_cash_shop_items.end());
	}

	int64_t get_pending_cash_shop_withdrawal()
	{
		return s_pending_cash_shop_withdrawal;
	}

	void set_pending_cash_shop_withdrawal(int64_t instance_id)
	{
		s_pending_cash_shop_withdrawal = instance_id;
	}

	void SetCashShopHandler::handle(InPacket& recv) const
	{
		CashShopParser::parseCharacterInfo(recv);

		recv.skip_byte();	// Not MTS
		recv.skip_string();	// account_name
		recv.skip_int();

		int16_t specialcashitem_size = recv.read_short();

		for (size_t i = 0; i < specialcashitem_size; i++)
		{
			recv.skip_int();	// sn
			recv.skip_int();	// mod
			recv.skip_byte();	// info
		}

		recv.skip(121);

		for (size_t cat = 1; cat <= 8; cat++)
		{
			for (size_t gender = 0; gender < 2; gender++)
			{
				for (size_t in = 0; in < 5; in++)
				{
					recv.skip_int(); // category
					recv.skip_int(); // gender
					recv.skip_int(); // commoditysn
				}
			}
		}

		recv.skip_int();
		recv.skip_short();
		recv.skip_byte();
		recv.skip_int();

		transition();
	}

	void CashShopOperationHandler::handle(InPacket& recv) const
	{
		int8_t operation = recv.read_byte();

		switch (operation)
		{
		case 0x4B:
		{
			// Authoritative cash shop locker snapshot sent when entering the shop.
			const uint16_t count = static_cast<uint16_t>(recv.read_short());
			std::vector<CashShopItemEntry> items;
			items.reserve(count);
			for (uint16_t i = 0; i < count; i++)
			{
				CashShopItemEntry item;
				item.instance_id = recv.read_long();
				recv.read_int(); // account id
				recv.read_int(); // character id
				item.item_id = recv.read_int();
				item.commodity_sn = recv.read_int();
				item.count = static_cast<uint16_t>(recv.read_short());
				recv.skip(13); // sender
				item.expiration = recv.read_long();
				recv.read_long(); // reserved
				items.push_back(item);
			}
			recv.read_short(); // storage slots
			recv.read_short(); // character slots
			replace_cash_shop_items(items);
			break;
		}
		case 0x4A:
		{
			// Some server revisions send this short success acknowledgement.
			chat::log("[Cash Shop] Item purchased successfully!", chat::LineType::YELLOW);
			break;
		}
		case 0x57:
		{
			// Purchase confirmation carries one cash-item record:
			// instance id, account id, character id, item id, commodity SN,
			// count, sender, expiration, and a reserved long.
			CashShopItemEntry item;
			item.instance_id = recv.read_long();
			recv.read_int(); // account id
			recv.read_int(); // character id
			item.item_id = recv.read_int();
			item.commodity_sn = recv.read_int();
			item.count = static_cast<uint16_t>(recv.read_short());
			recv.skip(13); // sender
			item.expiration = recv.read_long();
			recv.read_long(); // reserved

			add_cash_shop_item(item);
			chat::log("[Cash Shop] Item purchased successfully!", chat::LineType::YELLOW);
			break;
		}
		case 0x68:
		{
			// Successful locker withdrawal returns the destination character slot
			// followed by the complete inventory item record.
			const int16_t slot = recv.read_short();
			const int64_t instance_id = get_pending_cash_shop_withdrawal();
			CashShopItemEntry item;
			if (instance_id == 0 || !get_cash_shop_item(instance_id, item))
			{
				chat::log("[Cash Shop] Received an item transfer without a matching locker item.", chat::LineType::RED);
				break;
			}

			Inventory& inventory = Stage::get().get_player().get_inventory();
			ItemParser::parse_item(recv, InventoryType::by_item_id(item.item_id), slot, inventory);
			remove_cash_shop_item(instance_id);
			set_pending_cash_shop_withdrawal(0);
			chat::log("[Cash Shop] Item moved to your inventory.", chat::LineType::YELLOW);
			break;
		}
		case 0x4C:
		{
			int8_t reason = recv.read_byte();
			std::string msg;
			switch (reason)
			{
			case 0: msg = "Unknown error."; break;
			case 1: msg = "You don't have enough NX."; break;
			case 2: msg = "Item is out of stock."; break;
			case 3: msg = "Your cash inventory is full."; break;
			case 4: msg = "This item is not available for purchase."; break;
			case 5: msg = "You have exceeded the purchase limit."; break;
			case 6: msg = "You are under the required level."; break;
			case 7: msg = "You already have this item."; break;
			default: msg = "Purchase failed (code: " + std::to_string(reason) + ")"; break;
			}
			chat::log("[Cash Shop] " + msg, chat::LineType::RED);
			break;
		}
		case 0x59:
		{
			chat::log("[Cash Shop] Coupon redeemed successfully!", chat::LineType::YELLOW);
			break;
		}
		case 0x5C:
		{
			int8_t reason = recv.read_byte();
			std::string msg;
			switch (reason)
			{
			case 0: msg = "Invalid coupon code."; break;
			case 1: msg = "This coupon has expired."; break;
			case 2: msg = "This coupon has already been used."; break;
			case 3: msg = "This coupon is for a different server."; break;
			default: msg = "Coupon error (code: " + std::to_string(reason) + ")"; break;
			}
			chat::log("[Cash Shop] " + msg, chat::LineType::RED);
			break;
		}
		default:
			break;
		}
	}

	void SetITCHandler::handle(InPacket& recv) const
	{
		// SET_ITC — MTS transition
		// Same as SET_CASH_SHOP but with MTS trailing data instead of CS items
		CashShopParser::parseCharacterInfo(recv);

		recv.skip_string();	// account_name

		// MTS config bytes (hardcoded by Cosmic)
		recv.skip(28);

		// Open the MTS UI
		UI::get().emplace<UIMTS>();
	}

	void SetCashShopHandler::transition() const
	{
		// The cash shop art is authored for a 1024x768 canvas. Keep the actual
		// window dimensions unchanged and fit that canvas inside the window.
		Constants::Constants::get().set_viewport_size(1024, 768);
		Window::get().update_viewport();

		// Drop any focused textfield (e.g. the chat bar) so the arrow
		// keys reach the cash shop stage character
		UI::get().remove_textfield();

		float fadestep = 0.025f;

		Window::get().fadeout(
			fadestep,
			[]()
			{
				GraphicsGL::get().clear();

				Stage::get().load(-1, 0);

				GraphicsGL::get().unlock();

				UI::get().change_state(UI::State::CASHSHOP);
				UI::get().enable();
				Timer::get().start();
			}
		);

		GraphicsGL::get().lock();
		Stage::get().clear();
		Timer::get().start();
	}

	static int32_t s_cash_balances[3] = { 0, 0, 0 };

	int32_t get_cash_balance(int which)
	{
		return (which >= 0 && which < 3) ? s_cash_balances[which] : 0;
	}

	void QueryCashResultHandler::handle(InPacket& recv) const
	{
		// Cash query result — the balances render in the cash shop's
		// left panel next to the baked NX labels
		if (recv.available())
		{
			s_cash_balances[0] = recv.read_int(); // NX credit
			s_cash_balances[1] = recv.read_int(); // maple points
			s_cash_balances[2] = recv.read_int(); // NX prepaid
		}
	}

	void CashShopNameChangeHandler::handle(InPacket& recv) const
	{
		std::string name = recv.read_string();
		bool in_use = recv.read_bool();

		if (auto messenger = UI::get().get_element<UIStatusMessenger>())
		{
			if (in_use)
				messenger->show_status(Color::Name::RED, "Name '" + name + "' is already in use.");
			else
				messenger->show_status(Color::Name::WHITE, "Name '" + name + "' is available!");
		}
	}

	void CashShopNameChangePossibleHandler::handle(InPacket& recv) const
	{
		recv.read_int(); // 0
		int8_t error = recv.read_byte();
		recv.read_int(); // 0

		if (error == 0)
		{
			if (auto messenger = UI::get().get_element<UIStatusMessenger>())
				messenger->show_status(Color::Name::WHITE, "Name change is available.");
		}
		else
		{
			std::string msg;

			switch (error)
			{
			case 1: msg = "A name change has already been submitted."; break;
			case 2: msg = "Must wait at least one month between name changes."; break;
			case 3: msg = "Cannot change name due to a recent ban."; break;
			default: msg = "Name change is not available."; break;
			}

			if (auto messenger = UI::get().get_element<UIStatusMessenger>())
				messenger->show_status(Color::Name::RED, msg);
		}
	}

	void CashShopTransferWorldHandler::handle(InPacket& recv) const
	{
		recv.read_int(); // 0
		int8_t error = recv.read_byte();
		recv.read_int(); // 0
		bool ok = recv.read_bool();

		if (ok)
		{
			int32_t world_count = recv.read_int();
			std::string world_list = "[CashShop] Available worlds: ";

			for (int32_t i = 0; i < world_count; i++)
			{
				std::string world_name = recv.read_string();

				if (i > 0)
					world_list += ", ";

				world_list += world_name;
			}

			chat::log(world_list, chat::LineType::YELLOW);
		}
		else if (error != 0)
		{
			if (auto messenger = UI::get().get_element<UIStatusMessenger>())
				messenger->show_status(Color::Name::RED, "World transfer failed (error=" + std::to_string(error) + ").");
		}
	}

	void CashGachaponResultHandler::handle(InPacket& recv) const
	{
		int8_t op = recv.read_byte();

		if (op == (int8_t)0xE4) // Open failed
		{
			if (auto messenger = UI::get().get_element<UIStatusMessenger>())
				messenger->show_status(Color::Name::RED, "Gachapon failed to open.");
		}
		else if (op == (int8_t)0xE5) // Open success
		{
			recv.read_long(); // box cash id
			int32_t remaining = recv.read_int();

			chat::log("[Gachapon] Opened! Remaining: " + std::to_string(remaining), chat::LineType::YELLOW);
			// CashItemInfo + reward data follows
		}
	}
}
