//////////////////////////////////////////////////////////////////////////////////
//	This file is part of the continued Journey MMORPG client					//
//	Copyright (C) 2015-2019  Daniel Allendorf, Ryan Payton						//
//																//
//	This program is free software: you can redistribute it and/or modify		//
//	it under the terms of the GNU Affero General Public License as published by//
//	the Free Software Foundation, either version 3 of the License, or			//
//	(at your option) any later version.											//
//																//
//	This program is distributed in the hope that it will be useful,				//
//	but WITHOUT ANY WARRANTY; without even the implied warranty of				//
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the				//
//	GNU Affero General Public License for more details.							//
//																//
//	You should have received a copy of the GNU Affero General Public License	//
//	along with this program.  If not, see <https://www.gnu.org/licenses/>.		//
//////////////////////////////////////////////////////////////////////////////////
#include "UICashShop.h"

#include "../KeyAction.h"
#include "../UI.h"
#include "../Window.h"
#include "UIChatBar.h"

#include "../Components/MapleButton.h"

#include "../../Constants.h"
#include "../../Configuration.h"
#include "../../Gameplay/Stage.h"

#include "../../Net/Handlers/CashShopHandlers.h"
#include "../../Net/Packets/CashShopPackets.h"
#include "../../Net/Packets/GameplayPackets.h"
#include "../../Net/Packets/LoginPackets.h"
#include "../../Util/Misc.h"

#include <algorithm>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef USE_NX
#include <nlnx/nx.hpp>
#endif

