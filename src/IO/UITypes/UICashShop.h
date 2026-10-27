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
#pragma once

#include "../UIElement.h"

#include "../Components/Slider.h"
#include "../Components/Textfield.h"

#include "../../Character/Look/CharLook.h"
#include "../../Data/ItemData.h"
#include "../../Graphics/Geometry.h"
#include "../../Graphics/Text.h"

#include <string>
#include <vector>

namespace ms
{
	class UICashShop : public UIElement
	{
	public:
		static constexpr Type TYPE = UIElement::Type::CASHSHOP;
		static constexpr bool FOCUSED = true;
		static constexpr bool TOGGLED = false;

		UICashShop();

		void draw(float inter) const override;
		void update() override;
		void send_key(int32_t keycode, bool pressed, bool escape) override;
		void send_scroll(double yoffset) override;

		Button::State button_pressed(uint16_t buttonid) override;
		Cursor::State send_cursor(bool clicked, Point<int16_t> cursorpos) override;

		UIElement::Type get_type() const override;
		void exit_cashshop();
		void request_exit();

	private:
		static constexpr uint8_t GRID_COLS = 3;
		static constexpr uint8_t GRID_ROWS = 5;
		static constexpr uint8_t PAGE_SIZE = GRID_COLS * GRID_ROWS;
		static constexpr int16_t CARD_W = 185;
		static constexpr int16_t CARD_H = 124;
		static constexpr int16_t GRID_X = 176;
		static constexpr int16_t GRID_Y = 51;
		static constexpr int16_t STRIDE_X = 190;
		static constexpr int16_t STRIDE_Y = 127;

		class Item
		{
		public:
			Item(int32_t itemid, int32_t sn, int32_t price, uint16_t count)
				: sn(sn), price(price), count(count), data(ItemData::get(itemid)) {}

			int32_t sn;
			int32_t price;
			uint16_t count;

			void draw(const DrawArgument& args) const { data.get_icon(false).draw(args); }
			const std::string get_name() const { return data.get_name(); }
			const std::string& get_desc() const { return data.get_desc(); }
			int32_t get_itemid() const { return data.get_id(); }

		private:
			const ItemData& data;
		};

		enum Category : uint8_t
		{
			ALL_ITEMS,
			EQUIPMENT,
			USE_ITEMS,
			SETUP_ITEMS,
			ETC_ITEMS,
			CASH_ITEMS,
			PET_ITEMS,
			PET_EQUIPS,
			PET_MISC,
			CATEGORY_COUNT
		};

		enum Buttons : uint16_t
		{
			BtExit,
			BtHelp,
			BtCoupon,
			BtSearch,
			BtChargeNX,
			BtCouponSubmit,
			BtCouponCancel,
			BtHelpClose,
			BtBuy
		};

		enum Overlay : uint8_t
		{
			OVERLAY_NONE,
			OVERLAY_COUPON,
			OVERLAY_HELP
		};

		void rebuild_filter();
		void update_items();
		void update_tooltip(Point<int16_t> cursorpos);
		void choose_item(int16_t index);
		void navigate_items(int32_t offset);
		bool item_matches_category(const Item& item) const;
		void close_overlay();
		void schedule_exit();

		std::vector<Item> items;
		std::vector<size_t> visible_items;
		std::string applied_search;
		int16_t page;
		int16_t selected_item;
		uint8_t category;

		Sprite catalog_frame;
		Texture item_base;
		Texture coupon_bg;
		Texture help_bg;
		Texture search_bg;
		Textfield search_field;
		Textfield coupon_field;
		Slider list_slider;

		Text category_labels[CATEGORY_COUNT];
		Text item_name[PAGE_SIZE];
		Text item_price[PAGE_SIZE];
		Text item_count[PAGE_SIZE];
		Text player_name;
		Text job_name;
		mutable Text cash_balance_text[3];
		mutable Text selected_name;
		mutable Text selected_price;
		mutable Text result_summary;
		mutable Text empty_message;

		ColorBox category_active_bg;
		ColorBox category_hover_bg;
		ColorBox card_selected_bg;
		ColorBox coupon_shade;
		CharLook preview_look;

		float char_x;
		float char_yoff;
		float char_vy;
		bool char_jumping;
		bool facing_right;
		bool exit_pending;
		int32_t tooltip_item_id;
		int32_t return_mapid;
		uint8_t return_portalid;
		uint8_t cur_stance;
		Overlay overlay;
	};
}