namespace ms
{
	UICashShop::UICashShop()
		: page(0), selected_item(-1), category(ALL_ITEMS),
		  category_active_bg(144, 24, Color::Name::MALIBU, 0.24f),
		  category_hover_bg(144, 24, Color::Name::LIGHTGREY, 0.16f),
		  card_selected_bg(CARD_W, CARD_H, Color::Name::MALIBU, 0.15f),
		  coupon_shade(1024, 768, Color::Name::BLACK, 0.64f),
		  char_x(888.0f), char_yoff(0.0f), char_vy(0.0f), char_jumping(false),
		  facing_right(true), exit_pending(false),
		  tooltip_item_id(0),
		  return_mapid(Stage::get().get_player().get_stats().get_mapid()),
		  return_portalid(Stage::get().get_player().get_stats().get_portal()),
		  cur_stance(Stance::Id::STAND1), overlay(OVERLAY_NONE)
	{
		const nl::node cash_shop_gl = nl::nx::ui["CashShopGL.img"];
		const nl::node base_frame = cash_shop_gl["BaseFrame"];
		const nl::node main_menu = cash_shop_gl["MainMenu"];
		const nl::node main_item = main_menu["MainItem"];

		// Use the same server commodity table as the purchase packet, so every
		// card shown here has a real SN and the price the server expects.
		for (nl::node entry : nl::nx::etc["Commodity.img"])
		{
			if (static_cast<int32_t>(entry["OnSale"]) != 1)
				continue;

			const int32_t item_id = entry["ItemId"];
			if (!ItemData::get(item_id).is_valid())
				continue;

			items.emplace_back(item_id, static_cast<int32_t>(entry["SN"]),
				static_cast<int32_t>(entry["Price"]), static_cast<uint16_t>(entry["Count"]));
		}

		// The English full-screen background already includes the three right
		// inventory panels and the NX balance labels. The catalog gets its own
		// framed work area, kept inside the 596px center column.
		sprites.emplace_back(base_frame["background"]);
		catalog_frame = Sprite(main_menu["Home"]["background"], DrawArgument(Point<int16_t>(170, 31)));
		item_base = Texture(main_item["background"]);
		search_bg = Texture(base_frame["CSItemSearch"]["background"]);

		// The title bar artwork uses negative origins to place controls at their
		// authored 1024x768 coordinates. Keeping the default button position lets
		// those origins do the positioning.
		buttons[BtExit] = std::make_unique<MapleButton>(base_frame["BtExit"]);
		buttons[BtHelp] = std::make_unique<MapleButton>(base_frame["BtHelp"]);
		buttons[BtCoupon] = std::make_unique<MapleButton>(base_frame["BtCoupon"]);
		// The search button's negative origin places it at its authored
		// header coordinate. The charge button's positive layout position
		// compensates for its -85px vertical origin and keeps it above balances.
		buttons[BtSearch] = std::make_unique<MapleButton>(base_frame["CSItemSearch"]["BtGo"]);
		buttons[BtChargeNX] = std::make_unique<MapleButton>(cash_shop_gl["LeftMenu"]["MyMenu"]["BtChageNx"], Point<int16_t>(27, 525));

		const std::string categories[CATEGORY_COUNT] =
		{
			"All items", "Equipment", "Use", "Set-up", "Etc.", "Cash",
			"Pet", "Pet Equips", "Pet Misc"
		};
		for (uint8_t i = 0; i < CATEGORY_COUNT; i++)
			category_labels[i] = Text(Text::Font::A11M, Text::Alignment::LEFT, Color::Name::MINESHAFT, categories[i]);

		for (size_t i = 0; i < PAGE_SIZE; i++)
		{
			const div_t cell = std::div(static_cast<int>(i), GRID_COLS);
			const Point<int16_t> card_pos(
				GRID_X + STRIDE_X * cell.rem,
				GRID_Y + STRIDE_Y * cell.quot);

			buttons[BtBuy + i] = std::make_unique<MapleButton>(main_item["BtBuy"],
				card_pos + Point<int16_t>(7, 39));
			item_name[i] = Text(Text::Font::A11B, Text::Alignment::LEFT, Color::Name::MINESHAFT, "", 98);
			item_price[i] = Text(Text::Font::A11B, Text::Alignment::LEFT, Color::Name::ENDEAVOUR, "", 98);
			item_count[i] = Text(Text::Font::A11M, Text::Alignment::LEFT, Color::Name::DUSTYGRAY, "", 98);
		}

		search_field = Textfield(Text::Font::A11M, Text::Alignment::LEFT, Color::Name::MINESHAFT,
			Rectangle<int16_t>(Point<int16_t>(483, 9), Point<int16_t>(674, 23)), 40);
		search_field.set_enter_callback([this](std::string query)
		{
			applied_search = query;
			rebuild_filter();
		});
		search_field.set_key_callback(KeyAction::Id::ESCAPE, [this]()
		{
			UI::get().remove_textfield();
			search_field.set_state(Textfield::State::NORMAL);
		});

		const nl::node popup = cash_shop_gl["Popup"];
		coupon_bg = Texture(popup["Cupon"]["background"]);
		help_bg = Texture(cash_shop_gl["PicturePlate"]["HowTo"]);
		buttons[BtCouponSubmit] = std::make_unique<MapleButton>(popup["Cupon"]["BtOk"], Point<int16_t>(453, 397));
		buttons[BtCouponCancel] = std::make_unique<MapleButton>(popup["Cupon"]["BtCancel"], Point<int16_t>(515, 397));
		buttons[BtHelpClose] = std::make_unique<MapleButton>(cash_shop_gl["PicturePlate"]["BtClose"], Point<int16_t>(714, 192));
		buttons[BtCouponSubmit]->set_active(false);
		buttons[BtCouponCancel]->set_active(false);
		buttons[BtHelpClose]->set_active(false);

		coupon_field = Textfield(Text::Font::A11M, Text::Alignment::LEFT, Color::Name::MINESHAFT,
			Rectangle<int16_t>(Point<int16_t>(386, 378), Point<int16_t>(638, 392)), 64);
		coupon_field.set_enter_callback([this](std::string code)
		{
			if (!code.empty())
				CouponCodePacket(code).dispatch();
			close_overlay();
		});
		coupon_field.set_key_callback(KeyAction::Id::ESCAPE, [this]() { close_overlay(); });

		for (int i = 0; i < 3; i++)
			cash_balance_text[i] = Text(Text::Font::A11B, Text::Alignment::RIGHT, Color::Name::MINESHAFT);

		player_name = Text(Text::Font::A11B, Text::Alignment::LEFT, Color::Name::MINESHAFT,
			Stage::get().get_player().get_stats().get_name(), 140);
		job_name = Text(Text::Font::A11M, Text::Alignment::LEFT, Color::Name::DUSTYGRAY,
			Stage::get().get_player().get_stats().get_jobname(), 140);
		selected_name = Text(Text::Font::A11B, Text::Alignment::LEFT, Color::Name::MINESHAFT, "", 145);
		selected_price = Text(Text::Font::A11B, Text::Alignment::RIGHT, Color::Name::ENDEAVOUR, "", 82);
		result_summary = Text(Text::Font::A11M, Text::Alignment::LEFT, Color::Name::DUSTYGRAY);
		empty_message = Text(Text::Font::A12B, Text::Alignment::CENTER, Color::Name::DUSTYGRAY,
			"No items match your search.", 360);

		preview_look = Stage::get().get_player().get_look();
		preview_look.set_stance(Stance::Id::STAND1);

		list_slider = Slider(
			Slider::Type::THIN_MINESHAFT,
			Range<int16_t>(GRID_Y + 8, GRID_Y + (GRID_ROWS - 1) * STRIDE_Y + CARD_H - 12),
			755,
			1,
			1,
			[this](bool upwards)
			{
				const int32_t page_count = static_cast<int32_t>((visible_items.size() + PAGE_SIZE - 1) / PAGE_SIZE);
				const int32_t next_page = page + (upwards ? -1 : 1);
				if (next_page >= 0 && next_page < page_count)
				{
					page = static_cast<int16_t>(next_page);
					update_items();
				}
			});

		dimension = Point<int16_t>(1024, 768);
		applied_search.clear();
		rebuild_filter();
	}

	void UICashShop::draw(float inter) const
	{
		UIElement::draw_sprites(inter);
		catalog_frame.draw(position, inter);

		// Catalog search bar and its input sit in the fixed header strip.
		search_bg.draw(DrawArgument(position));
		search_field.draw(position);
		if (search_field.empty() && search_field.get_state() != Textfield::State::FOCUSED)
			Text(Text::Font::A11M, Text::Alignment::LEFT, Color::Name::DUSTYGRAY, "Search items", 170).draw(position + Point<int16_t>(485, 9));

		Text(Text::Font::A11B, Text::Alignment::LEFT, Color::Name::WHITE, "MAPLE CASH SHOP").draw(position + Point<int16_t>(178, 10));
		Text(Text::Font::A11B, Text::Alignment::LEFT, Color::Name::MINESHAFT, "ITEM CATALOG").draw(position + Point<int16_t>(183, 38));

		player_name.draw(position + Point<int16_t>(16, 42));
		job_name.draw(position + Point<int16_t>(16, 58));
		Text(Text::Font::A11B, Text::Alignment::LEFT, Color::Name::DUSTYGRAY, "Browse categories").draw(position + Point<int16_t>(16, 82));

		for (uint8_t i = 0; i < CATEGORY_COUNT; i++)
		{
			const Point<int16_t> tab_pos(12, 100 + 32 * i);
			const Rectangle<int16_t> tab_bounds(tab_pos, tab_pos + Point<int16_t>(144, 24));
			if (tab_bounds.contains(UI::get().get_cursor_position() - position))
				category_hover_bg.draw(DrawArgument(position + tab_pos));
			if (i == category)
				category_active_bg.draw(DrawArgument(position + tab_pos));
			category_labels[i].draw(position + tab_pos + Point<int16_t>(12, 6));
		}

		if (visible_items.empty())
		{
			empty_message.draw(position + Point<int16_t>(458, 365));
			result_summary.change_text("0 matching items");
		}
		else
		{
			for (size_t i = 0; i < PAGE_SIZE; i++)
			{
				const size_t visible_index = static_cast<size_t>(page) * PAGE_SIZE + i;
				if (visible_index >= visible_items.size())
					continue;

				const int16_t item_index = static_cast<int16_t>(visible_items[visible_index]);
				const Item& item = items[item_index];
				const div_t cell = std::div(static_cast<int>(i), GRID_COLS);
				const Point<int16_t> card_pos(
					GRID_X + STRIDE_X * cell.rem,
					GRID_Y + STRIDE_Y * cell.quot);

				item_base.draw(DrawArgument(position + card_pos));
				if (item_index == selected_item)
					card_selected_bg.draw(DrawArgument(position + card_pos));

				// The 64px icon is centered in the framed preview square on each
				// English product tile. Cash item icons use a 32px source origin.
				item.draw(DrawArgument(position + card_pos + Point<int16_t>(10, 118), 2.0f, 2.0f));
				item_name[i].draw(position + card_pos + Point<int16_t>(81, 16));
				item_price[i].draw(position + card_pos + Point<int16_t>(81, 49));
				item_count[i].draw(position + card_pos + Point<int16_t>(81, 68));
			}

			const size_t first = static_cast<size_t>(page) * PAGE_SIZE + 1;
			const size_t last = std::min(visible_items.size(), static_cast<size_t>(page + 1) * PAGE_SIZE);
			result_summary.change_text("Showing " + std::to_string(first) + "-" + std::to_string(last)
				+ " of " + std::to_string(visible_items.size()) + " items");
		}

		result_summary.draw(position + Point<int16_t>(183,
			GRID_Y + (GRID_ROWS - 1) * STRIDE_Y + CARD_H + 8));
		list_slider.draw(position);

		// Keep the stage character inside the baked preview window. Arrow keys
		// move the selection through the catalog and update this preview.
		preview_look.draw(DrawArgument(position + Point<int16_t>(static_cast<int16_t>(char_x),
			static_cast<int16_t>(330.0f + char_yoff)), !facing_right), inter);

		if (selected_item >= 0 && selected_item < static_cast<int16_t>(items.size()))
		{
			const Item& item = items[selected_item];
			selected_name.change_text(item.get_name());
			string_format::format_with_ellipsis(selected_name, 145);
			std::string price = std::to_string(item.price);
			string_format::split_number(price);
			selected_price.change_text(price + " NX");
			selected_name.draw(position + Point<int16_t>(779, 344));
			selected_price.draw(position + Point<int16_t>(997, 344));
		}
		else
		{
			selected_name.change_text("Select an item to preview");
			selected_price.change_text("");
			string_format::format_with_ellipsis(selected_name, 145);
			selected_name.draw(position + Point<int16_t>(779, 344));
		}

		// NX labels are baked into the full-screen background in this order:
		// prepaid, credit, Maple Points. The response is credit, points, prepaid.
		std::string prepaid = std::to_string(get_cash_balance(2));
		std::string credit = std::to_string(get_cash_balance(0));
		std::string maple_points = std::to_string(get_cash_balance(1));
		string_format::split_number(prepaid);
		string_format::split_number(credit);
		string_format::split_number(maple_points);
		cash_balance_text[0].change_text(prepaid);
		cash_balance_text[1].change_text(credit);
		cash_balance_text[2].change_text(maple_points);
		for (int i = 0; i < 3; i++)
			cash_balance_text[i].draw(position + Point<int16_t>(159, 670 + 25 * i));

		// The middle right panel is the server-side cash shop locker. It is
		// separate from the character's CASH inventory and is refreshed by the
		// server whenever the shop opens.
		const std::vector<CashShopItemEntry>& locker = get_cash_shop_items();
		for (size_t i = 0; i < locker.size() && i < 14; i++)
		{
			const int32_t item_id = locker[i].item_id;
			if (item_id == 0 || !ItemData::get(item_id).is_valid())
				continue;

			const div_t cell = std::div(static_cast<int>(i), 7);
			const Point<int16_t> slot_pos(777 + 34 * cell.rem, 514 + 34 * cell.quot);
			ItemData::get(item_id).get_icon(false).draw(DrawArgument(position + slot_pos + Point<int16_t>(0, 32)));
		}

		UIElement::draw_buttons(inter);

		if (overlay != OVERLAY_NONE)
		{
			coupon_shade.draw(DrawArgument(position));
			if (overlay == OVERLAY_COUPON)
			{
				coupon_bg.draw(DrawArgument(position + Point<int16_t>(374, 352)));
				coupon_field.draw(position);
				buttons.at(BtCouponSubmit)->draw(position);
				buttons.at(BtCouponCancel)->draw(position);
			}
			else
			{
				help_bg.draw(DrawArgument(position + Point<int16_t>(224, 184)));
				buttons.at(BtHelpClose)->draw(position);
			}
		}
	}

	void UICashShop::send_key(int32_t keycode, bool pressed, bool escape)
	{
		if (pressed && escape)
		{
			if (overlay != OVERLAY_NONE)
				close_overlay();
			else
				schedule_exit();
			return;
		}

		if (overlay != OVERLAY_NONE)
			return;

		switch (keycode)
		{
		case KeyAction::Id::LEFT:
			if (pressed)
				navigate_items(-1);
			break;
		case KeyAction::Id::RIGHT:
			if (pressed)
				navigate_items(1);
			break;
		case KeyAction::Id::UP:
			if (pressed)
				navigate_items(-GRID_COLS);
			break;
		case KeyAction::Id::DOWN:
			if (pressed)
				navigate_items(GRID_COLS);
			break;
		case KeyAction::Id::JUMP:
			if (pressed && !char_jumping)
			{
				char_jumping = true;
				char_vy = -6.2f;
			}
			break;
		default:
			break;
		}
	}

	void UICashShop::update()
	{
		UIElement::update();
		search_field.update(position);
		if (overlay == OVERLAY_COUPON)
			coupon_field.update(position);

		const std::string query = search_field.get_text();
		if (query != applied_search)
		{
			applied_search = query;
			rebuild_filter();
		}

		preview_look.update(Constants::TIMESTEP);

		if (char_jumping)
		{
			char_yoff += char_vy;
			char_vy += 0.28f;
			if (char_yoff >= 0.0f)
			{
				char_yoff = 0.0f;
				char_vy = 0.0f;
				char_jumping = false;
			}
		}

		const uint8_t want = char_jumping ? Stance::Id::JUMP : Stance::Id::STAND1;
		if (want != cur_stance)
		{
			cur_stance = want;
			preview_look.set_stance(static_cast<Stance::Id>(cur_stance));
		}

		update_tooltip(UI::get().get_cursor_position());
	}

	void UICashShop::send_scroll(double yoffset)
	{
		if (overlay == OVERLAY_NONE && list_slider.isenabled())
			list_slider.send_scroll(yoffset);
	}

	Button::State UICashShop::button_pressed(uint16_t buttonid)
	{
		if (exit_pending)
			return Button::State::DISABLED;

		if (buttonid >= BtBuy)
		{
			const size_t visible_index = static_cast<size_t>(page) * PAGE_SIZE + (buttonid - BtBuy);
			if (visible_index < visible_items.size())
			{
				const int16_t item_index = static_cast<int16_t>(visible_items[visible_index]);
				choose_item(item_index);
				const Item& item = items[item_index];
				BuyCashItemPacket(1, item.sn).dispatch();
			}
			return Button::State::NORMAL;
		}

		switch (buttonid)
		{
		case BtExit:
			schedule_exit();
			return Button::State::NORMAL;
		case BtHelp:
		case BtCoupon:
		{
			if (tooltip_item_id != 0)
			{
				UI::get().clear_tooltip(Tooltip::Parent::SHOP);
				tooltip_item_id = 0;
			}

			overlay = buttonid == BtHelp ? OVERLAY_HELP : OVERLAY_COUPON;
			for (auto& button : buttons)
				button.second->set_active(false);
			if (overlay == OVERLAY_HELP)
				buttons[BtHelpClose]->set_active(true);
			else
			{
				buttons[BtCouponSubmit]->set_active(true);
				buttons[BtCouponCancel]->set_active(true);
				coupon_field.change_text("");
				coupon_field.set_state(Textfield::State::NORMAL);
			}
			return Button::State::NORMAL;
		}
		case BtSearch:
			applied_search = search_field.get_text();
			rebuild_filter();
			return Button::State::NORMAL;
		case BtChargeNX:
		{
			const std::string url = Configuration::get().get_chargenx();
#ifdef _WIN32
			ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#endif
			return Button::State::NORMAL;
		}
		case BtCouponSubmit:
			if (!coupon_field.get_text().empty())
				CouponCodePacket(coupon_field.get_text()).dispatch();
			close_overlay();
			return Button::State::NORMAL;
		case BtCouponCancel:
		case BtHelpClose:
			close_overlay();
			return Button::State::NORMAL;
		default:
			return Button::State::DISABLED;
		}
	}

	Cursor::State UICashShop::send_cursor(bool clicked, Point<int16_t> cursorpos)
	{
		if (exit_pending)
			return Cursor::State::IDLE;

		const Point<int16_t> local = cursorpos - position;
		update_tooltip(cursorpos);

		if (overlay == OVERLAY_COUPON)
		{
			const Cursor::State field_state = coupon_field.send_cursor(local, clicked);
			if (field_state != Cursor::State::IDLE)
				return field_state;
			return UIElement::send_cursor(clicked, cursorpos);
		}
		if (overlay == OVERLAY_HELP)
			return UIElement::send_cursor(clicked, cursorpos);

		if (local.y() < 30)
		{
			const Cursor::State field_state = search_field.send_cursor(local, clicked);
			if (field_state != Cursor::State::IDLE)
				return field_state;
		}

		if (clicked)
		{
			// A click on a locker item withdraws it to the character inventory.
			const std::vector<CashShopItemEntry>& locker = get_cash_shop_items();
			for (size_t i = 0; i < locker.size() && i < 14; i++)
			{
				const div_t cell = std::div(static_cast<int>(i), 7);
				const Point<int16_t> slot_pos(777 + 34 * cell.rem, 514 + 34 * cell.quot);
				const Rectangle<int16_t> slot_bounds(slot_pos, slot_pos + Point<int16_t>(32, 32));
				if (!slot_bounds.contains(local))
					continue;

				const CashShopItemEntry& item = locker[i];
				if (get_pending_cash_shop_withdrawal() != 0 || item.instance_id == 0)
					return Cursor::State::IDLE;

				Inventory& inventory = Stage::get().get_player().get_inventory();
				const InventoryType::Id destination = InventoryType::by_item_id(item.item_id);
				if (inventory.find_free_slot(destination) == 0)
				{
					chat::log("[Cash Shop] Your character inventory is full.", chat::LineType::RED);
					return Cursor::State::IDLE;
				}

				set_pending_cash_shop_withdrawal(item.instance_id);
				WithdrawCashItemPacket(item.instance_id).dispatch();
				return Cursor::State::IDLE;
			}

			for (uint8_t i = 0; i < CATEGORY_COUNT; i++)
			{
				const Rectangle<int16_t> category_bounds(Point<int16_t>(12, 100 + 32 * i), Point<int16_t>(156, 124 + 32 * i));
				if (category_bounds.contains(local))
				{
					category = i;
					page = 0;
					rebuild_filter();
					return Cursor::State::IDLE;
				}
			}

			for (size_t i = 0; i < PAGE_SIZE; i++)
			{
				const size_t visible_index = static_cast<size_t>(page) * PAGE_SIZE + i;
				if (visible_index >= visible_items.size())
					break;

				const div_t cell = std::div(static_cast<int>(i), GRID_COLS);
				const Point<int16_t> card_pos(
					GRID_X + STRIDE_X * cell.rem,
					GRID_Y + STRIDE_Y * cell.quot);
				const Rectangle<int16_t> card_bounds(card_pos, card_pos + Point<int16_t>(CARD_W, CARD_H));
				if (!card_bounds.contains(local))
					continue;

				const Button* buy_button = buttons.at(BtBuy + i).get();
				if (!buy_button->bounds(position).contains(cursorpos))
					choose_item(static_cast<int16_t>(visible_items[visible_index]));
				break;
			}
		}

		if (list_slider.isenabled())
		{
			const Cursor::State state = list_slider.send_cursor(local, clicked);
			if (state != Cursor::State::IDLE)
				return state;
		}

		return UIElement::send_cursor(clicked, cursorpos);
	}

	UIElement::Type UICashShop::get_type() const
	{
		return TYPE;
	}

	void UICashShop::exit_cashshop()
	{
		UI& ui = UI::get();
		Constants::Constants::get().clear_viewport_size();
		Window::get().update_viewport();
		ui.change_state(UI::State::GAME);

		Stage& stage = Stage::get();
		Player& player = stage.get_player();
		PlayerLoginPacket(player.get_oid()).dispatch();

		// The cash shop preview is still an ACTIVE Stage. Stage::load only
		// reloads a map from INACTIVE/TRANSITION, so clear the preview before
		// restoring the map and portal captured when the shop was opened.
		stage.clear();
		stage.load(return_mapid, return_portalid);
		stage.transfer_player();

		ui.enable();
		Timer::get().start();
		GraphicsGL::get().unlock();
	}

	void UICashShop::rebuild_filter()
	{
		visible_items.clear();
		const std::string query = string_format::tolower(search_field.get_text());

		for (size_t i = 0; i < items.size(); i++)
		{
			const Item& item = items[i];
			if (!item_matches_category(item))
				continue;

			if (!query.empty() && string_format::tolower(item.get_name()).find(query) == std::string::npos)
				continue;

			visible_items.push_back(i);
		}

		selected_item = -1;
		preview_look = Stage::get().get_player().get_look();
		preview_look.set_stance(static_cast<Stance::Id>(cur_stance));

		page = 0;
		update_items();
	}

	void UICashShop::update_items()
	{
		if (tooltip_item_id != 0)
		{
			UI::get().clear_tooltip(Tooltip::Parent::SHOP);
			tooltip_item_id = 0;
		}

		const size_t page_count = std::max<size_t>(1, (visible_items.size() + PAGE_SIZE - 1) / PAGE_SIZE);
		if (page >= static_cast<int16_t>(page_count))
			page = static_cast<int16_t>(page_count - 1);

		list_slider.setrows(page, 1, static_cast<int16_t>(page_count));
		list_slider.setenabled(page_count > 1);

		for (size_t i = 0; i < PAGE_SIZE; i++)
		{
			const size_t visible_index = static_cast<size_t>(page) * PAGE_SIZE + i;
			const bool found = visible_index < visible_items.size();
			buttons[BtBuy + i]->set_active(found && overlay == OVERLAY_NONE);

			if (!found)
			{
				item_name[i].change_text("");
				item_price[i].change_text("");
				item_count[i].change_text("");
				continue;
			}

			const Item& item = items[visible_items[visible_index]];
			item_name[i].change_text(item.get_name());
			string_format::format_with_ellipsis(item_name[i], 98);

			std::string price = std::to_string(item.price);
			string_format::split_number(price);
			item_price[i].change_text(price + " NX");
			item_count[i].change_text(item.count > 1 ? "Bundle of " + std::to_string(item.count) : "");
		}

		if (overlay == OVERLAY_NONE)
			for (auto& button : buttons)
				if (button.first < BtCouponSubmit)
					button.second->set_active(true);

		for (size_t i = 0; i < PAGE_SIZE; i++)
			buttons[BtBuy + i]->set_active((static_cast<size_t>(page) * PAGE_SIZE + i) < visible_items.size() && overlay == OVERLAY_NONE);
	}

	void UICashShop::update_tooltip(Point<int16_t> cursorpos)
	{
		int32_t hover_item_id = 0;
		if (overlay == OVERLAY_NONE)
		{
			const Point<int16_t> local = cursorpos - position;
			for (size_t i = 0; i < PAGE_SIZE; i++)
			{
				const size_t visible_index = static_cast<size_t>(page) * PAGE_SIZE + i;
				if (visible_index >= visible_items.size())
					break;

				const div_t cell = std::div(static_cast<int>(i), GRID_COLS);
				const Point<int16_t> card_pos(
					GRID_X + STRIDE_X * cell.rem,
					GRID_Y + STRIDE_Y * cell.quot);
				const Rectangle<int16_t> card_bounds(card_pos, card_pos + Point<int16_t>(CARD_W, CARD_H));
				if (card_bounds.contains(local))
				{
					hover_item_id = items[visible_items[visible_index]].get_itemid();
					break;
				}
			}
		}

		if (hover_item_id == tooltip_item_id)
			return;

		if (hover_item_id != 0)
			UI::get().show_item(Tooltip::Parent::SHOP, hover_item_id);
		else if (tooltip_item_id != 0)
			UI::get().clear_tooltip(Tooltip::Parent::SHOP);

		tooltip_item_id = hover_item_id;
	}

	void UICashShop::choose_item(int16_t index)
	{
		if (index < 0 || index >= static_cast<int16_t>(items.size()))
			return;

		selected_item = index;
		Player& player = Stage::get().get_player();
		preview_look = player.get_look();

		const int32_t item_id = items[index].get_itemid();
		if (item_id >= 1000000 && item_id < 2000000)
			preview_look.add_equip(item_id);

		preview_look.set_stance(static_cast<Stance::Id>(cur_stance));
	}

	void UICashShop::navigate_items(int32_t offset)
	{
		if (visible_items.empty())
			return;

		const auto selected = std::find(visible_items.begin(), visible_items.end(),
			static_cast<size_t>(selected_item));
		const int32_t current = selected == visible_items.end()
			? -1
			: static_cast<int32_t>(std::distance(visible_items.begin(), selected));
		const int32_t target = current < 0
			? 0
			: std::max<int32_t>(0, std::min<int32_t>(static_cast<int32_t>(visible_items.size()) - 1,
				current + offset));
		const int16_t target_page = static_cast<int16_t>(target / PAGE_SIZE);
		if (target_page != page)
		{
			page = target_page;
			update_items();
		}

		choose_item(static_cast<int16_t>(visible_items[target]));
	}

	bool UICashShop::item_matches_category(const Item& item) const
	{
		if (category == ALL_ITEMS)
			return true;

		const int32_t item_id = item.get_itemid();
		const int32_t item_type = item_id / 1000000;
		const int32_t item_prefix = item_id / 10000;
		switch (category)
		{
		case EQUIPMENT: return item_type == 1;
		case USE_ITEMS: return item_type == 2;
		case SETUP_ITEMS: return item_type == 3;
		case ETC_ITEMS: return item_type == 4;
		case CASH_ITEMS: return item_type >= 5;
		case PET_ITEMS: return item_prefix == 500;
		case PET_EQUIPS: return item_prefix == 180 || item_prefix == 181;
		case PET_MISC:
			// Pet name tags and pet food are sold as separate item families.
			return (item_prefix >= 182 && item_prefix < 190) || item_prefix == 212;
		default: return true;
		}
	}

	void UICashShop::close_overlay()
	{
		if (overlay == OVERLAY_COUPON)
		{
			UI::get().remove_textfield();
			coupon_field.set_state(Textfield::State::NORMAL);
		}

		overlay = OVERLAY_NONE;
		buttons[BtCouponSubmit]->set_active(false);
		buttons[BtCouponCancel]->set_active(false);
		buttons[BtHelpClose]->set_active(false);
		for (auto& button : buttons)
			if (button.first < BtCouponSubmit)
				button.second->set_active(true);
		update_items();
	}

	void UICashShop::schedule_exit()
	{
		if (exit_pending)
			return;

		exit_pending = true;
		Constants::Constants::get().clear_viewport_size();
		Window::get().update_viewport();

		Window::get().fadeout(
			0.025f,
			[]()
			{
				GraphicsGL::get().clear();
				ChangeMapPacket().dispatch();
			}
		);

		GraphicsGL::get().lock();
		Stage::get().clear();
		Timer::get().start();
	}

	void UICashShop::request_exit()
	{
		if (overlay != OVERLAY_NONE)
			close_overlay();
		else
			schedule_exit();
	}
}
